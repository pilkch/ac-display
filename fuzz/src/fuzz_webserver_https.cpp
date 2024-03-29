#include <memory.h>

#include <functional>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <thread>

#include <netdb.h> 
#include <poll.h>

#include "curl_helper.h"
#include "util.h"
#include "web_server.h"

namespace {

const util::cIPAddress host(127, 0, 0, 1);
const uint16_t port = 14327;

void PerformHTTPSGetRequest(std::span<const char> const fuzz_data)
{
  const std::string base_url("https://" + util::ToString(host) + ":" + std::to_string(port));

  std::vector<char> url;
  for (auto&& c : base_url) {
    url.push_back(c);
  }
  for (auto&& c : fuzz_data) {
    url.push_back(c);
  }
  url.push_back(0);

  std::string user_agent("FuzzTester");
  std::ostringstream output;
  curl::PerformRequest(std::string((const char*)url.data(), url.size()), user_agent, "../server.crt", output);

  std::cout<<"Output: "<<output.str()<<std::endl;

  std::cout<<"PerformGetRequest returning"<<std::endl;
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t data_length)
{
  // libcurl can't cope with NULL or empty URLs
  if ((data == nullptr) || (data_length == 0)) {
    return -1;
  }

  curl::cCurlHelper curlHelper;

  // Create the web server
  acdisplay::cWebServerManager web_server_manager;
  if (!web_server_manager.Create(host, port, "../server.key", "../server.crt")) {
    std::cerr<<"Error creating web server"<<std::endl;
    return -1;
  }

  PerformHTTPSGetRequest(std::span<const char>{(const char*)data, data_length});

  (void)getc(stdin);

  std::cout<<"Shutting down server"<<std::endl;
  if (!web_server_manager.Destroy()) {
    std::cerr<<"Error destroying web server"<<std::endl;
  }

  std::cout<<"returning 0"<<std::endl;
  return 0;
}
