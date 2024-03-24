#pragma once

#include "ip_address.h"

namespace acdisplay {

bool RunWebServer(const util::cIPAddress& ip_address, uint16_t port, const std::string& private_key, const std::string& public_cert);

}
