// acudp headers
#include <acudp.hpp>

// gtest headers
#include <gtest/gtest.h>

namespace acudp_copied {

/**
 * Reads buffer formatted as 100 byte len array of shorts,
 * terminated by 0x0025, into str as string.
 */
void _read_data_string(char *str, const char *buf)
{
    int nread = 0;
    while (nread < 50) {
        if (*buf == 0x25) {
            *str = '\0';
            break;
        }

        *str++ = *buf++;
        buf++;            // skip 0x00
        nread++;
    }

    if (nread == 50) *--str = '\0'; // Always nul terminate
}

/**
 * Reads little-endian int stored in buf, and copies it into n.
 */
void _read_data_int(int *n, const char *buf)
{
    memcpy(n, buf, sizeof(int));
}

void format_setup_response_from_data(acudp_setup_response_t *resp, const char *buf)
{
    _read_data_string(resp->car_name,    buf);
    _read_data_string(resp->driver_name, buf + 100);
    _read_data_int(  &resp->identifier,  buf + 2 * 100);
    _read_data_int(  &resp->version,     buf + 2 * 100 + 4);
    _read_data_string(resp->track_name,  buf + 2 * 100 + 2 * 4);
}

void format_lap_from_data(acudp_lap_t *lap, const char *buf)
{
    _read_data_int(  &lap->car_identifier_number,  buf);
    _read_data_int(  &lap->lap,                    buf + 4);
    _read_data_string(lap->driver_name,            buf + 2*4);
    _read_data_string(lap->car_name,               buf + 100 + 2*4);
    _read_data_int(  &lap->time_ms,                buf + 2*100 + 2*4);
}

}


TEST(ACUDP, TestHandshakeResponse)
{
  const char* packet =
"\x67\x00\x72\x00\x32\x00\x5f\x00\x6f\x00\x70\x00\x65\x00\x6c\x00" \
"\x5f\x00\x6b\x00\x61\x00\x64\x00\x65\x00\x74\x00\x74\x00\x25\x00" \
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x6d\x00\x79\x00\x6e\x00\x61\x00\x6d\x00\x65\x00" \
"\x25\x00\x2d\xdc\x78\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x00\x00\x00\x00\x92\x10\x00\x00\x01\x00\x00\x00" \
"\x6b\x00\x73\x00\x5f\x00\x62\x00\x72\x00\x61\x00\x6e\x00\x64\x00" \
"\x73\x00\x5f\x00\x68\x00\x61\x00\x74\x00\x63\x00\x68\x00\x25\x00" \
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\xd4\xce\x55\x3c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\xb0\xf4\x4f\xcc\x70\x00\x00\x00\xc8\xe7\xff\xee\xf6\x7f\x00\x00" \
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x6b\x00\x73\x00\x5f\x00\x62\x00\x72\x00\x61\x00" \
"\x6e\x00\x64\x00\x73\x00\x5f\x00\x68\x00\x61\x00\x74\x00\x63\x00" \
"\x68\x00\x25\x00\x76\xc4\x7e\x3f\xb1\x37\xbc\x3d\x00\x00\x00\x00" \
"\x0e\x35\x6b\x3f\xde\x1a\xa4\x3b\xa2\x1b\xca\xbe\x00\x00\x00\x00" \
"\x6e\xd1\x18\xc3\xd0\xfa\x0f\xc1\xf3\xa7\xba\xc3\x00\x00\x80\x3f" \
"\x00\x00\x80\x3f\x00\x00\x80\x3f\x00\x00\x80\x3f\x00\x00\x80\x3f" \
"\x00\x00\x00\x00\x00\x00\x00\x00";

  acudp_setup_response_t response;
  acudp_copied::format_setup_response_from_data(&response, packet);

  std::cout<<"Handshake response:"<<std::endl;
  std::cout<<"  car_name: "<<response.car_name<<std::endl;
  std::cout<<"  driver_name: "<<response.driver_name<<std::endl;
  std::cout<<"  identifier: "<<response.identifier<<std::endl;
  std::cout<<"  version: "<<response.version<<std::endl;
  std::cout<<"  track_name: "<<response.track_name<<std::endl;
  std::cout<<"  track_config: "<<response.track_config<<std::endl;

  EXPECT_STREQ("gr2_opel_kadett", response.car_name);
  EXPECT_STREQ("myname", response.driver_name);
  EXPECT_EQ(4242, response.identifier);
  EXPECT_EQ(1, response.version);
  EXPECT_STREQ("ks_brands_hatch", response.track_name);
}

TEST(ACUDP, TestCarPacket)
{
  // Test packet format
  const char* packet =
"\x61\xbd\x7b\x92\x48\x01\x00\x00\x6b\xaf\xdf\x3b\xd7\xfd\x8a\x3b" \
"\x06\x8a\xf8\x3a\x00\x00\x00\x00\x00\x00\x45\xbe\x60\x42\xa2\x0d" \
"\x60\x42\xa2\x0d\x60\x42\xa2\x0d\x11\x5e\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x80\x3f\xb8\xc0\x54\x44\x00\x00\x00\x00\x01\x00\x00\x00" \
"\x70\xb0\x0f\x3f\x96\x55\x61\x81\x99\x48\x67\x81\x4d\xab\x5c\x01" \
"\x90\x60\x48\x01\x33\x0e\xa9\x42\x35\xe3\xa4\x42\xa4\x27\xa8\x42" \
"\x2d\x7b\xa3\x42\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x4c\x70\x5c\xbc\x61\xf6\x57\xbc\xbf\x26\x3d\x3c" \
"\xe1\x2f\xc7\xbc\xb7\xee\xb6\x3b\x4e\xfa\xc1\x3b\x3f\x62\xaf\x3b" \
"\xb3\xc6\xb6\x3b\xa9\x5a\x64\x3c\x1c\x42\x61\x3c\xa3\x70\x62\x3c" \
"\xfd\x91\x60\x3c\x23\x7a\x00\x45\x61\xa0\x09\x45\x19\xfe\x04\x45" \
"\xe3\x0d\x0b\x45\xa8\x72\xa7\x3f\xeb\x60\xa9\x3f\xf1\x97\xa8\x3f" \
"\xea\x7c\xa8\x3f\x8f\x86\x1e\xbb\x57\xf8\x2f\xbb\x3e\xd7\x12\xbb" \
"\x20\x5f\x1c\xbb\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
"\x00\x00\x00\x00\x1b\xd6\x13\xbc\x50\xaa\x34\xbc\x58\x20\x54\xbb" \
"\x84\x16\xb9\xba\xbc\x74\x93\x3e\xbc\x74\x93\x3e\xbc\x74\x93\x3e" \
"\xbc\x74\x93\x3e\x89\x19\x91\x3e\x95\xee\x90\x3e\xcb\x49\x91\x3e" \
"\x83\x30\x91\x3e\xc2\xcd\x15\x3d\x9a\x67\x20\x3d\x16\x20\xa6\x3d" \
"\xe0\x4a\xa8\x3d\x55\x43\x7d\x3f\x00\x00\x00\x00\x1b\x30\x19\xc3" \
"\xf9\xcb\x02\xc1\x79\xf2\xbb\xc3";

  const acudp_car* pCar = (const acudp_car*)packet;
  const acudp_car& car = *pCar;

  std::cout<<"Car:"<<std::endl;
  std::cout<<"  identifier: "<<car.identifier<<std::endl;
  std::cout<<"  size: "<<car.size<<std::endl;
  std::cout<<"  speed_kmh: "<<car.speed_kmh<<std::endl;
  std::cout<<"  lap_time: "<<car.lap_time<<std::endl;
  std::cout<<"  "<<car.car_position_normalized<<std::endl;
  std::cout<<"  lap_count: "<<car.lap_count<<std::endl;
  std::cout<<"  engine_rpm: "<<car.engine_rpm<<std::endl;
  std::cout<<"  gear: "<<car.gear<<std::endl;
  std::cout<<"  gas: "<<car.gas<<std::endl;
  std::cout<<"  brake: "<<car.brake<<std::endl;
  std::cout<<"  clutch: "<<car.clutch<<std::endl;

  EXPECT_EQ('a', car.identifier);
  EXPECT_EQ(328, car.size);
  EXPECT_EQ(0, int(car.speed_kmh));
  EXPECT_EQ(24081, car.lap_time);
  EXPECT_EQ(0, car.lap_count);
  EXPECT_EQ(851, int(car.engine_rpm));
  EXPECT_EQ(1, car.gear);
  EXPECT_EQ(0, car.gas);
  EXPECT_EQ(0, car.brake);
  EXPECT_EQ(1, car.clutch);
}