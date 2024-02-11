#pragma once

#include <cstdint>
#include <string>

#include "ip_address.h"

namespace application {

class cSettings {
public:
  bool LoadFromFile(const std::string& file_path);

  bool IsValid() const;
  void Clear();

  const util::cIPAddress& GetIPAddress() const { return address; }
  uint16_t GetPort() const { return port; }

private:
  util::cIPAddress address;
  uint16_t port;
};

}
