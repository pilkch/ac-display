#pragma once

#include <cstdint>

#include <string_view>

namespace util {

class cIPAddress {
public:
  cIPAddress();
  cIPAddress(uint8_t octet0, uint8_t octet1, uint8_t octet2, uint8_t octet3);

  void Clear();

  constexpr bool IsValid() const;

  uint8_t octet0;
  uint8_t octet1;
  uint8_t octet2;
  uint8_t octet3;
};

inline constexpr bool cIPAddress::IsValid() const
{
  // No this is not complete, but it is good enough, I'm only using the 192.168.x.x range anyway, feel free to improve these checks
  return (
    // 10.0.0.0 - 10.255.255.255 (10/8 prefix)
    (octet0 == 10) ||

    // 172.16.0.0 - 172.31.255.255 (172.16/12 prefix)
    ((octet0 == 172) && ((octet1 >= 16) && (octet1 <= 31))) ||

    // 192.168.0.0 - 192.168.255.255 (192.168/16 prefix)
    ((octet0 == 192) && (octet1 == 168)) ||

    // 127.0.0.1
    ((octet0 == 127) && (octet1 == 0) && (octet2 == 0) && (octet3 == 1))
  );
}


std::string ToString(const cIPAddress& address);
bool ParseAddress(const std::string& text, cIPAddress& out_address);

}
