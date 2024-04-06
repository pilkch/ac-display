#include <memory.h>

#include <fstream>
#include <functional>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <thread>

// gtest headers
#include <gtest/gtest.h>

// Application headers
#include "gnutlsmm.h"
#include "poll_helper.h"
#include "tcp_connection.h"
#include "util.h"
#include "web_server.h"

namespace {

const util::cIPAddress host(127, 0, 0, 1);
const uint16_t port = 18302;

const size_t BUFFER_LENGTH = 4 * 1024; // 4k read buffer

bool string_to_int(const std::string& s, unsigned long int& out_value, int base = 0)
{
  if (s.empty()) {
    return false;
  }

  char* end = nullptr;
  out_value = std::strtoul(s.c_str(), &end, base);

  if (errno == ERANGE) {
    // Invalid number
    return false;
  }

  if (*end != '\0') {
    // Invalid string
    return false;
  }

  return true;
}

size_t GetFileSizeBytes(const std::string& sFilePath)
{
  struct stat s;
  if (stat(sFilePath.c_str(), &s) < 0) return 0;

  return s.st_size;
}

bool ReadFileIntoVector(const std::string& sFilePath, std::vector<char>& contents)
{
  if (!util::TestFileExists(sFilePath)) {
    std::cerr<<"File \""<<sFilePath<<"\" not found"<<std::endl;
    return false;
  }

  const size_t nFileSizeBytes = GetFileSizeBytes(sFilePath);
  if (nFileSizeBytes == 0) {
    std::cerr<<"Empty file \""<<sFilePath<<"\""<<std::endl;
    return false;
  }

  std::ifstream f(sFilePath);

  contents.reserve(nFileSizeBytes);

  contents.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());

  return true;
}

}


class WebServerTest : public testing::Test {
protected:
  void SetUp() override;
  void TearDown() override;

  acdisplay::cWebServerManager web_server_manager;
};

void WebServerTest::SetUp()
{
  // Create the web server
  if (!web_server_manager.Create(host, port, "./server.key", "./server.crt")) {
    std::cerr<<"Error creating web server"<<std::endl;
  }
}

void WebServerTest::TearDown()
{
  std::cout<<"Shutting down server"<<std::endl;
  if (!web_server_manager.Destroy()) {
    std::cerr<<"Error destroying web server"<<std::endl;
  }
}



int GnuTLSPullTimeOutCallback(gnutls_transport_ptr_t, unsigned int ms)
{
  (void)ms;
  return 0;
}

class cHTTPHeaders {
public:
  cHTTPHeaders() :
    response_code(0),
    content_length(0)
  {
  }

  void Clear()
  {
    response_code = 0;
    content_type.clear();
    content_length = 0;
  }

  uint16_t response_code;
  std::string content_type;
  size_t content_length;
  std::map<std::string, std::string> raw_headers;
};

class cHTTPResponse {
public:
  void Clear()
  {
    headers.Clear();
    content.clear();
  }

  cHTTPHeaders headers;
  std::vector<char> content;
};

bool ParseHeaders(std::string_view text, cHTTPHeaders& headers)
{
  std::cout<<"ParseHeaders"<<std::endl;

  headers.Clear();

  if (text.empty()) {
    return false;
  }

  // Parse the HTTP header
  // Headers: HTTP/1.1 200 OK
  {
    std::cout<<"a"<<std::endl;
    const size_t new_line = text.find("\r\n");
    if (new_line == std::string_view::npos) {
      return false;
    }

    std::cout<<"b"<<std::endl;
    const size_t space1 = text.find(' ');
    if (space1 == std::string_view::npos) {
      return false;
    }

    std::cout<<"c"<<std::endl;
    const size_t space2 = text.find(' ', space1 + 1);
    if (space2 == std::string_view::npos) {
      return false;
    }

    std::cout<<"d space1: "<<space1<<", space2: "<<space2<<std::endl;
    // Parse the response code
    const std::string_view str_value(text.substr(space1, space2 - space1));
    std::cout<<"Response code text: "<<str_value<<std::endl;
    unsigned long int value = 0;
    if (!string_to_int(std::string(str_value), value)) {
      return false;
    }

    if (value > UINT16_MAX) {
      return false;
    }

    headers.response_code = uint16_t(value);

    text.remove_prefix(new_line + 2);
  }

  // Parse the remaining headers
  // Date: Sat, 06 Apr 2024 04:55:51 GMT
  // Connection: close
  // Content-Type: text/html
  // Content-Length: 2330
  while (!text.empty()) {
    const size_t new_line = text.find("\r\n");
    if (new_line == std::string_view::npos) {
      break;
    }

    const size_t colon = text.find(':');
    if (colon == std::string_view::npos) {
      return false;
    }

    const std::string_view key{text.substr(0, colon)};
    const std::string_view str_value{text.substr(colon + 2, new_line - (colon + 2))};
    std::cout<<"key: "<<key<<", str_value: "<<str_value<<std::endl;
    if (key == "Content-Type") {
      headers.content_type = str_value;
    } else if (key == "Content-Length") {
      unsigned long int value = 0;
      if (!string_to_int(std::string(str_value), value)) {
        return false;
      }

      headers.content_length = value;
    } else {
      headers.raw_headers[std::string(key)] = std::string(str_value);
    }

    text.remove_prefix(new_line + 2);
  }

  return (
    (headers.response_code != 0),
    !headers.content_type.empty()
  );
}


bool GnuTLSPerformRequest(std::string_view request, uint16_t port, std::string_view user_agent, std::string_view server_certificate_path, cHTTPResponse& out_response)
{
  std::cout<<"GnuTLSPerformRequest Connecting to "<<util::ToString(host)<<":"<<port<<std::endl;

  out_response.Clear();

  tcp_connection connection;

  gnutlsmm::client_session session;

  session.init(GNUTLS_NONBLOCK);

  // X509 stuff
  gnutlsmm::certificate_credentials credentials;

  credentials.init();

  // Set the trusted ca file
  credentials.set_x509_trust_file(server_certificate_path.data(), GNUTLS_X509_FMT_PEM);

  // Put the x509 credentials to the current session
  session.set_credentials(credentials);

  // Set TLS version and cypher priorities
  // https://gnutls.org/manual/html_node/Priority-Strings.html
  // NOTE: No SSL, only TLS1.2
  // TODO: TLS1.3 didn't seem to work, server dependent?
  //session.set_priority ("NORMAL:+SRP:+SRP-RSA:+SRP-DSS:-DHE-RSA:-VERS-SSL3.0:%SAFE_RENEGOTIATION:%LATEST_RECORD_VERSION", nullptr);
  session.set_priority("SECURE128:+SECURE192:-VERS-ALL:+VERS-TLS1.2:%SAFE_RENEGOTIATION", nullptr);

  // connect to the peer
  connection.connect(host, port);
  session.set_transport_ptr((gnutls_transport_ptr_t)(ptrdiff_t)connection.get_sd());

  // NOTE: TO use a timeout we need to provide a callback function, but becuase we pass GNUTLS_NONBLOCK to init above, then the callback is not called
  session.set_transport_timeout_with_pull_timeout_function(500, GnuTLSPullTimeOutCallback);

  // Perform the TLS handshake
  std::cout << "Performing handshake" << std::endl;
  const int result = session.handshake();
  if (result < 0) {
    std::cerr<<"Handshake failed, error "<<std::to_string(result)<<std::endl;
    return false;
  }

  std::cout << "Handshake completed" << std::endl;

  std::cout << "Sending HTTP request" << std::endl;
  session.send(request.data(), request.length());

  std::cout << "Reading response" << std::endl;

  // For debugging
  //std::ofstream ofs("output.html", std::ofstream::trunc);

  char buffer[BUFFER_LENGTH + 1];

  poll_read p(connection.get_sd());

  const int timeout_ms = 2000;

  // Once we start not receiving data we retry 10 times in 100ms and then exit
  size_t no_bytes_retries = 0;
  const size_t max_no_bytes_retries = 10;
  const size_t retries_timeout_ms = 10;

  std::vector<char> received_so_far;

  bool reading_headers = true;

  // NOTE: gnutls_record_recv may return GNUTLS_E_PREMATURE_TERMINATION
  // https://lists.gnupg.org/pipermail/gnutls-help/2014-May/003455.html
  // This means the peer has terminated the TLS session using a TCP RST (i.e., called close()).
  // Since gnutls cannot distinguish that termination from an attacker terminating the session it warns you with this error code.

  while (no_bytes_retries < max_no_bytes_retries) {
    // Check if there is already something in the gnutls buffers
    if (session.check_pending() == 0) {
      // There was no gnutls data ready, check the socket
      switch (p.poll(timeout_ms)) {
        case POLL_READ_RESULT::DATA_READY: {
          // Check if bytes are actually available (Otherwise if we try to read again the gnutls session object goes into a bad state and gnutlsxx throws an exception)
          if (connection.get_bytes_available() == 0) {
            //std::cout<<"but no bytes available"<<std::endl;
            no_bytes_retries++;
            // Don't hog the CPU
            util::msleep(retries_timeout_ms);
            continue;
          }
        }
        case POLL_READ_RESULT::ERROR: {
          break;
        }
        case POLL_READ_RESULT::TIMED_OUT: {
          // We hit the 2 second timeout, we are probably done
          break;
        }
      }
    }

    const ssize_t result = session.recv(buffer, BUFFER_LENGTH);
    if (result == 0) {
      std::cout<<"Peer has closed the TLS connection"<<std::endl;
      break;
    } else if (result < 0) {
      std::cout<<"Read error: "<<gnutls_strerror_name(result)<<" "<<gnutls_strerror(result)<<std::endl;
      break;
    }

    const size_t bytes_read = result;
    //std::cout<<"Received "<<bytes_read<<" bytes"<<std::endl;
    if (reading_headers) {
      received_so_far.insert(received_so_far.end(), buffer, buffer + bytes_read);

      std::string_view sv(received_so_far.data(), received_so_far.size());
      const size_t delimiter = sv.find("\r\n\r\n");
      if (delimiter != std::string::npos) {
        //std::cout<<"Headers received"<<std::endl;
        std::string_view str_headers = sv.substr(0, delimiter);
        ParseHeaders(str_headers, out_response.headers);

        // Anything after this was file content
        const size_t content_start = delimiter + strlen("\r\n\r\n");

        // We are now up to the content
        reading_headers = false;

        //std::cout<<"Reading content"<<std::endl;

        // Add to the file content
        out_response.content.insert(out_response.content.end(), received_so_far.begin() + content_start, received_so_far.end());
      }
    } else {
      // Everything else is content
      out_response.content.insert(out_response.content.end(), buffer, buffer + bytes_read);
    }
  }

  session.bye(GNUTLS_SHUT_RDWR);

  std::cout<<"Finished"<<std::endl;

  return true;
}

std::string HTTPSCreateRequest(std::string_view url)
{
  return "GET " + std::string(url.data(), url.size()) + " HTTP/1.0\r\n\r\n";
}

bool PerformHTTPSGetRequest(std::span<const char> const url, uint16_t port, std::string_view server_certificate_path, cHTTPResponse& out_response)
{
  std::cout<<"PerformHTTPSGetRequest"<<std::endl;

  std::string user_agent("UnitTest");
  const std::string request = HTTPSCreateRequest(std::string_view{url.data(), url.size()});
  if (!GnuTLSPerformRequest(request, port, user_agent, server_certificate_path, out_response)) {
    return false;
  }

  //std::cout<<"Headers: "<<out_response.headers.response_code<<", "<<out_response.headers.content_type<<", "<<out_response.headers.content_length<<std::endl;
  //std::cout<<"Content: "<<std::string(out_response.content.data(), out_response.content.size())<<std::endl;

  std::cout<<"PerformGetRequest returning true"<<std::endl;
  return true;
}

bool PerformHTTPSGetRequestSpan(std::span<const char> const request, cHTTPResponse& out_response)
{
  return PerformHTTPSGetRequest(request, port, "./server.crt", out_response);
}

bool PerformHTTPSGetRequestString(const std::string& request, cHTTPResponse& out_response)
{
  return PerformHTTPSGetRequest(request, port, "./server.crt", out_response);
}

// NOTE: I would split this up, but every new function recreates the web server, I'd rather just run a single instance
TEST_F(WebServerTest, TestResources)
{
  std::vector<char> expected_content_index_html;
  ASSERT_TRUE(ReadFileIntoVector("resources/index.html", expected_content_index_html));
  std::vector<char> expected_content_style_css;
  ASSERT_TRUE(ReadFileIntoVector("resources/style.css", expected_content_style_css));
  std::vector<char> expected_content_receive_js;
  ASSERT_TRUE(ReadFileIntoVector("resources/receive.js", expected_content_receive_js));
  std::vector<char> expected_content_dial_js;
  ASSERT_TRUE(ReadFileIntoVector("resources/dial.js", expected_content_dial_js));
  std::vector<char> expected_content_util_js;
  ASSERT_TRUE(ReadFileIntoVector("resources/util.js", expected_content_util_js));
  std::vector<char> expected_content_disconnected_icon_svg;
  ASSERT_TRUE(ReadFileIntoVector("resources/disconnected_icon.svg", expected_content_disconnected_icon_svg));
  std::vector<char> expected_content_fullscreen_icon_svg;
  ASSERT_TRUE(ReadFileIntoVector("resources/fullscreen_icon.svg", expected_content_fullscreen_icon_svg));
  std::vector<char> expected_content_favicon_svg;
  ASSERT_TRUE(ReadFileIntoVector("resources/favicon.svg", expected_content_favicon_svg));

  cHTTPResponse response;

  // Exact matches for each resource
  EXPECT_TRUE(PerformHTTPSGetRequestString("/", response));
  EXPECT_EQ(200, response.headers.response_code);
  EXPECT_STREQ("text/html", response.headers.content_type.c_str());
  EXPECT_TRUE(response.content == expected_content_index_html);

  EXPECT_TRUE(PerformHTTPSGetRequestString("/style.css", response));
  EXPECT_EQ(200, response.headers.response_code);
  EXPECT_STREQ("text/css", response.headers.content_type.c_str());
  EXPECT_TRUE(response.content == expected_content_style_css);

  EXPECT_TRUE(PerformHTTPSGetRequestString("/receive.js", response));
  EXPECT_EQ(200, response.headers.response_code);
  EXPECT_STREQ("text/javascript", response.headers.content_type.c_str());
  EXPECT_TRUE(response.content == expected_content_receive_js);
  EXPECT_TRUE(PerformHTTPSGetRequestString("/dial.js", response));
  EXPECT_EQ(200, response.headers.response_code);
  EXPECT_STREQ("text/javascript", response.headers.content_type.c_str());
  EXPECT_TRUE(response.content == expected_content_dial_js);
  EXPECT_TRUE(PerformHTTPSGetRequestString("/util.js", response));
  EXPECT_EQ(200, response.headers.response_code);
  EXPECT_STREQ("text/javascript", response.headers.content_type.c_str());
  EXPECT_TRUE(response.content == expected_content_util_js);

  EXPECT_TRUE(PerformHTTPSGetRequestString("/disconnected_icon.svg", response));
  EXPECT_EQ(200, response.headers.response_code);
  EXPECT_STREQ("image/svg+xml", response.headers.content_type.c_str());
  EXPECT_TRUE(response.content == expected_content_disconnected_icon_svg);
  EXPECT_TRUE(PerformHTTPSGetRequestString("/fullscreen_icon.svg", response));
  EXPECT_EQ(200, response.headers.response_code);
  EXPECT_STREQ("image/svg+xml", response.headers.content_type.c_str());
  EXPECT_TRUE(response.content == expected_content_fullscreen_icon_svg);
  EXPECT_TRUE(PerformHTTPSGetRequestString("/favicon.svg", response));
  EXPECT_EQ(200, response.headers.response_code);
  EXPECT_STREQ("image/svg+xml", response.headers.content_type.c_str());
  EXPECT_TRUE(response.content == expected_content_favicon_svg);

  // Resources that have extra data on the end which will be trimmed and match the real file
  EXPECT_TRUE(PerformHTTPSGetRequestString("/style.css?something_else", response));
  EXPECT_EQ(200, response.headers.response_code);
  EXPECT_STREQ("text/css", response.headers.content_type.c_str());

  // Resources that build on existing file paths so that they don't match any more
  EXPECT_TRUE(PerformHTTPSGetRequestString("/style.css.something.else", response));
  EXPECT_EQ(404, response.headers.response_code);
  EXPECT_TRUE(PerformHTTPSGetRequestString("/style.css&something&else", response));
  EXPECT_EQ(404, response.headers.response_code);

  // Resources that don't exist
  EXPECT_TRUE(PerformHTTPSGetRequestString("", response));
  EXPECT_EQ(404, response.headers.response_code);
  EXPECT_TRUE(PerformHTTPSGetRequestString("/missing_missing.txt", response));
  EXPECT_EQ(404, response.headers.response_code);
  EXPECT_TRUE(PerformHTTPSGetRequestString("/missing_dir/missing_file.txt", response));
  EXPECT_EQ(404, response.headers.response_code);

  // Resources that aren't in this directory and we do not want to send back as a response
  EXPECT_TRUE(PerformHTTPSGetRequestString("../", response));
  EXPECT_EQ(404, response.headers.response_code);
  EXPECT_TRUE(PerformHTTPSGetRequestString("../snooping_around.txt", response));
  EXPECT_EQ(404, response.headers.response_code);
  EXPECT_TRUE(PerformHTTPSGetRequestString("/bin/bash", response));
  EXPECT_EQ(404, response.headers.response_code);
  EXPECT_TRUE(PerformHTTPSGetRequestString("/etc/fstab", response));
  EXPECT_EQ(404, response.headers.response_code);


  // Bad requests
  const std::string user_agent("UnitTest");
  const std::string request_bad_url = "GET zvjiofkweklnewfjksdfiusdfjsif HTTP/1.0\r\n\r\n";
  EXPECT_TRUE(GnuTLSPerformRequest(request_bad_url, port, user_agent, "./server.crt", response));
  EXPECT_EQ(404, response.headers.response_code);
  const std::string request_missing_url = "GET  HTTP/1.0\r\n\r\n";
  EXPECT_TRUE(GnuTLSPerformRequest(request_missing_url, port, user_agent, "./server.crt", response));
  EXPECT_EQ(404, response.headers.response_code);


  // Validate http headers
  EXPECT_TRUE(PerformHTTPSGetRequestString("/", response));
  EXPECT_EQ(200, response.headers.response_code);
  EXPECT_STREQ("text/html", response.headers.content_type.c_str());
  EXPECT_STREQ(response.headers.raw_headers["strict-transport-security"].c_str(), "max-age=31536000; includeSubDomains; preload");
  EXPECT_STREQ(response.headers.raw_headers["x-content-type-options"].c_str(), "nosniff");
  EXPECT_STREQ(response.headers.raw_headers["referrer-policy"].c_str(), "same-origin");
  EXPECT_STREQ(response.headers.raw_headers["content-security-policy"].c_str(), "frame-ancestors 'none'");
  EXPECT_STREQ(response.headers.raw_headers["permissions-policy"].c_str(), "");
  EXPECT_STREQ(response.headers.raw_headers["cross-origin-embedder-policy-report-only"].c_str(), "require-corp; report-to=\"default\"");
  EXPECT_STREQ(response.headers.raw_headers["cross-origin-opener-policy"].c_str(), "same-origin; report-to=\"default\"");
  EXPECT_STREQ(response.headers.raw_headers["cross-origin-opener-policy-report-only"].c_str(), "same-origin; report-to=\"default\"");
  EXPECT_STREQ(response.headers.raw_headers["cross-origin-resource-policy"].c_str(), "same-origin");
}
