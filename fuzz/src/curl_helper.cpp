#include <cstdio>
#include <cstdlib>

#include <iostream>
#include <sstream>

#include <unistd.h>

#include <curl/curl.h>

#include "curl_helper.h"

namespace curl {

cCurlHelper::cCurlHelper()
{
  curl_global_init(CURL_GLOBAL_ALL);
}

cCurlHelper::~cCurlHelper()
{
  curl_global_cleanup();
}

}


namespace {

static size_t write_data(void* ptr, size_t size, size_t nmemb, void* user_data)
{
  std::ostringstream* pOutputStream = (std::ostringstream*)user_data;
  std::ostringstream& o = *pOutputStream;

  o<<std::string_view((const char*)ptr, size);

  return size;
}

}

namespace curl {

class cCurlHandle {
public:
  cCurlHandle();
  ~cCurlHandle();

  constexpr bool IsValid() const { return curl != nullptr; }

  bool PerformRequest(std::string_view url, std::string_view user_agent, const std::optional<std::string>& self_signed_certificate_path, std::ostringstream& output);

private:
  CURL* curl;
};

cCurlHandle::cCurlHandle() :
  curl(nullptr)
{
  curl = curl_easy_init();
}

cCurlHandle::~cCurlHandle()
{
  if (curl != nullptr) {
    curl_easy_cleanup(curl);
    curl = nullptr;
  }
}

bool cCurlHandle::PerformRequest(std::string_view url, std::string_view user_agent, const std::optional<std::string>& self_signed_certificate_path, std::ostringstream& output)
{
  std::cout<<"PerformRequest url: \""<<url<<"\""<<std::endl;
  curl_easy_setopt(curl, CURLOPT_URL, url.data());
  std::cout<<"b"<<std::endl;
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.data());
  std::cout<<"c"<<std::endl;
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // Turn on debugging

  std::cout<<"d"<<std::endl;
  // Check if we need to provide a self signed public key
  if (self_signed_certificate_path) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, self_signed_certificate_path.value().c_str());
  std::cout<<"e"<<std::endl;
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
  }


  std::cout<<"f"<<std::endl;
  // Send all data to this function
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
  std::cout<<"g"<<std::endl;
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&output);

  std::cout<<"h"<<std::endl;
  // Perform the download
  const CURLcode result = curl_easy_perform(curl);

  return (result == CURLE_OK);
}

bool PerformRequest(std::string_view url, std::string_view user_agent, const std::optional<std::string>& self_signed_certificate_path, std::ostringstream& output)
{
  cCurlHandle handle;

  if (!handle.IsValid()) {
    std::cerr<<"Error creating curl handle"<<std::endl;
    return false;
  }

  return handle.PerformRequest(url, user_agent, self_signed_certificate_path, output);
}

}
