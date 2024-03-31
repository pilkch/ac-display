#include <memory.h>

#include <memory>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "tcp_connection.h"

tcp_connection::tcp_connection() :
  sd(-1)
{
}

tcp_connection::~tcp_connection()
{
  close();
}

bool tcp_connection::connect(const util::cIPAddress& ip, int port)
{
  close();

  // Connect to server
  sd = ::socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in sa;
  ::memset(&sa, '\0', sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = ::htons(port);
  ::inet_pton(AF_INET, util::ToString(ip).c_str(), &sa.sin_addr);

  const int result = ::connect(sd, (struct sockaddr *) &sa, sizeof(sa));
  return (result >= 0);
}

size_t tcp_connection::get_bytes_available() const
{
  int bytes_available = 0;
  ::ioctl(sd, FIONREAD, &bytes_available);
  return size_t(std::max(0, bytes_available));
}

void tcp_connection::close()
{
  if (sd != -1) {
    ::shutdown(sd, SHUT_RDWR); // No more receptions or transmissions
    ::close(sd);
    sd = -1;
  }
}
