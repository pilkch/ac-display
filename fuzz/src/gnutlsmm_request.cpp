#include <memory.h>

#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <thread>

#include "gnutlsmm.h"
#include "gnutlsmm_request.h"
#include "poll_helper.h"
#include "tcp_connection.h"
#include "util.h"

namespace {

const size_t BUFFER_LENGTH = 4 * 1024; // 4k read buffer

}

namespace gnutlsmm {

int GnuTLSPullTimeOutCallback(gnutls_transport_ptr_t, unsigned int ms)
{
  (void)ms;
  return 0;
}

bool GnuTLSPerformRequest(const util::cIPAddress& host, uint16_t port, std::string_view request, std::string_view user_agent, std::string_view server_certificate_path)
{
  std::cout<<"GnuTLSPerformRequest Connecting to "<<util::ToString(host)<<":"<<port<<std::endl;

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
  session.send(request.data(), request.size());

  std::cout << "Reading response" << std::endl;

  char buffer[BUFFER_LENGTH + 1];

  poll_read p(connection.get_sd());

  const int timeout_ms = 2000;

  // Once we start not receiving data we retry 10 times in 100ms and then exit
  size_t no_bytes_retries = 0;
  const size_t max_no_bytes_retries = 10;
  const size_t retries_timeout_ms = 10;

  std::string received_so_far;

  bool reading_headers = true;

  // NOTE: gnutls_record_recv may return GNUTLS_E_PREMATURE_TERMINATION
  // https://lists.gnupg.org/pipermail/gnutls-help/2014-May/003455.html
  // This means the peer has terminated the TLS session using a TCP RST (i.e., called close()).
  // Since gnutls cannot distinguish that termination from an attacker terminating the session it warns you with this error code.

  while (no_bytes_retries < max_no_bytes_retries) {
    // Check if there is already something in the gnutls buffers
    if (session.check_pending() == 0) {
      // There was no gnutls data ready, check the socket
      const POLL_READ_RESULT result = p.poll(timeout_ms);
      if ((result == POLL_READ_RESULT::ERROR) || (result == POLL_READ_RESULT::TIMED_OUT)) {
        // There was an error or we hit the timeout, we are probably done
        break;
      }

      // Check if bytes are actually available (Otherwise if we try to read again the gnutls session object goes into a bad state and gnutlsxx throws an exception)
      if (connection.get_bytes_available() == 0) {
        //std::cout<<"but no bytes available"<<std::endl;
        no_bytes_retries++;
        // Don't hog the CPU
        util::msleep(retries_timeout_ms);
        continue;
      }
    }

    std::cout<<"Reading from session"<<std::endl;
    const ssize_t result = session.recv(buffer, BUFFER_LENGTH);
    if (result == 0) {
      std::cout<<"Peer has closed the TLS connection"<<std::endl;
      break;
    } else if (result < 0) {
      std::cout<<"Read error: "<<gnutls_strerror_name(result)<<" "<<gnutls_strerror(result)<<std::endl;
      break;
    }

    const size_t bytes_read = result;
    //std::cout << "Received " << bytes_read << " bytes" << std::endl;
    if (reading_headers) {
      received_so_far.append(buffer, bytes_read);

      size_t i = received_so_far.find("\r\n\r\n");
      if (i != std::string::npos) {
        std::cout<<"Headers received"<<std::endl;

        // Anything after this was file content
        i += strlen("\r\n\r\n");

        // We are now up to the content
        reading_headers = false;

        std::cout<<"Reading content"<<std::endl;
      }
    } else {
      // Everything else is content
    }
  }

  session.bye(GNUTLS_SHUT_RDWR);

  std::cout<<"Finished"<<std::endl;

  return true;
}

}
