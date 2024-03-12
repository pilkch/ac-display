#pragma once

#include <cstdint>

#include <string_view>

namespace util {

class cIPAddress {
public:
  cIPAddress();
  cIPAddress(uint8_t octet0, uint8_t octet1, uint8_t octet2, uint8_t octet3);

  void Clear();

  bool IsValid() const;

  uint8_t octet0;
  uint8_t octet1;
  uint8_t octet2;
  uint8_t octet3;
};

std::string ToString(const cIPAddress& address);
bool ParseAddress(const std::string& text, cIPAddress& out_address);

}
