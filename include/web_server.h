#pragma once

#include "ip_address.h"

namespace acdisplay {

class cStaticResourcesRequestHandler;
class cWebSocketRequestHandler;
class cWebServer;

// TODO: Refactor this, it is a bit of a mess
class cWebServerManager {
public:
  cWebServerManager();
  ~cWebServerManager();

  bool Create(const util::cIPAddress& host, uint16_t port, const std::string& private_key, const std::string& public_cert);
  bool Destroy();

private:
  // NOTE: We would use std::unique_ptr, but it needs to know about the destructor of the item to delete it
  cStaticResourcesRequestHandler* static_resources_request_handler;
  cWebSocketRequestHandler* web_socket_request_handler;
  cWebServer* webserver;
};

}
