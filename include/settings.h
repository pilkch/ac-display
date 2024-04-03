#pragma once

#include <cstdint>
#include <string>

#include "ip_address.h"

namespace application {

class cSettings {
public:
  cSettings();

  bool LoadFromFile(const std::string& file_path);

  constexpr bool IsValid() const;
  void Clear();

  constexpr bool GetRunningInContainer() const { return running_in_container; }
  constexpr const util::cIPAddress& GetACUDPHost() const { return acudp_host; }
  constexpr uint16_t GetACUDPPort() const { return acudp_port; }
  constexpr const util::cIPAddress& GetHTTPSHost() const { return https_host; }
  constexpr uint16_t GetHTTPSPort() const { return https_port; }
  constexpr const std::string& GetHTTPSPrivateKey() const { return https_private_key; }
  constexpr const std::string& GetHTTPSPublicCert() const { return https_public_cert; }

private:
  bool running_in_container;
  util::cIPAddress acudp_host;
  uint16_t acudp_port;
  util::cIPAddress https_host;
  uint16_t https_port;
  std::string https_private_key;
  std::string https_public_cert;
};

}
