#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
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

namespace acdisplay {

const std::string HTML_MIMETYPE = "text/html";
const std::string CSS_MIMETYPE = "text/css";
const std::string JAVASCRIPT_MIMETYPE = "text/javascript";
const std::string SVG_XML_MIMETYPE = "image/svg+xml";

class cStaticResource {
public:
  void Clear();

  std::string request_path;
  std::string response_mime_type;
  std::string response_text;
};


class cStaticResourcesRequestHandler {
public:
  bool LoadStaticResources();

  bool HandleRequest(struct MHD_Connection* connection, std::string_view url);

private:
  bool LoadStaticResource(const std::string& request_path, const std::string& response_mime_type, const std::string& file_path);

  // Yes, this could have been a map of std::string to std::pair<std::string, std::string>
  std::vector<cStaticResource> static_resources;
};

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

  return (
    LoadStaticResource("/", HTML_MIMETYPE, "./resources/index.html") &&
    LoadStaticResource("/style.css", CSS_MIMETYPE, "./resources/style.css") &&
    LoadStaticResource("/receive.js", JAVASCRIPT_MIMETYPE, "./resources/receive.js") &&
    LoadStaticResource("/dial.js", JAVASCRIPT_MIMETYPE, "./resources/dial.js") &&
    LoadStaticResource("/util.js", JAVASCRIPT_MIMETYPE, "./resources/util.js") &&
    LoadStaticResource("/disconnected_icon.svg", SVG_XML_MIMETYPE, "./resources/disconnected_icon.svg") &&
    LoadStaticResource("/fullscreen_icon.svg", SVG_XML_MIMETYPE, "./resources/fullscreen_icon.svg") &&
    LoadStaticResource("/favicon.svg", SVG_XML_MIMETYPE, "./resources/favicon.svg")
  );
}



const std::string PAGE_NOT_FOUND = "404 Not Found";
const std::string PAGE_INVALID_WEBSOCKET_REQUEST = "Invalid WebSocket request";


}

/**
 * This struct is used to keep the data of a connected user.
 * It is passed to the socket-receive thread (WebSocketClientReceiveThreadFunction) as well as to
 * the socket-send thread (WebSocketClientSendThreadFunction).
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
    user_id(0),
    disconnect(0)
  {
    memset(this, 0, sizeof(ConnectedUser));
  }

  /* the TCP/IP socket for reading/writing */
  MHD_socket fd;
  /* the UpgradeResponseHandle of libmicrohttpd (needed for closing the socket) */
  struct MHD_UpgradeResponseHandle *urh;
  /* the websocket encode/decode stream */
  struct MHD_WebSocketStream *ws;
  /* the possibly read data at the start (only used once) */
  char *extra_in;
  size_t extra_in_size;
  /* the unique user id (counting from 1, ids will never be re-used) */
  size_t user_id;
  /* specifies whether the websocket shall be closed (1) or not (0) */
  int disconnect;
  /* condition variable to wake up the sender of this connection */
  pthread_cond_t wake_up_sender;
  /* mutex to ensure that no send actions are mixed
     (sending can be done by send and recv thread;
      may not be simultaneously locked with users_mutex by the same thread) */
  pthread_mutex_t send_mutex;
};


/* the unique user counter for new users (only accessed by main thread) */
size_t unique_user_id = 0;

/* the connected users data (May be accessed by all threads, but is protected by mutex) */
pthread_mutex_t users_mutex;
std::vector<ConnectedUser*> users;

/* specifies whether all websockets must close (1) or not (0) */
volatile int disconnect_all = 0;

/**
 * Change socket to blocking.
 *
 * @param fd the socket to manipulate
 */
static void make_blocking (MHD_socket fd)
{
  std::cout<<"make_blocking"<<std::endl;

#if defined(MHD_POSIX_SOCKETS)
  int flags;

  flags = fcntl (fd, F_GETFL);
  if (-1 == flags)
    abort ();
  if ((flags & ~O_NONBLOCK) != flags)
    if (-1 == fcntl (fd, F_SETFL, flags & ~O_NONBLOCK))
      abort ();
#elif defined(MHD_WINSOCK_SOCKETS)
  unsigned long flags = 0;

  if (0 != ioctlsocket (fd, (int) FIONBIO, &flags))
    abort ();
#endif /* MHD_WINSOCK_SOCKETS */
}


/**
 * Sends all data of the given buffer via the TCP/IP socket
 *
 * @param fd  The TCP/IP socket which is used for sending
 * @param buf The buffer with the data to send
 * @param len The length in bytes of the data in the buffer
 */
static void send_all(struct ConnectedUser *cu, const char *buf, size_t len)
{
  //std::cout<<"send_all"<<std::endl;

  ssize_t ret;
  size_t off;

  if (0 != pthread_mutex_lock (&cu->send_mutex))
    abort ();
  for (off = 0; off < len; off += ret) {
    ret = send(cu->fd, &buf[off], (int)(len - off), 0);
    if (0 > ret) {
      if (EAGAIN == errno) {
        ret = 0;
        continue;
      }
      break;
    }
    if (0 == ret) {
      break;
    }
  }
  if (0 != pthread_mutex_unlock (&cu->send_mutex))
    abort ();
}


/**
 * Adds a new user to the global user list.
 * This will be called at the start of WebSocketClientReceiveThreadFunction.
 *
 * @param cu The connected user
 * @return   0 on success, other values on error
 */
static int users_adduser(struct ConnectedUser* cu)
{
  std::cout<<"users_adduser"<<std::endl;

  /* lock the mutex */
  if (0 != pthread_mutex_lock(&users_mutex))
    abort ();

  // Add the new user to the list
  users.push_back(cu);

  /* unlock the mutex */
  if (0 != pthread_mutex_unlock(&users_mutex))
    abort ();
  return 0;
}


/**
 * Removes a user from the global user list.
 *
 * @param cu The connected user
 */
void users_removeuser(struct ConnectedUser* cu)
{
  std::cout<<"users_removeuser"<<std::endl;

  /* lock the mutex */
  if (0 != pthread_mutex_lock(&users_mutex))
    abort ();

  // Remove the user from the list
  std::remove(users.begin(), users.end(), cu);

  /* unlock the mutex */
  if (0 != pthread_mutex_unlock(&users_mutex))
    abort ();
}


/**
* Parses received data from the TCP/IP socket with the websocket stream
*
* @param cu           The connected user
* @param new_name     The new user name
* @param new_name_len The length of the new name
* @return             0 on success, other values on error
*/
static int connecteduser_parse_received_websocket_stream (struct ConnectedUser *cu, char *buf, size_t buf_len)
{
  std::cout<<"connecteduser_parse_received_websocket_stream"<<std::endl;

  size_t buf_offset = 0;
  while (buf_offset < buf_len) {
    size_t new_offset = 0;
    char *frame_data = nullptr;
    size_t frame_len  = 0;
    int status = MHD_websocket_decode(cu->ws,
                                       buf + buf_offset,
                                       buf_len - buf_offset,
                                       &new_offset,
                                       &frame_data,
                                       &frame_len);
    if (0 > status) {
      /* an error occurred and the connection must be closed */
      if (nullptr != frame_data) {
        /* depending on the WebSocket flag */
        /* MHD_WEBSOCKET_FLAG_GENERATE_CLOSE_FRAMES_ON_ERROR */
        /* close frames might be generated on errors */
        send_all(cu, frame_data, frame_len);
        MHD_websocket_free(cu->ws, frame_data);
      }
      return 1;
    } else {
      buf_offset += new_offset;

      if (0 < status) {
        /* the frame is complete */
        switch (status) {
        case MHD_WEBSOCKET_STATUS_CLOSE_FRAME:
          /* if we receive a close frame, we will respond with one */
          MHD_websocket_free(cu->ws, frame_data);
          {
            char *result = nullptr;
            size_t result_len = 0;
            int er = MHD_websocket_encode_close(cu->ws,
                                                 MHD_WEBSOCKET_CLOSEREASON_REGULAR,
                                                 nullptr,
                                                 0,
                                                 &result,
                                                 &result_len);
            if (MHD_WEBSOCKET_STATUS_OK == er) {
              send_all(cu, result, result_len);
              MHD_websocket_free(cu->ws, result);
            }
          }
          return 1;

        default:
          /* This case should really never happen, */
          /* because there are only five types of (finished) websocket frames. */
          /* If it is ever reached, it means that there is memory corruption. */
          MHD_websocket_free(cu->ws, frame_data);
          return 1;
        }
      }
    }
  }

  return 0;
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
static void* WebSocketClientSendThreadFunction(void* cls)
{
  std::cout<<"WebSocketClientSendThreadFunction"<<std::endl;

  struct ConnectedUser* cu = (ConnectedUser*)cls;

  /* the main loop of sending messages requires to lock the mutex */
  if (0 != pthread_mutex_lock(&users_mutex))
    abort ();
  while (true) {
    /* loop while not all messages processed */
    bool all_messages_read = false;
    while (!all_messages_read) {
      if (1 == disconnect_all) {
        // The application is closing so we need to disconnect all users
        struct MHD_UpgradeResponseHandle* urh = cu->urh;
        if (nullptr != urh) {
          /* Close the TCP/IP socket. */
          /* This will also wake-up the waiting receive-thread for this connected user. */
          cu->urh = nullptr;
          MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        }
        if (0 != pthread_mutex_unlock(&users_mutex))
          abort ();
        return nullptr;
      } else if (1 == cu->disconnect) {
        /* The sender thread shall close. */
        /* This is only requested by the receive thread, so we can just leave. */
        if (0 != pthread_mutex_unlock(&users_mutex))
          abort ();
        return nullptr;
      } else {
        all_messages_read = true;
      }
    }

    /* Wait for wake up. */
    /* This will automatically unlock the mutex while waiting and */
    /* lock the mutex after waiting */
    pthread_cond_wait(&cu->wake_up_sender, &users_mutex);
  }

  std::cout<<"WebSocketClientSendThreadFunction returning"<<std::endl;
  return nullptr;
}


/**
 * Receives messages from the TCP/IP socket and
 * initializes the connected user.
 *
 * @param cls The connected user
 * @return    Always nullptr
 */
static void* WebSocketClientReceiveThreadFunction(void* cls)
{
  std::cout<<"WebSocketClientReceiveThreadFunction"<<std::endl;
  struct ConnectedUser *cu = (ConnectedUser*)cls;

  /* make the socket blocking */
  make_blocking(cu->fd);

  /* initialize the wake-up-sender condition variable */
  if (0 != pthread_cond_init(&cu->wake_up_sender, nullptr)) {
    MHD_upgrade_action(cu->urh, MHD_UPGRADE_ACTION_CLOSE);
    free(cu->extra_in);
    free(cu);
    return nullptr;
  }

  /* initialize the send mutex */
  if (0 != pthread_mutex_init(&cu->send_mutex, nullptr)) {
    MHD_upgrade_action(cu->urh, MHD_UPGRADE_ACTION_CLOSE);
    pthread_cond_destroy(&cu->wake_up_sender);
    free(cu->extra_in);
    free(cu);
    return nullptr;
  }

  /* add the user to the user list */
  users_adduser(cu);

  /* initialize the web socket stream for encoding/decoding */
  int result = MHD_websocket_stream_init(&cu->ws, MHD_WEBSOCKET_FLAG_SERVER | MHD_WEBSOCKET_FLAG_NO_FRAGMENTS, 0);
  if (MHD_WEBSOCKET_STATUS_OK != result) {
    users_removeuser(cu);
    pthread_cond_destroy(&cu->wake_up_sender);
    pthread_mutex_destroy(&cu->send_mutex);
    MHD_upgrade_action(cu->urh, MHD_UPGRADE_ACTION_CLOSE);
    delete[] cu->extra_in;
    delete cu;
    return nullptr;
  }


  /* start the message-send thread */
  pthread_t pt;
  if (0 != pthread_create (&pt, nullptr, &WebSocketClientSendThreadFunction, cu))
    abort ();

  /* start by parsing extra data MHD may have already read, if any */
  if (0 != cu->extra_in_size) {
    if (0 != connecteduser_parse_received_websocket_stream(cu, cu->extra_in, cu->extra_in_size)) {
      users_removeuser(cu);
      if (0 != pthread_mutex_lock(&users_mutex))
        abort ();
      cu->disconnect = 1;
      pthread_cond_signal(&cu->wake_up_sender);
      if (0 != pthread_mutex_unlock(&users_mutex))
        abort ();
      pthread_join(pt, nullptr);

      struct MHD_UpgradeResponseHandle *urh = cu->urh;
      if (nullptr != urh) {
        cu->urh = nullptr;
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
      }
      pthread_cond_destroy(&cu->wake_up_sender);
      pthread_mutex_destroy(&cu->send_mutex);
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
  while (1) {
    got = recv (cu->fd, buf, sizeof (buf), 0);
    if (0 >= got) {
      /* the TCP/IP socket has been closed */
      break;
    }
    if (0 < got) {
      if (0 != connecteduser_parse_received_websocket_stream(cu, buf, (size_t) got)) {
        /* A websocket protocol error occurred */
        users_removeuser(cu);
        if (0 != pthread_mutex_lock(&users_mutex))
          abort();
        cu->disconnect = 1;
        pthread_cond_signal(&cu->wake_up_sender);
        if (0 != pthread_mutex_unlock(&users_mutex))
          abort ();
        pthread_join(pt, nullptr);
        struct MHD_UpgradeResponseHandle* urh = cu->urh;
        if (nullptr != urh) {
          cu->urh = nullptr;
          MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        }
        pthread_cond_destroy(&cu->wake_up_sender);
        pthread_mutex_destroy(&cu->send_mutex);
        MHD_websocket_stream_free(cu->ws);
        delete cu;
        return nullptr;
      }
    }
  }

  /* cleanup */
  users_removeuser(cu);
  if (0 != pthread_mutex_lock(&users_mutex))
    abort();
  cu->disconnect = 1;
  pthread_cond_signal(&cu->wake_up_sender);
  if (0 != pthread_mutex_unlock(&users_mutex))
    abort();
  pthread_join(pt, nullptr);
  struct MHD_UpgradeResponseHandle *urh = cu->urh;
  if (nullptr != urh) {
    cu->urh = nullptr;
    MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
  }
  pthread_cond_destroy(&cu->wake_up_sender);
  pthread_mutex_destroy(&cu->send_mutex);
  MHD_websocket_stream_free(cu->ws);
  delete cu;

  return nullptr;
}




namespace acdisplay {

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
        const int result = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return (result == MHD_YES);
      }
    }
  }

  return false;
}



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
  cu->user_id = ++unique_user_id;

  /* create thread for the new connected user */
  pthread_t pt;
  if (0 != pthread_create(&pt, nullptr, &WebSocketClientReceiveThreadFunction, cu))
    abort();
  pthread_detach(pt);
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

    char is_valid = 1;
    const char *value = nullptr;
    char sec_websocket_accept[29] = { 0 };

    /* check whether an websocket upgrade is requested */
    if (0 != MHD_websocket_check_http_version(version.data())) {
      is_valid = 0;
    }
    value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONNECTION);
    if (0 != MHD_websocket_check_connection_header(value)) {
      is_valid = 0;
    }
    value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_UPGRADE);
    if (0 != MHD_websocket_check_upgrade_header(value)) {
      is_valid = 0;
    }
    value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_SEC_WEBSOCKET_VERSION);
    if (0 != MHD_websocket_check_version_header(value)) {
      is_valid = 0;
    }
    value = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_SEC_WEBSOCKET_KEY);
    if (0 != MHD_websocket_create_accept_header(value, sec_websocket_accept)) {
      is_valid = 0;
    }

    if (1 == is_valid) {
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

void SendWebSocketUpdate()
{
  // Get the shared rpm value
  mutex_ac_data.lock();
  const cACData copy = ac_data;
  mutex_ac_data.unlock();

  // Create our car info update
  const std::string update = "car_info|" +
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

  char* frame_data = nullptr;
  size_t frame_len = 0;

  // Send the update to each connection
  // HACK: This is not quick because we call send on the socket inside the lock
  if (0 != pthread_mutex_lock(&users_mutex))
    abort ();

  for (auto&& cu : users) {
    const int status = MHD_websocket_encode_text(
      cu->ws,
      update.c_str(), update.length(),
      MHD_WEBSOCKET_FRAGMENTATION_NONE,
      &frame_data, &frame_len,
      nullptr);
    if (MHD_WEBSOCKET_STATUS_OK == status) {
      send_all(cu, frame_data, frame_len);

      // Free the frame data
      MHD_websocket_free(cu->ws, frame_data);
    }
  }

  if (0 != pthread_mutex_unlock(&users_mutex))
    abort ();
}


namespace acdisplay {

class cWebServer {
public:
  explicit cWebServer(cStaticResourcesRequestHandler& static_resources_request_handler, cWebSocketRequestHandler& web_socket_request_handler);
  ~cWebServer();

  bool Open(const util::cIPAddress& host, uint16_t port, const std::string& private_key, const std::string& public_cert);
  bool Close();

private:
  static enum MHD_Result _OnRequest(
    void *cls,
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **req_cls
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
                          MHD_OPTION_END);
  }

  return (daemon != nullptr);
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
  void *cls,
  struct MHD_Connection *connection,
  const char *url,
  const char *method,
  const char *version,
  const char *upload_data,
  size_t *upload_data_size,
  void **req_cls
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
  struct MHD_Response* response = MHD_create_response_from_buffer_static(PAGE_NOT_FOUND.length(), PAGE_NOT_FOUND.c_str());
  enum MHD_Result result = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
  MHD_destroy_response(response);

  return result;
}

bool RunWebServer(const util::cIPAddress& host, uint16_t port, const std::string& private_key, const std::string& public_cert)
{
  cStaticResourcesRequestHandler static_resources_request_handler;
  cWebSocketRequestHandler web_socket_request_handler;

  // Load the static resources
  if (!static_resources_request_handler.LoadStaticResources()) {
    return false;
  }


  if (0 != pthread_mutex_init(&users_mutex, nullptr))
    return 1;

  cWebServer webserver(static_resources_request_handler, web_socket_request_handler);
  if (!webserver.Open(host, port, private_key, public_cert)) {
    std::cerr<<"Error opening web server"<<std::endl;
    return false;
  }

  std::cout<<"Server is running"<<std::endl;

  while (true) {
    SendWebSocketUpdate();

    util::msleep(50);
  }

  std::cout<<"Press enter to shutdown the server"<<std::endl;
  (void) getc (stdin);
  std::cout<<"Shutting down the server"<<std::endl;

  // Tell each connection to wake up and disconnect
  if (0 != pthread_mutex_lock(&users_mutex))
    abort ();
  disconnect_all = 1;
  for (auto&& cu : users)
    pthread_cond_signal(&cu->wake_up_sender);
  if (0 != pthread_mutex_unlock(&users_mutex))
    abort ();

  // Wait for the connection threads to respond
  std::cout<<"Waiting for the connection threads to respond"<<std::endl;
  sleep (2);

  // Tell each connection to close
  std::cout<<"Closing connections"<<std::endl;
  if (0 != pthread_mutex_lock(&users_mutex))
    abort ();
  for (auto&& cu : users) {
    struct MHD_UpgradeResponseHandle* urh = cu->urh;
    if (nullptr != urh) {
      cu->urh = nullptr;
      MHD_upgrade_action(cu->urh, MHD_UPGRADE_ACTION_CLOSE);
    }
  }
  if (0 != pthread_mutex_unlock(&users_mutex))
    abort ();

  // Wait for the connection threads to close
  sleep (2);

  /* usually we should wait here in a safe way for all threads to disconnect, */
  /* but we skip this in the example */

  pthread_mutex_destroy(&users_mutex);

  webserver.Close();

  return false;
}

}
