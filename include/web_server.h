#pragma once

#include "ip_address.h"

namespace acdisplay {

class cWebServer {
public:
  bool Run(const util::cIPAddress& ip_address, uint16_t port, const std::string& private_key, const std::string& public_cert);

private:

};

}
