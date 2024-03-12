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

  const util::cIPAddress& GetACUDPHost() const { return acudp_host; }
  uint16_t GetACUDPPort() const { return acudp_port; }
  const util::cIPAddress& GetHTTPSHost() const { return https_host; }
  uint16_t GetHTTPSPort() const { return https_port; }
  const std::string& GetHTTPSPrivateKey() const { return https_private_key; }
  const std::string& GetHTTPSPublicCert() const { return https_public_cert; }

private:
  util::cIPAddress acudp_host;
  uint16_t acudp_port;
  util::cIPAddress https_host;
  uint16_t https_port;
  std::string https_private_key;
  std::string https_public_cert;
};

}
