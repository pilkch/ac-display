#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>

#include <microhttpd.h>
#include <microhttpd_ws.h>

#include "ac_data.h"
#include "util.h"
#include "web_server.h"

// For "ms" literal suffix
using namespace std::chrono_literals;

// NOTE: This file is based on the websocket chat server example that ships with libmicrohttpd:
// https://github.com/Karlson2k/libmicrohttpd/blob/master/src/examples/websocket_chatserver_example.c

namespace {

const std::string HTML_MIMETYPE = "text/html";
const std::string CSS_MIMETYPE = "text/css";
const std::string JAVASCRIPT_MIMETYPE = "text/javascript";
const std::string SVG_XML_MIMETYPE = "image/svg+xml";

}

namespace acdisplay {

class cStaticResource {
public:
  void Clear();

  std::string request_path;
  std::string response_mime_type;
  std::string response_text;
};


const std::string PAGE_NOT_FOUND = "404 Not Found";
const std::string PAGE_INVALID_WEBSOCKET_REQUEST = "Invalid WebSocket request";



void ServerAddSecurityHeaders(struct MHD_Response* response)
{
  const std::pair<std::string, std::string> security_headers[] = {
    { "strict-transport-security", "max-age=31536000; includeSubDomains; preload" },
    { "x-content-type-options", "nosniff" },
    { "referrer-policy", "same-origin" },
    { "content-security-policy", "frame-ancestors 'none'" },
    { "permissions-policy", "" },
    { "cross-origin-embedder-policy-report-only", "require-corp; report-to=\"default\"" },
    { "cross-origin-opener-policy", "same-origin; report-to=\"default\"" },
    { "cross-origin-opener-policy-report-only", "same-origin; report-to=\"default\"" },
    { "cross-origin-resource-policy", "same-origin" }
  };
  for (auto&& header : security_headers) {
    MHD_add_response_header(response, header.first.c_str(), header.second.c_str());
  }
}

enum MHD_Result Server404NotFoundResponse(struct MHD_Connection* connection)
{
  struct MHD_Response* response = MHD_create_response_from_buffer_static(PAGE_NOT_FOUND.length(), PAGE_NOT_FOUND.c_str());
  const enum MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
  const char* mime = nullptr;
  MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_ENCODING, mime);
  ServerAddSecurityHeaders(response);
  MHD_destroy_response(response);
  return ret;
}

}

/**
 * This struct is used to keep the data of a connected user.
 * It is passed to the socket-receive thread (cWebSocketRequestHandler::ClientReceiveThreadFunction) as well as to
 * the socket-send thread (cWebSocketRequestHandler::ClientSendThreadFunction).
 * It can also be accessed via the global array users (mutex protected).
 */
struct ConnectedUser
{
  ConnectedUser() :
    fd(-1),
    urh(nullptr),
    ws(nullptr),
    extra_in(nullptr),
    extra_in_size(0),
    disconnect(false),
    wake_up_notify(false)
  {
  }

  /* the TCP/IP socket for reading/writing */
  MHD_socket fd;
  /* the UpgradeResponseHandle of libmicrohttpd (needed for closing the socket) */
  struct MHD_UpgradeResponseHandle* urh;
  /* the websocket encode/decode stream */
  struct MHD_WebSocketStream* ws;
  /* the possibly read data at the start (only used once) */
  char* extra_in;
  size_t extra_in_size;
  /* specifies whether the websocket shall be closed (true)) or not (false) */
  bool disconnect;
  /* condition variable to wake up the sender of this connection */
  std::condition_variable wake_up_sender;
  bool wake_up_notify; // Flag to tell the cWebSocketRequestHandler::ClientSendThreadFunction thread that it should wake up (This can only be modified when locked by the users_mutex)

  /* mutex to ensure that no send actions are mixed
     (sending can be done by send and recv thread;
      may not be simultaneously locked with users_mutex by the same thread) */
  std::mutex send_mutex;
};


/* the connected users data (May be accessed by all threads, but is protected by mutex) */
std::mutex users_mutex;
std::vector<ConnectedUser*> users;

/* specifies whether all websockets must close (1) or not (0) */
volatile int disconnect_all = 0;


namespace connected_users {

static void AddUser(struct ConnectedUser* cu)
{
  std::cout<<"AddUser"<<std::endl;

  // Lock the users mutex and add the user to the list
  std::lock_guard<std::mutex> lock(users_mutex);

  users.push_back(cu);
}

void RemoveUser(struct ConnectedUser* cu)
{
  std::cout<<"RemoveUser"<<std::endl;

  // Lock the users mutex and remove the user from the list
  std::lock_guard<std::mutex> lock(users_mutex);

  for (size_t i = 0; i < users.size(); i++) {
    if (users[i] == cu) {
      users.erase(users.begin() + i);
      break;
    }
  }
}

}


namespace network {

/**
 * Change socket to blocking.
 *
 * @param fd the socket to manipulate
 */
static void SocketMakeBlocking(MHD_socket fd)
{
#if defined(MHD_POSIX_SOCKETS)
  const int flags = fcntl(fd, F_GETFL);
  if (-1 == flags)
    abort();
  if ((flags & ~O_NONBLOCK) != flags)
    if (-1 == fcntl(fd, F_SETFL, flags & ~O_NONBLOCK))
      abort();
#elif defined(MHD_WINSOCK_SOCKETS)
  unsigned long flags = 0;

  if (0 != ioctlsocket(fd, (int) FIONBIO, &flags))
    abort();
#endif /* MHD_WINSOCK_SOCKETS */
}

/**
 * Sends all data of the given buffer via the TCP/IP socket
 *
 * @param fd  The TCP/IP socket which is used for sending
 */
static void SocketSendAll(struct ConnectedUser& cu, std::string_view buffer)
{
  std::lock_guard<std::mutex> lock(cu.send_mutex);

  while (!buffer.empty()) {
    const ssize_t result = send(cu.fd, buffer.data(), int(buffer.length()), 0);
    if (result < 0) {
      // Error
      if (EAGAIN == errno) {
        // It was just an EAGAIN error, we should try again
        continue;
      }
      break;
    } else if (0 == result) {
      // Didn't read anything
      break;
    }

    // Else there was a normal read, subtract what we read from the buffer
    buffer.remove_prefix(std::min<size_t>(buffer.length(), result));
  }
}

}


namespace acdisplay {

class cStaticResourcesRequestHandler {
public:
  bool LoadStaticResources();

  bool HandleRequest(struct MHD_Connection* connection, std::string_view url);

private:
  bool LoadStaticResource(const std::string& request_path, const std::string& response_mime_type, const std::string& file_path);

  // Yes, this could have been a map of std::string to std::pair<std::string, std::string>
  std::vector<cStaticResource> static_resources;
};

bool cStaticResourcesRequestHandler::LoadStaticResource(const std::string& request_path, const std::string& response_mime_type, const std::string& file_path)
{
  cStaticResource resource;

  const size_t nMaxFileSizeBytes = 20 * 1024;
  if (!util::ReadFileIntoString(file_path, nMaxFileSizeBytes, resource.response_text)) {
    std::cerr<<"File \""<<file_path<<"\" not found"<<std::endl;
    return false;
  }

  resource.request_path = request_path;
  resource.response_mime_type = response_mime_type;

  static_resources.push_back(resource);

  return true;
}

bool cStaticResourcesRequestHandler::LoadStaticResources()
{
  static_resources.clear();

  // A bit of a hack, this will find the resources folder either from the main folder, or from the fuzz folder
  const std::string resources_folder = util::TestFolderExists("resources") ? "./resources" : "../resources";

  return (
    LoadStaticResource("/", HTML_MIMETYPE, resources_folder + "/index.html") &&
    LoadStaticResource("/style.css", CSS_MIMETYPE, resources_folder + "/style.css") &&
    LoadStaticResource("/receive.js", JAVASCRIPT_MIMETYPE, resources_folder + "/receive.js") &&
    LoadStaticResource("/dial.js", JAVASCRIPT_MIMETYPE, resources_folder + "/dial.js") &&
    LoadStaticResource("/util.js", JAVASCRIPT_MIMETYPE, resources_folder + "/util.js") &&
    LoadStaticResource("/disconnected_icon.svg", SVG_XML_MIMETYPE, resources_folder + "/disconnected_icon.svg") &&
    LoadStaticResource("/fullscreen_icon.svg", SVG_XML_MIMETYPE, resources_folder + "/fullscreen_icon.svg") &&
    LoadStaticResource("/favicon.svg", SVG_XML_MIMETYPE, resources_folder + "/favicon.svg")
  );
}

bool cStaticResourcesRequestHandler::HandleRequest(struct MHD_Connection* connection, std::string_view url)
{
  std::cout<<"cStaticResourcesRequestHandler::HandleRequest "<<url<<std::endl;

  // Handle static resources
  if (!url.empty() && (url[0] == '/')) {
    for (auto&& resource : static_resources) {
      if (url == resource.request_path) {
        // This is the requested resource so create a response
        struct MHD_Response* response = MHD_create_response_from_buffer_static(resource.response_text.length(), resource.response_text.c_str());
        MHD_add_response_header(response, "Content-Type", resource.response_mime_type.c_str());
        ServerAddSecurityHeaders(response);
        const int result = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return (result == MHD_YES);
      }
    }
  }

  return false;
}




class cWebSocketRequestHandler {
public:
  bool HandleRequest(struct MHD_Connection* connection, std::string_view url, std::string_view version);

private:
  static void UpgradeHandler(
    void* cls,
    struct MHD_Connection* connection,
    void* req_cls,
    const char* extra_in,
    size_t extra_in_size,
    MHD_socket fd,
    struct MHD_UpgradeResponseHandle* urh
  );

  static void* ClientReceiveThreadFunction(void* cls);
  static void* ClientSendThreadFunction(void* cls);

  static void SendWebSocketMessage(struct ConnectedUser& user, std::string_view message);
  static void SendWebSocketCarConfig(struct ConnectedUser& user);
  static void SendWebSocketUpdate(struct ConnectedUser& user);
  static bool ReceiveWebSocket(struct ConnectedUser& cu, char* buf, size_t buf_len);
};


/**
 * Function called after a protocol "upgrade" response was sent
 * successfully and the socket should now be controlled by some
 * protocol other than HTTP.
 *
 * Any data already received on the socket will be made available in
 * @e extra_in.  This can happen if the application sent extra data
 * before MHD send the upgrade response.  The application should
 * treat data from @a extra_in as if it had read it from the socket.
 *
 * Note that the application must not close() @a sock directly,
 * but instead use #MHD_upgrade_action() for special operations
 * on @a sock.
 *
 * Data forwarding to "upgraded" @a sock will be started as soon
 * as this function return.
 *
 * Except when in 'thread-per-connection' mode, implementations
 * of this function should never block (as it will still be called
 * from within the main event loop).
 *
 * @param cls closure, whatever was given to #MHD_create_response_for_upgrade().
 * @param connection original HTTP connection handle,
 *                   giving the function a last chance
 *                   to inspect the original HTTP request
 * @param req_cls last value left in `req_cls` of the `MHD_AccessHandlerCallback`
 * @param extra_in if we happened to have read bytes after the
 *                 HTTP header already (because the client sent
 *                 more than the HTTP header of the request before
 *                 we sent the upgrade response),
 *                 these are the extra bytes already read from @a sock
 *                 by MHD.  The application should treat these as if
 *                 it had read them from @a sock.
 * @param extra_in_size number of bytes in @a extra_in
 * @param sock socket to use for bi-directional communication
 *        with the client.  For HTTPS, this may not be a socket
 *        that is directly connected to the client and thus certain
 *        operations (TCP-specific setsockopt(), getsockopt(), etc.)
 *        may not work as expected (as the socket could be from a
 *        socketpair() or a TCP-loopback).  The application is expected
 *        to perform read()/recv() and write()/send() calls on the socket.
 *        The application may also call shutdown(), but must not call
 *        close() directly.
 * @param urh argument for #MHD_upgrade_action()s on this @a connection.
 *        Applications must eventually use this callback to (indirectly)
 *        perform the close() action on the @a sock.
 */
void cWebSocketRequestHandler::UpgradeHandler(void* cls,
                 struct MHD_Connection* connection,
                 void* req_cls,
                 const char* extra_in,
                 size_t extra_in_size,
                 MHD_socket fd,
                 struct MHD_UpgradeResponseHandle* urh)
{
  std::cout<<"cWebSocketRequestHandler::UpgradeHandler"<<std::endl;

  (void) cls;         /* Unused. Silent compiler warning. */
  (void) connection;  /* Unused. Silent compiler warning. */
  (void) req_cls;     /* Unused. Silent compiler warning. */

  /* This callback must return as soon as possible. */

  /* allocate new connected user */
  struct ConnectedUser* cu = new ConnectedUser;
  if (nullptr == cu) {
    std::cerr<<"cWebSocketRequestHandler::UpgradeHandler Error allocating memory"<<std::endl;
    return;
  }

  if (0 != extra_in_size) {
    cu->extra_in = new char[extra_in_size];
    if (nullptr == cu->extra_in)
      abort();
    memcpy(cu->extra_in, extra_in, extra_in_size);
  }
  cu->extra_in_size = extra_in_size;
  cu->fd = fd;
  cu->urh = urh;

  /* create thread for the new connected user */
  pthread_t pt;
  if (0 != pthread_create(&pt, nullptr, &ClientReceiveThreadFunction, cu))
    abort();
  pthread_detach(pt);
}

void cWebSocketRequestHandler::SendWebSocketMessage(struct ConnectedUser& user, std::string_view message)
{
  char* frame_data = nullptr;
  size_t frame_len = 0;

  const int status = MHD_websocket_encode_text(
    user.ws,
    message.data(), message.size(),
    MHD_WEBSOCKET_FRAGMENTATION_NONE,
    &frame_data, &frame_len,
    nullptr
  );
  if (MHD_WEBSOCKET_STATUS_OK == status) {
    network::SocketSendAll(user, std::string_view(frame_data, frame_len));

    // Free the frame data
    MHD_websocket_free(user.ws, frame_data);
  }
}

void cWebSocketRequestHandler::SendWebSocketCarConfig(struct ConnectedUser& user)
{
  // Get a copy of the AC data
  mutex_ac_data.lock();
  const cACData copy = ac_data;
  mutex_ac_data.unlock();

  // Create our car config
  const std::string message = "car_config|" +
    std::to_string(copy.config_rpm_red_line) + "|" +
    std::to_string(copy.config_rpm_maximum) + "|" +
    std::to_string(copy.config_speedometer_red_line_kph) + "|" +
    std::to_string(copy.config_speedometer_maximum_kph)
  ;

  SendWebSocketMessage(user, message);
}

void cWebSocketRequestHandler::SendWebSocketUpdate(struct ConnectedUser& user)
{
  // Get a copy of the AC data
  mutex_ac_data.lock();
  const cACData copy = ac_data;
  mutex_ac_data.unlock();

  // Create our car update
  const std::string message = "car_update|" +
    std::to_string(copy.gear) + "|" +
    std::to_string(copy.accelerator_0_to_1) + "|" +
    std::to_string(copy.brake_0_to_1) + "|" +
    std::to_string(copy.clutch_0_to_1) + "|" +
    std::to_string(copy.rpm) + "|" +
    std::to_string(copy.speed_kmh) + "|" +
    std::to_string(copy.lap_time_ms) + "|" +
    std::to_string(copy.last_lap_ms) + "|" +
    std::to_string(copy.best_lap_ms) + "|" +
    std::to_string(copy.lap_count)
  ;

  SendWebSocketMessage(user, message);
}

/**
 * Sends messages from the message list over the TCP/IP socket
 * after encoding it with the websocket stream.
 * This is also used for server-side actions,
 * because the thread for receiving messages waits for
 * incoming data and cannot be woken up.
 * But the sender thread can be woken up easily.
 *
 * @param cls          The connected user
 * @return             Always nullptr
 */
void* cWebSocketRequestHandler::ClientSendThreadFunction(void* cls)
{
  std::cout<<"cWebSocketRequestHandler::ClientSendThreadFunction"<<std::endl;

  struct ConnectedUser& cu = *((ConnectedUser*)cls);

  // Send the config once at the start
  SendWebSocketCarConfig(cu);

  std::chrono::high_resolution_clock::time_point last = std::chrono::high_resolution_clock::now();

  bool running = true;

  // Lock the users mutex
  std::unique_lock lock(users_mutex);

  while (running) {
    /* loop while not all messages processed */
    bool all_messages_read = false;
    while (running && !all_messages_read) {
      if (1 == disconnect_all) {
        // The application is closing so we need to disconnect all users
        if (cu.urh != nullptr) {
          /* Close the TCP/IP socket. */
          /* This will also wake-up the waiting receive-thread for this connected user. */
          struct MHD_UpgradeResponseHandle* urh = cu.urh;
          cu.urh = nullptr;
          MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        }

        running = false;
      } else if (cu.disconnect) {
        /* The sender thread shall close. */
        /* This is only requested by the receive thread, so we can just leave. */
        running = false;
      } else {
        all_messages_read = true;
      }
    }

    if (!running) {
      break;
    }

    // Reset the wake up flag
    cu.wake_up_notify = false;

    /* Wait for wake up. */
    /* This will automatically unlock the mutex while waiting and */
    /* lock the mutex after waiting */
    cu.wake_up_sender.wait_until(lock, std::chrono::system_clock::now() + 20ms, [&cu]{ return cu.wake_up_notify; });

    const std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();

    // Send an update every 20 milliseconds (Roughly)
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count() > 20) {
      lock.unlock();
      SendWebSocketUpdate(cu);
      lock.lock();

      last = now;
    }
  }

  std::cout<<"cWebSocketRequestHandler::ClientSendThreadFunction returning"<<std::endl;
  return nullptr;
}


/**
* Parses received data from the TCP/IP socket with the websocket stream
*
* @param cu           The connected user
* @param new_name     The new user name
* @param new_name_len The length of the new name
*/
bool cWebSocketRequestHandler::ReceiveWebSocket(struct ConnectedUser& cu, char *buf, size_t buf_len)
{
  std::cout<<"cWebSocketRequestHandler::ReceiveWebSocket"<<std::endl;

  size_t buf_offset = 0;
  while (buf_offset < buf_len) {
    size_t new_offset = 0;
    char* frame_data = nullptr;
    size_t frame_len  = 0;
    int status = MHD_websocket_decode(cu.ws,
                                       buf + buf_offset, buf_len - buf_offset,
                                       &new_offset,
                                       &frame_data, &frame_len);
    if (0 > status) {
      /* an error occurred and the connection must be closed */
      if (nullptr != frame_data) {
        /* depending on the WebSocket flag */
        /* MHD_WEBSOCKET_FLAG_GENERATE_CLOSE_FRAMES_ON_ERROR */
        /* close frames might be generated on errors */
        network::SocketSendAll(cu, std::string_view(frame_data, frame_len));
        MHD_websocket_free(cu.ws, frame_data);
      }
      return false;
    } else {
      buf_offset += new_offset;

      if (0 < status) {
        /* the frame is complete */
        switch (status) {
        case MHD_WEBSOCKET_STATUS_CLOSE_FRAME:
          /* if we receive a close frame, we will respond with one */
          MHD_websocket_free(cu.ws, frame_data);
          {
            char *result = nullptr;
            size_t result_len = 0;
            int er = MHD_websocket_encode_close(cu.ws,
                                                 MHD_WEBSOCKET_CLOSEREASON_REGULAR,
                                                 nullptr,
                                                 0,
                                                 &result, &result_len);
            if (MHD_WEBSOCKET_STATUS_OK == er) {
              network::SocketSendAll(cu, std::string_view(result, result_len));
              MHD_websocket_free(cu.ws, result);
            }
          }
          return false;

        default:
          /* This case should really never happen, */
          /* because there are only five types of (finished) websocket frames. */
          /* If it is ever reached, it means that there is memory corruption. */
          MHD_websocket_free(cu.ws, frame_data);
          return false;
        }
      }
    }
  }

  return true;
}


/**
 * Receives messages from the TCP/IP socket and
 * initializes the connected user.
 *
 * @param cls The connected user
 * @return    Always nullptr
 */
void* cWebSocketRequestHandler::ClientReceiveThreadFunction(void* cls)
{
  std::cout<<"cWebSocketRequestHandler::ClientReceiveThreadFunction"<<std::endl;
  struct ConnectedUser* cu = (ConnectedUser*)cls;

  /* make the socket blocking */
  network::SocketMakeBlocking(cu->fd);

  /* add the user to the user list */
  connected_users::AddUser(cu);

  /* initialize the web socket stream for encoding/decoding */
  int result = MHD_websocket_stream_init(&cu->ws, MHD_WEBSOCKET_FLAG_SERVER | MHD_WEBSOCKET_FLAG_NO_FRAGMENTS, 0);
  if (MHD_WEBSOCKET_STATUS_OK != result) {
    connected_users::RemoveUser(cu);
    MHD_upgrade_action(cu->urh, MHD_UPGRADE_ACTION_CLOSE);
    delete[] cu->extra_in;
    delete cu;
    return nullptr;
  }


  /* start the message-send thread */
  pthread_t pt;
  if (0 != pthread_create(&pt, nullptr, &ClientSendThreadFunction, cu))
    abort();

  /* start by parsing extra data MHD may have already read, if any */
  if (0 != cu->extra_in_size) {
    if (!ReceiveWebSocket(*cu, cu->extra_in, cu->extra_in_size)) {
      connected_users::RemoveUser(cu);

      {
        std::lock_guard<std::mutex> lock(users_mutex);

        cu->disconnect = true;
        cu->wake_up_notify = true;
        cu->wake_up_sender.notify_one();
      }

      pthread_join(pt, nullptr);

      if (cu->urh != nullptr) {
        struct MHD_UpgradeResponseHandle* urh = cu->urh;
        cu->urh = nullptr;
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
      }
      MHD_websocket_stream_free(cu->ws);
      delete[] cu->extra_in;
      delete cu;
      return nullptr;
    }
    delete[] cu->extra_in;
    cu->extra_in = nullptr;
  }

  char buf[128] = { 0 };
  ssize_t got = 0;

  /* the main loop for receiving data */
  while (true) {
    got = recv(cu->fd, buf, sizeof(buf), 0);
    if (0 >= got) {
      /* the TCP/IP socket has been closed */
      break;
    }
    if (0 < got) {
      if (!ReceiveWebSocket(*cu, buf, size_t(got))) {
        /* A websocket protocol error occurred */
        connected_users::RemoveUser(cu);

        {
          std::lock_guard<std::mutex> lock(users_mutex);

          cu->disconnect = true;
          cu->wake_up_notify = true;
          cu->wake_up_sender.notify_one();
        }

        pthread_join(pt, nullptr);
        if (cu->urh != nullptr) {
          struct MHD_UpgradeResponseHandle* urh = cu->urh;
          cu->urh = nullptr;
          MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        }
        MHD_websocket_stream_free(cu->ws);
        delete cu;
        return nullptr;
      }
    }
  }

  /* cleanup */
  connected_users::RemoveUser(cu);

  {
    std::lock_guard<std::mutex> lock(users_mutex);

    cu->disconnect = true;
    cu->wake_up_notify = true;
    cu->wake_up_sender.notify_one();
  }

  pthread_join(pt, nullptr);

  if (cu->urh != nullptr) {
    struct MHD_UpgradeResponseHandle* urh = cu->urh;  
    cu->urh = nullptr;
    MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
  }
  MHD_websocket_stream_free(cu->ws);
  delete cu;

  return nullptr;
}

bool cWebSocketRequestHandler::HandleRequest(struct MHD_Connection* connection, std::string_view url, std::string_view version)
{
  std::cout<<"cWebSocketRequestHandler::HandleRequest "<<url<<std::endl;

  struct MHD_Response *response = nullptr;
  int result = MHD_YES;

  if (url == "/ACDisplayServerWebSocket") {
    /**
      * The path for the websocket updates has been accessed.
      * For a valid WebSocket request, at least five headers are required:
      * 1. "Host: <name>"
      * 2. "Connection: Upgrade"
      * 3. "Upgrade: websocket"
      * 4. "Sec-WebSocket-Version: 13"
      * 5. "Sec-WebSocket-Key: <base64 encoded value>"
      * Values are compared in a case-insensitive manner.
      * Furthermore it must be a HTTP/1.1 or higher GET request.
      * See: https://tools.ietf.org/html/rfc6455#section-4.2.1
      *
      * To make this example portable we skip the Host check
      */

    bool is_valid = true;

    /* check whether an websocket upgrade is requested */
    if (0 != MHD_websocket_check_http_version(version.data())) {
      is_valid = false;
    }

    const char* value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONNECTION);
    if (0 != MHD_websocket_check_connection_header(value)) {
      is_valid = false;
    }
    value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_UPGRADE);
    if (0 != MHD_websocket_check_upgrade_header(value)) {
      is_valid = false;
    }
    value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_SEC_WEBSOCKET_VERSION);
    if (0 != MHD_websocket_check_version_header(value)) {
      is_valid = false;
    }
    value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_SEC_WEBSOCKET_KEY);

    char sec_websocket_accept[29] = { 0 };
    if (0 != MHD_websocket_create_accept_header(value, sec_websocket_accept)) {
      is_valid = false;
    }

    if (is_valid) {
      /* create the response for upgrade */
      response = MHD_create_response_for_upgrade(&cWebSocketRequestHandler::UpgradeHandler, nullptr);

      /**
        * For the response we need at least the following headers:
        * 1. "Connection: Upgrade"
        * 2. "Upgrade: websocket"
        * 3. "Sec-WebSocket-Accept: <base64value>"
        * The value for Sec-WebSocket-Accept can be generated with MHD_websocket_create_accept_header.
        * It requires the value of the Sec-WebSocket-Key header of the request.
        * See also: https://tools.ietf.org/html/rfc6455#section-4.2.2
        */
      MHD_add_response_header(response, MHD_HTTP_HEADER_UPGRADE, "websocket");
      MHD_add_response_header(response, MHD_HTTP_HEADER_SEC_WEBSOCKET_ACCEPT, sec_websocket_accept);
      result = MHD_queue_response(connection, MHD_HTTP_SWITCHING_PROTOCOLS, response);
      MHD_destroy_response(response);
    } else {
      /* return error page */
      struct MHD_Response* response = MHD_create_response_from_buffer_static(PAGE_INVALID_WEBSOCKET_REQUEST.length(), PAGE_INVALID_WEBSOCKET_REQUEST.c_str());
      result = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
      MHD_destroy_response(response);
    }
  } else {
    struct MHD_Response* response = MHD_create_response_from_buffer_static(PAGE_NOT_FOUND.length(), PAGE_NOT_FOUND.c_str());
    result = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
  }

  return (result == MHD_YES);
}

}


namespace acdisplay {

class cWebServer {
public:
  cWebServer(cStaticResourcesRequestHandler& static_resources_request_handler, cWebSocketRequestHandler& web_socket_request_handler);
  ~cWebServer();

  bool Open(const util::cIPAddress& host, uint16_t port, const std::string& private_key, const std::string& public_cert);
  void NoMoreConnections();
  bool Close();

private:
  static enum MHD_Result _OnRequest(
    void* cls,
    struct MHD_Connection* connection,
    const char* url,
    const char* method,
    const char* version,
    const char* upload_data,
    size_t* upload_data_size,
    void** req_cls
  );

  struct MHD_Daemon* daemon;

  cStaticResourcesRequestHandler& static_resources_request_handler;
  cWebSocketRequestHandler& web_socket_request_handler;
};

cWebServer::cWebServer(cStaticResourcesRequestHandler& _static_resources_request_handler, cWebSocketRequestHandler& _web_socket_request_handler) :
  daemon(nullptr),
  static_resources_request_handler(_static_resources_request_handler),
  web_socket_request_handler(_web_socket_request_handler)
{
}

cWebServer::~cWebServer()
{
  NoMoreConnections();
  Close();
}

bool cWebServer::Open(const util::cIPAddress& host, uint16_t port, const std::string& private_key, const std::string& public_cert)
{
  const std::string address(util::ToString(host));

  struct sockaddr_in sad;
  memset(&sad, 0, sizeof(sad));
  if (inet_pton(AF_INET, address.c_str(), &(sad.sin_addr.s_addr)) != 1) {
    std::cerr<<"V4 inet_pton fail for "<<address<<std::endl;
    return false;
  }

  sad.sin_family = AF_INET;
  sad.sin_port   = htons(port);

  if (!private_key.empty() && !public_cert.empty()) {
    std::cout<<"cWebServer::Run Starting server at https://"<<address<<":"<<port<<"/"<<std::endl;
    std::string server_key;
    util::ReadFileIntoString(private_key, 10 * 1024, server_key);
    std::string server_cert;
    util::ReadFileIntoString(public_cert, 10 * 1024, server_cert);

    daemon = MHD_start_daemon(MHD_ALLOW_UPGRADE | MHD_USE_AUTO
                          | MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG
                          | MHD_USE_TLS,
                          port,
                          nullptr, nullptr,
                          &_OnRequest, this,
                          MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)120,
                          MHD_OPTION_HTTPS_MEM_KEY, server_key.c_str(),
                          MHD_OPTION_HTTPS_MEM_CERT, server_cert.c_str(),
                          MHD_OPTION_SOCK_ADDR, (struct sockaddr*)&sad,
                          MHD_OPTION_LISTENING_ADDRESS_REUSE, 1, // Mainly for fuzz testing so that we can bind the port repeatedly in quick succession
                          MHD_OPTION_END);
  } else {
    std::cout<<"cWebServer::Run Starting server at http://"<<address<<":"<<port<<"/"<<std::endl;
    daemon = MHD_start_daemon(MHD_ALLOW_UPGRADE | MHD_USE_AUTO
                          | MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG,
                          port,
                          nullptr, nullptr,
                          &_OnRequest, this,
                          MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)120,
                          MHD_OPTION_SOCK_ADDR, (struct sockaddr*)&sad,
                          MHD_OPTION_LISTENING_ADDRESS_REUSE, 1, // Mainly for fuzz testing so that we can bind the port repeatedly in quick succession
                          MHD_OPTION_END);
  }

  return (daemon != nullptr);
}

void cWebServer::NoMoreConnections()
{
  if (daemon != nullptr) {
    MHD_quiesce_daemon(daemon);
  }
}

bool cWebServer::Close()
{
  // Stop the server
  if (daemon != nullptr) {
    MHD_stop_daemon(daemon);
    daemon = nullptr;
  }

  return true;
}



/**
 * Function called by the MHD_daemon when the client tries to access a page.
 *
 * This is used to provide html, css, javascript, images, and icons,
 * and to initialize a websocket connection.
 * The rules for the initialization of a websocket connection
 * are listed near the URL check of "/ACDisplayServerWebSocket".
 *
 * @param cls closure, whatever was given to #MHD_start_daemon().
 * @param connection The HTTP connection handle
 * @param url The requested URL
 * @param method The request method (typically "GET")
 * @param version The HTTP version
 * @param upload_data Given upload data for POST requests
 * @param upload_data_size The size of the upload data
 * @param req_cls A pointer for request specific data
 * @return MHD_YES on success or MHD_NO on error.
 */
enum MHD_Result cWebServer::_OnRequest(
  void* cls,
  struct MHD_Connection* connection,
  const char* url,
  const char* method,
  const char* version,
  const char* upload_data,
  size_t* upload_data_size,
  void** req_cls
)
{
  //std::cout<<"cWebServer::_OnRequest "<<url<<std::endl;
  (void)upload_data;
  (void)upload_data_size;

  static int aptr = 0;

  if (0 != strcmp(method, "GET")) {
    return MHD_NO;              /* unexpected method */
  }

  if (&aptr != *req_cls) {
    // Never respond on first call
    *req_cls = &aptr;
    return MHD_YES;
  }
  // We have handled a request before
  *req_cls = nullptr;


  cWebServer* pThis = static_cast<cWebServer*>(cls);
  if (pThis == nullptr) {
    std::cerr<<"Error pThis is NULL"<<std::endl;
    return MHD_NO;
  }

  // Handle static resources
  if (pThis->static_resources_request_handler.HandleRequest(connection, url)) {
    return MHD_YES;
  }

  // Handle web socket requests
  if (pThis->web_socket_request_handler.HandleRequest(connection, url, version)) {
    return MHD_YES;
  }

  // Unknown resource
  const enum MHD_Result result = Server404NotFoundResponse(connection);

  return result;
}


cWebServerManager::cWebServerManager() :
  static_resources_request_handler(nullptr),
  web_socket_request_handler(nullptr),
  webserver(nullptr)
{
}

cWebServerManager::~cWebServerManager()
{
  if (webserver != nullptr) {
    delete webserver;
    webserver = nullptr;
  }

  if (web_socket_request_handler != nullptr) {
    delete web_socket_request_handler;
    web_socket_request_handler = nullptr;
  }

  if (static_resources_request_handler != nullptr) {
    delete static_resources_request_handler;
    static_resources_request_handler = nullptr;
  }
}

bool cWebServerManager::Create(const util::cIPAddress& host, uint16_t port, const std::string& private_key, const std::string& public_cert)
{
  if (
    (static_resources_request_handler != nullptr) ||
    (web_socket_request_handler != nullptr) ||
    (webserver != nullptr)
  ) {
    std::cerr<<"Error already created"<<std::endl;
    return false;
  }

  static_resources_request_handler = new cStaticResourcesRequestHandler;
  web_socket_request_handler = new cWebSocketRequestHandler;

  // Load the static resources
  if (!static_resources_request_handler->LoadStaticResources()) {
    return false;
  }

  webserver = new cWebServer(*static_resources_request_handler, *web_socket_request_handler);
  if (!webserver->Open(host, port, private_key, public_cert)) {
    std::cerr<<"Error opening web server"<<std::endl;
    return false;
  }

  std::cout<<"Server is running"<<std::endl;

  return true;
};

bool cWebServerManager::Destroy()
{
  std::cout<<"Shutting down the server"<<std::endl;

  webserver->NoMoreConnections();

  // Tell each connection to wake up and disconnect
  {
    std::lock_guard<std::mutex> lock(users_mutex);

    disconnect_all = 1;
    for (auto&& cu : users) {
      std::cout<<"Notifying a user connection"<<std::endl;
      cu->wake_up_notify = true;
      cu->wake_up_sender.notify_one();
    }
  }

  // Wait for the connection threads to respond
  std::cout<<"Waiting for the connection threads to respond"<<std::endl;
  sleep(2);

  // Tell each connection to close
  std::cout<<"Closing connections"<<std::endl;
  {
    std::lock_guard<std::mutex> lock(users_mutex);
    if (!users.empty()) {
      std::cerr<<"Error there are still active connections"<<std::endl;
    }
  }

  // Wait for the connection threads to close
  sleep(2);

  /* usually we should wait here in a safe way for all threads to disconnect, */
  /* but we skip this in the example */

  webserver->Close();

  return true;
}

}
