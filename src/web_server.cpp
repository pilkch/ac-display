#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
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
  enum MHD_Result OnRequest(
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **req_cls
  );

  bool LoadStaticResources();

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
 * This struct is used to keep the data of a connected chat user.
 * It is passed to the socket-receive thread (WebSocketClientReceiveThreadFunction) as well as to
 * the socket-send thread (WebSocketClientSendThreadFunction).
 * It can also be accessed via the global array users (mutex protected).
 */
struct ConnectedUser
{
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
  /* the current user name */
  char *user_name;
  size_t user_name_len;
  /* the zero-based index of the next message;
     may be decremented when old messages are deleted */
  size_t next_message_index;
  /* specifies whether the websocket shall be closed (1) or not (0) */
  int disconnect;
  /* condition variable to wake up the sender of this connection */
  pthread_cond_t wake_up_sender;
  /* mutex to ensure that no send actions are mixed
     (sending can be done by send and recv thread;
      may not be simultaneously locked with chat_mutex by the same thread) */
  pthread_mutex_t send_mutex;
};

/**
 * A single message, which has been send via the chat.
 * This can be text, an image or a command.
 */
struct Message
{
  /* The user id of the sender. This is 0 if it is a system message- */
  size_t from_user_id;
  /* The user id of the recipient. This is 0 if every connected user shall receive it */
  size_t to_user_id;
  /* The data of the message. */
  char *data;
  size_t data_len;
  /* Specifies whether the data is UTF-8 encoded text (0) or binary data (1) */
  int is_binary;
};

/* the unique user counter for new users (only accessed by main thread) */
size_t unique_user_id = 0;

/* the chat data (users and messages; may be accessed by all threads, but is protected by mutex) */
pthread_mutex_t chat_mutex;
struct ConnectedUser **users = nullptr;
size_t user_count = 0;
struct Message **messages = nullptr;
size_t message_count = 0;
/* specifies whether all websockets must close (1) or not (0) */
volatile int disconnect_all = 0;
/* a counter for cleaning old messages (each 10 messages we will try to clean the list */
int clean_count = 0;
#define CLEANUP_LIMIT 10

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
static void
send_all (struct ConnectedUser *cu,
          const char *buf,
          size_t len)
{
  //std::cout<<"send_all"<<std::endl;

  ssize_t ret;
  size_t off;

  if (0 != pthread_mutex_lock (&cu->send_mutex))
    abort ();
  for (off = 0; off < len; off += ret)
  {
    ret = send (cu->fd,
                &buf[off],
                (int) (len - off),
                0);
    if (0 > ret)
    {
      if (EAGAIN == errno)
      {
        ret = 0;
        continue;
      }
      break;
    }
    if (0 == ret)
      break;
  }
  if (0 != pthread_mutex_unlock (&cu->send_mutex))
    abort ();
}


/**
 * Adds a new chat message to the list of messages.
 *
 * @param from_user_id the user id of the sender (0 means system)
 * @param to_user_id   the user id of the recipiend (0 means everyone)
 * @param data         the data to send (UTF-8 text or binary; will be copied)
 * @param data_len     the length of the data to send
 * @param is_binary    specifies whether the data is UTF-8 text (0) or binary (1)
 * @param needs_lock   specifies whether the caller has already locked the global chat mutex (0) or
 *                     if this procedure needs to lock it (1)
 *
 * @return             0 on success, other values on error
 */
static int
chat_addmessage (size_t from_user_id,
                 size_t to_user_id,
                 const char *data,
                 size_t data_len,
                 int is_binary,
                 int needs_lock)
{
  std::cout<<"chat_addmessage"<<std::endl;

  /* allocate the buffer and fill it with data */
  struct Message *message = (struct Message *) malloc (sizeof (struct Message));
  if (nullptr == message)
    return 1;

  memset (message, 0, sizeof (struct Message));
  message->from_user_id = from_user_id;
  message->to_user_id   = to_user_id;
  message->is_binary    = is_binary;
  message->data_len     = data_len;
  message->data = (char*)malloc (data_len + 1);
  if (nullptr == message->data) {
    free (message);
    return 1;
  }
  memcpy (message->data, data, data_len);
  message->data[data_len] = 0;

  /* lock the global mutex if needed */
  if (0 != needs_lock) {
    if (0 != pthread_mutex_lock (&chat_mutex))
      abort ();
  }

  /* add the new message to the global message list */
  size_t message_count_ = message_count + 1;
  struct Message **messages_ = (struct Message **)realloc(messages, message_count_ * sizeof (struct Message *));
  if (nullptr == messages_) {
    free (message);
    if (0 != needs_lock)
      if (0 != pthread_mutex_unlock (&chat_mutex))
        abort ();
    return 1;
  }
  messages_[message_count] = message;
  messages = messages_;
  message_count = message_count_;

  /* inform the sender threads about the new message */
  for (size_t i = 0; i < user_count; ++i)
    pthread_cond_signal (&users[i]->wake_up_sender);

  /* unlock the global mutex if needed */
  if (0 != needs_lock) {
    if (0 != needs_lock)
      if (0 != pthread_mutex_unlock (&chat_mutex))
        abort ();
  }
  return 0;
}


/**
 * Adds a new chat user to the global user list.
 * This will be called at the start of WebSocketClientReceiveThreadFunction.
 *
 * @param cu The connected user
 * @return   0 on success, other values on error
 */
static int
chat_adduser (struct ConnectedUser *cu)
{
  std::cout<<"chat_adduser"<<std::endl;

  /* lock the mutex */
  if (0 != pthread_mutex_lock (&chat_mutex))
    abort ();

  /* add the new user to the list */
  size_t user_count_ = user_count + 1;
  struct ConnectedUser **users_ = (struct ConnectedUser **) realloc (users, user_count_ * sizeof (struct ConnectedUser *));
  if (nullptr == users_) {
    /* realloc failed */
    if (0 != pthread_mutex_unlock (&chat_mutex))
      abort ();
    return 1;
  }
  users_[user_count] = cu;
  users      = users_;
  user_count = user_count_;

  /* Initialize the next message index to the current message count. */
  /* This will skip all old messages for this new connected user. */
  cu->next_message_index = message_count;

  /* unlock the mutex */
  if (0 != pthread_mutex_unlock (&chat_mutex))
    abort ();
  return 0;
}


/**
 * Removes a chat user from the global user list.
 *
 * @param cu The connected user
 * @return   0 on success, other values on error
 */
static int
chat_removeuser (struct ConnectedUser *cu)
{
  std::cout<<"chat_removeuser"<<std::endl;

  /* lock the mutex */
  if (0 != pthread_mutex_lock (&chat_mutex))
    abort ();
  /* inform the other chat users that the user is gone */
  int got_error = 0;

  /* remove the user from the list */
  int found = 0;
  for (size_t i = 0; i < user_count; ++i) {
    if (cu == users[i]) {
      found = 1;
      for (size_t j = i + 1; j < user_count; ++j) {
        users[j - 1] = users[j];
      }
      --user_count;
      break;
    }
  }
  if (0 == found)
    got_error = 1;

  /* unlock the mutex */
  if (0 != pthread_mutex_unlock (&chat_mutex))
    abort ();

  return got_error;
}


/**
 * Renames a chat user
 *
 * @param cu           The connected user
 * @param new_name     The new user name. On success this pointer will be taken.
 * @param new_name_len The length of the new name
 * @return             0 on success, other values on error. 2 means name already in use.
 */
static int chat_renameuser (struct ConnectedUser *cu, char *new_name, size_t new_name_len)
{
  std::cout<<"chat_renameuser"<<std::endl;

  /* lock the mutex */
  if (0 != pthread_mutex_lock (&chat_mutex))
    abort ();

  /* check whether the name is already in use */
  for (size_t i = 0; i < user_count; ++i) {
    if (cu != users[i]) {
      if (
        (users[i]->user_name_len == new_name_len) &&
        (0 == strcasecmp (users[i]->user_name, new_name))
      ) {
        if (0 != pthread_mutex_unlock (&chat_mutex))
          abort ();
        return 2;
      }
    }
  }

  /* generate the notification message */
  char user_index[32];
  snprintf (user_index, 32, "%d", (int) cu->user_id);
  size_t user_index_len = strlen (user_index);
  size_t data_len = user_index_len + new_name_len + 10;
  char *data = (char *) malloc (data_len + 1);
  if (nullptr == data)
    return 1;
  strcpy (data, "username|");
  strcat (data, user_index);
  strcat (data, "|");
  strcat (data, new_name);

  /* inform the other chat users about the new name */
  if (0 != chat_addmessage (0, 0, data, data_len, 0, 0)) {
    free (data);
    if (0 != pthread_mutex_unlock (&chat_mutex))
      abort ();
    return 1;
  }
  free (data);

  /* accept the new user name */
  free (cu->user_name);
  cu->user_name = new_name;
  cu->user_name_len = new_name_len;

  /* unlock the mutex */
  if (0 != pthread_mutex_unlock (&chat_mutex))
    abort ();

  return 0;
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
  while (buf_offset < buf_len)
  {
    size_t new_offset = 0;
    char *frame_data = nullptr;
    size_t frame_len  = 0;
    int status = MHD_websocket_decode (cu->ws,
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
        send_all (cu, frame_data, frame_len);
        MHD_websocket_free (cu->ws, frame_data);
      }
      return 1;
    } else {
      buf_offset += new_offset;

      if (0 < status) {
        /* the frame is complete */
        switch (status) {
        case MHD_WEBSOCKET_STATUS_CLOSE_FRAME:
          /* if we receive a close frame, we will respond with one */
          MHD_websocket_free (cu->ws, frame_data);
          {
            char *result = nullptr;
            size_t result_len = 0;
            int er = MHD_websocket_encode_close (cu->ws,
                                                 MHD_WEBSOCKET_CLOSEREASON_REGULAR,
                                                 nullptr,
                                                 0,
                                                 &result,
                                                 &result_len);
            if (MHD_WEBSOCKET_STATUS_OK == er) {
              send_all (cu, result, result_len);
              MHD_websocket_free (cu->ws, result);
            }
          }
          return 1;

        default:
          /* This case should really never happen, */
          /* because there are only five types of (finished) websocket frames. */
          /* If it is ever reached, it means that there is memory corruption. */
          MHD_websocket_free (cu->ws, frame_data);
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

  struct ConnectedUser *cu = (ConnectedUser*)cls;

  /* the main loop of sending messages requires to lock the mutex */
  if (0 != pthread_mutex_lock (&chat_mutex))
    abort ();
  for (;;) {
    /* loop while not all messages processed */
    int all_messages_read = 0;
    while (0 == all_messages_read) {
      if (1 == disconnect_all) {
        /* the application closes and want that we disconnect all users */
        struct MHD_UpgradeResponseHandle *urh = cu->urh;
        if (nullptr != urh) {
          /* Close the TCP/IP socket. */
          /* This will also wake-up the waiting receive-thread for this connected user. */
          cu->urh = nullptr;
          MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        }
        if (0 != pthread_mutex_unlock (&chat_mutex))
          abort ();
        return nullptr;
      } else if (1 == cu->disconnect) {
        /* The sender thread shall close. */
        /* This is only requested by the receive thread, so we can just leave. */
        if (0 != pthread_mutex_unlock (&chat_mutex))
          abort ();
        return nullptr;
      } else if (cu->next_message_index < message_count) {
        /* a chat message or command is pending */
        char *frame_data = nullptr;
        size_t frame_len = 0;
        int er = 0;
        {
          struct Message *msg = messages[cu->next_message_index];
          if (
            (0 == msg->to_user_id) ||
            (cu->user_id == msg->to_user_id) ||
            (cu->user_id == msg->from_user_id)
          ) {
            if (0 == msg->is_binary) {
              er = MHD_websocket_encode_text (cu->ws,
                                              msg->data,
                                              msg->data_len,
                                              MHD_WEBSOCKET_FRAGMENTATION_NONE,
                                              &frame_data,
                                              &frame_len,
                                              nullptr);
            } else {
              er = MHD_websocket_encode_binary (cu->ws,
                                                msg->data,
                                                msg->data_len,
                                                MHD_WEBSOCKET_FRAGMENTATION_NONE,
                                                &frame_data,
                                                &frame_len);
            }
          }
        }
        ++cu->next_message_index;

        /* send the data via the TCP/IP socket and */
        /* unlock the mutex while sending */
        if (0 != pthread_mutex_unlock (&chat_mutex))
          abort ();
        if (MHD_WEBSOCKET_STATUS_OK == er) {
          send_all (cu, frame_data, frame_len);
        }
        MHD_websocket_free (cu->ws, frame_data);
        if (0 != pthread_mutex_lock (&chat_mutex))
          abort ();
        /* check whether there are still pending messages */
        all_messages_read = (cu->next_message_index < message_count) ? 0 : 1;
      } else {
        all_messages_read = 1;
      }
    }

    /* Wait for wake up. */
    /* This will automatically unlock the mutex while waiting and */
    /* lock the mutex after waiting */
    pthread_cond_wait (&cu->wake_up_sender, &chat_mutex);
  }

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
  char buf[128];
  ssize_t got;
  int result;

  /* make the socket blocking */
  make_blocking (cu->fd);

  /* generate the user name */
  {
    char user_name[32];
    strcpy (user_name, "User");
    snprintf (user_name + 4, 28, "%d", (int) cu->user_id);
    cu->user_name_len = strlen (user_name);
    cu->user_name = (char*)malloc (cu->user_name_len + 1);
    if (nullptr == cu->user_name) {
      free (cu->extra_in);
      free (cu);
      MHD_upgrade_action (cu->urh, MHD_UPGRADE_ACTION_CLOSE);
      return nullptr;
    }
    strcpy (cu->user_name, user_name);
  }

  /* initialize the wake-up-sender condition variable */
  if (0 != pthread_cond_init (&cu->wake_up_sender, nullptr)) {
    MHD_upgrade_action (cu->urh, MHD_UPGRADE_ACTION_CLOSE);
    free (cu->user_name);
    free (cu->extra_in);
    free (cu);
    return nullptr;
  }

  /* initialize the send mutex */
  if (0 != pthread_mutex_init (&cu->send_mutex, nullptr)) {
    MHD_upgrade_action (cu->urh, MHD_UPGRADE_ACTION_CLOSE);
    pthread_cond_destroy (&cu->wake_up_sender);
    free (cu->user_name);
    free (cu->extra_in);
    free (cu);
    return nullptr;
  }

  /* add the user to the chat user list */
  chat_adduser (cu);

  /* initialize the web socket stream for encoding/decoding */
  result = MHD_websocket_stream_init (&cu->ws,
                                      MHD_WEBSOCKET_FLAG_SERVER
                                      | MHD_WEBSOCKET_FLAG_NO_FRAGMENTS,
                                      0);
  if (MHD_WEBSOCKET_STATUS_OK != result) {
    chat_removeuser (cu);
    pthread_cond_destroy (&cu->wake_up_sender);
    pthread_mutex_destroy (&cu->send_mutex);
    MHD_upgrade_action (cu->urh,
                        MHD_UPGRADE_ACTION_CLOSE);
    free (cu->user_name);
    free (cu->extra_in);
    free (cu);
    return nullptr;
  }

  /* send a list of all currently connected users (bypassing the messaging system) */
  {
    struct UserInit
    {
      char *user_init;
      size_t user_init_len;
    };
    struct UserInit *init_users = nullptr;
    size_t init_users_len = 0;

    /* first collect all users without sending (so the mutex isn't locked too long) */
    if (0 != pthread_mutex_lock (&chat_mutex))
      abort ();
    if (0 < user_count) {
      init_users = (struct UserInit *) malloc (user_count * sizeof (struct UserInit));
      if (nullptr != init_users) {
        init_users_len = user_count;
        for (size_t i = 0; i < user_count; ++i) {
          char user_index[32];
          snprintf (user_index, 32, "%d", (int) users[i]->user_id);
          size_t user_index_len = strlen (user_index);
          struct UserInit iu;
          iu.user_init_len = user_index_len + users[i]->user_name_len + 10;
          iu.user_init = (char *) malloc (iu.user_init_len + 1);
          if (nullptr != iu.user_init)
          {
            strcpy (iu.user_init, "userinit|");
            strcat (iu.user_init, user_index);
            strcat (iu.user_init, "|");
            if (0 < users[i]->user_name_len)
              strcat (iu.user_init, users[i]->user_name);
          }
          init_users[i] = iu;
        }
      }
    }
    if (0 != pthread_mutex_unlock (&chat_mutex))
      abort ();

    /* then send all users to the connected client */
    for (size_t i = 0; i < init_users_len; ++i) {
      char *frame_data = nullptr;
      size_t frame_len = 0;
      if ((0 < init_users[i].user_init_len) && (nullptr != init_users[i].user_init) ) {
        int status = MHD_websocket_encode_text (cu->ws,
                                                init_users[i].user_init,
                                                init_users[i].user_init_len,
                                                MHD_WEBSOCKET_FRAGMENTATION_NONE,
                                                &frame_data,
                                                &frame_len,
                                                nullptr);
        if (MHD_WEBSOCKET_STATUS_OK == status)
        {
          send_all (cu, frame_data, frame_len);
          MHD_websocket_free (cu->ws, frame_data);
        }
        free (init_users[i].user_init);
      }
    }
    free (init_users);
  }


  /* start the message-send thread */
  pthread_t pt;
  if (0 != pthread_create (&pt, nullptr, &WebSocketClientSendThreadFunction, cu))
    abort ();

  /* start by parsing extra data MHD may have already read, if any */
  if (0 != cu->extra_in_size) {
    if (0 != connecteduser_parse_received_websocket_stream (cu, cu->extra_in, cu->extra_in_size)) {
      chat_removeuser (cu);
      if (0 != pthread_mutex_lock (&chat_mutex))
        abort ();
      cu->disconnect = 1;
      pthread_cond_signal (&cu->wake_up_sender);
      if (0 != pthread_mutex_unlock (&chat_mutex))
        abort ();
      pthread_join (pt, nullptr);

      struct MHD_UpgradeResponseHandle *urh = cu->urh;
      if (nullptr != urh) {
        cu->urh = nullptr;
        MHD_upgrade_action (urh, MHD_UPGRADE_ACTION_CLOSE);
      }
      pthread_cond_destroy (&cu->wake_up_sender);
      pthread_mutex_destroy (&cu->send_mutex);
      MHD_websocket_stream_free (cu->ws);
      free (cu->user_name);
      free (cu->extra_in);
      free (cu);
      return nullptr;
    }
    free (cu->extra_in);
    cu->extra_in = nullptr;
  }

  /* the main loop for receiving data */
  while (1) {
    got = recv (cu->fd,
                buf,
                sizeof (buf),
                0);
    if (0 >= got) {
      /* the TCP/IP socket has been closed */
      break;
    }
    if (0 < got) {
      if (0 != connecteduser_parse_received_websocket_stream (cu, buf, (size_t) got)) {
        /* A websocket protocol error occurred */
        chat_removeuser (cu);
        if (0 != pthread_mutex_lock (&chat_mutex))
          abort ();
        cu->disconnect = 1;
        pthread_cond_signal (&cu->wake_up_sender);
        if (0 != pthread_mutex_unlock (&chat_mutex))
          abort ();
        pthread_join (pt, nullptr);
        struct MHD_UpgradeResponseHandle *urh = cu->urh;
        if (nullptr != urh)
        {
          cu->urh = nullptr;
          MHD_upgrade_action (urh, MHD_UPGRADE_ACTION_CLOSE);
        }
        pthread_cond_destroy (&cu->wake_up_sender);
        pthread_mutex_destroy (&cu->send_mutex);
        MHD_websocket_stream_free (cu->ws);
        free (cu->user_name);
        free (cu);
        return nullptr;
      }
    }
  }

  /* cleanup */
  chat_removeuser (cu);
  if (0 != pthread_mutex_lock (&chat_mutex))
    abort ();
  cu->disconnect = 1;
  pthread_cond_signal (&cu->wake_up_sender);
  if (0 != pthread_mutex_unlock (&chat_mutex))
    abort ();
  pthread_join (pt, nullptr);
  struct MHD_UpgradeResponseHandle *urh = cu->urh;
  if (nullptr != urh) {
    cu->urh = nullptr;
    MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
  }
  pthread_cond_destroy (&cu->wake_up_sender);
  pthread_mutex_destroy (&cu->send_mutex);
  MHD_websocket_stream_free (cu->ws);
  free (cu->user_name);
  free (cu);

  return nullptr;
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
static void
upgrade_handler (void *cls,
                 struct MHD_Connection *connection,
                 void *req_cls,
                 const char *extra_in,
                 size_t extra_in_size,
                 MHD_socket fd,
                 struct MHD_UpgradeResponseHandle *urh)
{
  std::cout<<"upgrade_handler"<<std::endl;
  struct ConnectedUser *cu;
  pthread_t pt;
  (void) cls;         /* Unused. Silent compiler warning. */
  (void) connection;  /* Unused. Silent compiler warning. */
  (void) req_cls;     /* Unused. Silent compiler warning. */

  /* This callback must return as soon as possible. */

  /* allocate new connected user */
  cu = (ConnectedUser*)malloc (sizeof (struct ConnectedUser));
  if (nullptr == cu)
    abort ();
  memset (cu, 0, sizeof (struct ConnectedUser));
  if (0 != extra_in_size) {
    cu->extra_in = (char*)malloc (extra_in_size);
    if (nullptr == cu->extra_in)
      abort ();
    memcpy (cu->extra_in, extra_in, extra_in_size);
  }
  cu->extra_in_size = extra_in_size;
  cu->fd = fd;
  cu->urh = urh;
  cu->user_id = ++unique_user_id;
  cu->user_name = nullptr;
  cu->user_name_len = 0;

  /* create thread for the new connected user */
  if (0 != pthread_create(&pt, nullptr, &WebSocketClientReceiveThreadFunction, cu))
    abort ();
  pthread_detach (pt);
}


namespace acdisplay {

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
 enum MHD_Result cStaticResourcesRequestHandler::OnRequest(
  struct MHD_Connection *connection,
  const char *url,
  const char *method,
  const char *version,
  const char *upload_data,
  size_t *upload_data_size,
  void **req_cls
)
{
  std::cout<<"cStaticResourcesRequestHandler::OnRequest "<<url<<std::endl;
  static int aptr;
  struct MHD_Response *response;
  int ret;
  (void) version;           /* Unused. Silent compiler warning. */
  (void) upload_data;       /* Unused. Silent compiler warning. */
  (void) upload_data_size;  /* Unused. Silent compiler warning. */

  if (0 != strcmp (method, "GET"))
    return MHD_NO;              /* unexpected method */
  if (&aptr != *req_cls) {
    /* do never respond on first call */
    *req_cls = &aptr;
    return MHD_YES;
  }
  *req_cls = nullptr;                  /* reset when done */

  bool handled = false;

  // Handle static resources
  if (url[0] == '/') {
    for (auto&& resource : static_resources) {
      if (url == resource.request_path) {
        // This is the requested resource so create a response
        struct MHD_Response* response = MHD_create_response_from_buffer_static(resource.response_text.length(), resource.response_text.c_str());
        MHD_add_response_header(response, "Content-Type", resource.response_mime_type.c_str());
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);

        handled = true;

        break;
      }
    }
  }

  if (!handled) {
  if (0 == strcmp (url, "/ACDisplayServerWebSocket")) {
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
    char sec_websocket_accept[29];

    /* check whether an websocket upgrade is requested */
    if (0 != MHD_websocket_check_http_version (version))
    {
      is_valid = 0;
    }
    value = MHD_lookup_connection_value (connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONNECTION);
    if (0 != MHD_websocket_check_connection_header (value))
    {
      is_valid = 0;
    }
    value = MHD_lookup_connection_value (connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_UPGRADE);
    if (0 != MHD_websocket_check_upgrade_header (value))
    {
      is_valid = 0;
    }
    value = MHD_lookup_connection_value (connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_SEC_WEBSOCKET_VERSION);
    if (0 != MHD_websocket_check_version_header (value))
    {
      is_valid = 0;
    }
    value = MHD_lookup_connection_value (connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_SEC_WEBSOCKET_KEY);
    if (0 != MHD_websocket_create_accept_header (value, sec_websocket_accept))
    {
      is_valid = 0;
    }

    if (1 == is_valid)
    {
      /* create the response for upgrade */
      response = MHD_create_response_for_upgrade (&upgrade_handler, nullptr);

      /**
       * For the response we need at least the following headers:
       * 1. "Connection: Upgrade"
       * 2. "Upgrade: websocket"
       * 3. "Sec-WebSocket-Accept: <base64value>"
       * The value for Sec-WebSocket-Accept can be generated with MHD_websocket_create_accept_header.
       * It requires the value of the Sec-WebSocket-Key header of the request.
       * See also: https://tools.ietf.org/html/rfc6455#section-4.2.2
       */
      MHD_add_response_header (response, MHD_HTTP_HEADER_UPGRADE, "websocket");
      MHD_add_response_header (response, MHD_HTTP_HEADER_SEC_WEBSOCKET_ACCEPT, sec_websocket_accept);
      ret = MHD_queue_response (connection, MHD_HTTP_SWITCHING_PROTOCOLS, response);
      MHD_destroy_response (response);
    }
    else
    {
      /* return error page */
      struct MHD_Response *response;
      response = MHD_create_response_from_buffer_static(PAGE_INVALID_WEBSOCKET_REQUEST.length(), PAGE_INVALID_WEBSOCKET_REQUEST.c_str());
      ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
      MHD_destroy_response(response);
    }
  } else {
    struct MHD_Response *response;
    response = MHD_create_response_from_buffer_static(PAGE_NOT_FOUND.length(), PAGE_NOT_FOUND.c_str());
    ret = MHD_queue_response (connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
  }
  }

  return (MHD_Result)ret;
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
  if (0 != pthread_mutex_lock (&chat_mutex))
    abort ();

  for (size_t i = 0; i < user_count; ++i) {
    struct ConnectedUser* cu = users[i];
    if (nullptr != cu) {
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
  }

  if (0 != pthread_mutex_unlock (&chat_mutex))
    abort ();
}


namespace acdisplay {

class cWebServer {
public:
  explicit cWebServer(cStaticResourcesRequestHandler& static_resources_request_handler);
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
};

cWebServer::cWebServer(cStaticResourcesRequestHandler& _static_resources_request_handler) :
  daemon(nullptr),
  static_resources_request_handler(_static_resources_request_handler)
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
  cWebServer* pThis = static_cast<cWebServer*>(cls);
  if (pThis == nullptr) {
    std::cerr<<"Error pThis is NULL"<<std::endl;
    return MHD_NO;
  }

  return pThis->static_resources_request_handler.OnRequest(connection, url, method, version, upload_data, upload_data_size, req_cls);
}

bool RunWebServer(const util::cIPAddress& host, uint16_t port, const std::string& private_key, const std::string& public_cert)
{
  cStaticResourcesRequestHandler static_resources_request_handler;

  // Load the static resources
  if (!static_resources_request_handler.LoadStaticResources()) {
    return false;
  }


  if (0 != pthread_mutex_init (&chat_mutex, nullptr))
    return 1;

  cWebServer refactor_webserver(static_resources_request_handler);
  if (!refactor_webserver.Open(host, port, private_key, public_cert)) {
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
  if (0 != pthread_mutex_lock (&chat_mutex))
    abort ();
  disconnect_all = 1;
  for (size_t i = 0; i < user_count; ++i)
    pthread_cond_signal (&users[i]->wake_up_sender);
  if (0 != pthread_mutex_unlock (&chat_mutex))
    abort ();

  // Wait for the connection threads to respond
  std::cout<<"Waiting for the connection threads to respond"<<std::endl;
  sleep (2);

  // Tell each connection to close
  std::cout<<"Closing connections"<<std::endl;
  if (0 != pthread_mutex_lock (&chat_mutex))
    abort ();
  for (size_t i = 0; i < user_count; ++i) {
    struct MHD_UpgradeResponseHandle* urh = users[i]->urh;
    if (nullptr != urh) {
      users[i]->urh = nullptr;
      MHD_upgrade_action (users[i]->urh, MHD_UPGRADE_ACTION_CLOSE);
    }
  }
  if (0 != pthread_mutex_unlock (&chat_mutex))
    abort ();

  // Wait for the connection threads to close
  sleep (2);

  /* usually we should wait here in a safe way for all threads to disconnect, */
  /* but we skip this in the example */

  pthread_mutex_destroy (&chat_mutex);

  refactor_webserver.Close();

  return false;
}

}
