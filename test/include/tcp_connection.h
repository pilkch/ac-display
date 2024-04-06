#pragma once

#include "ip_address.h"

class tcp_connection {
public:
  tcp_connection();
  ~tcp_connection();

  bool connect(const util::cIPAddress& ip, int port);
  void close();

  constexpr int get_sd() const { return sd; }

  size_t get_bytes_available() const;

private:
  int sd;
};
