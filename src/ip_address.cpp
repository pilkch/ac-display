#include <iostream>
#include <sstream>

#include "ip_address.h"

namespace util {

cIPAddress::cIPAddress() :
  octet0(0),
  octet1(0),
  octet2(0),
  octet3(0)
{
}

cIPAddress::cIPAddress(uint8_t _octet0, uint8_t _octet1, uint8_t _octet2, uint8_t _octet3) :
  octet0(_octet0),
  octet1(_octet1),
  octet2(_octet2),
  octet3(_octet3)
{
}

void cIPAddress::Clear()
{
  octet0 = 0;
  octet1 = 0;
  octet2 = 0;
  octet3 = 0;
}

bool cIPAddress::IsValid() const
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


std::string ToString(const cIPAddress& address)
{
  std::ostringstream o;
  o<<int(address.octet0)<<"."<<int(address.octet1)<<"."<<int(address.octet2)<<"."<<int(address.octet3);
  return o.str();
}

bool ParseAddress(const std::string& text, cIPAddress& out_address)
{
  out_address.Clear();

  // This isn't the best way of parsing the address, but it works for the straightforward cases at least
  std::stringstream s(text);
  int a,b,c,d;
  char dot0,dot1,dot2;
  s >> a >> dot0 >> b >> dot1 >> c >> dot2 >> d;

  if ((a < 0) || (a > 255) || (b < 0) || (b > 255) || (c < 0) || (c > 255) || (d < 0) || (d > 255)) {
    std::cout<<"Invalid octet"<<std::endl;
    return false;
  } else if ((dot0 != '.') || (dot1 != '.') || (dot2 != '.')) {
    std::cout<<"Invalid dot"<<std::endl;
    return false;
  } else if (s.rdbuf()->in_avail() != 0) {
    std::cout<<"More data in the stringstream"<<std::endl;
    // There is more after the IP address
    return false;
  }

  out_address.octet0 = a;
  out_address.octet1 = b;
  out_address.octet2 = c;
  out_address.octet3 = d;

  return true;
}

}
