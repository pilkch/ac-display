#pragma once

#include <string_view>

#include "ip_address.h"

namespace gnutlsmm {

bool GnuTLSPerformRequest(const util::cIPAddress& host, uint16_t port, std::string_view request, std::string_view user_agent, std::string_view server_certificate_path);

}
