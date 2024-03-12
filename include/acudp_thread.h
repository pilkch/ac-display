#pragma once

#include <cstdint>

#include <acudp.hpp>

#include <ip_address.h>

namespace acdisplay {

bool StartACUDPThread(const util::cIPAddress& ip_address, uint16_t port);

}
