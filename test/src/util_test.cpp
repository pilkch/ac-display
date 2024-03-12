// Application headers
#include "ip_address.h"
#include "util.h"

// gtest headers
#include <gtest/gtest.h>

TEST(Util, TestIPAddress)
{
  {
    // Test default constructor
    util::cIPAddress address;
    EXPECT_EQ(0, address.octet0);
    EXPECT_EQ(0, address.octet1);
    EXPECT_EQ(0, address.octet2);
    EXPECT_EQ(0, address.octet3);
  }
  {
    // Test constructor and clear
    util::cIPAddress address(1,2,3,4);
    EXPECT_EQ(1, address.octet0);
    EXPECT_EQ(2, address.octet1);
    EXPECT_EQ(3, address.octet2);
    EXPECT_EQ(4, address.octet3);

    address.Clear();
    EXPECT_EQ(0, address.octet0);
    EXPECT_EQ(0, address.octet1);
    EXPECT_EQ(0, address.octet2);
    EXPECT_EQ(0, address.octet3);
  }
  {
    // Test IsValid
    util::cIPAddress all_zeroes(0,0,0,0);
    EXPECT_FALSE(all_zeroes.IsValid());

    util::cIPAddress valid_10_range(10,1,2,3);
    EXPECT_TRUE(valid_10_range.IsValid());

    util::cIPAddress valid_172_16_range(172,16,2,3);
    EXPECT_TRUE(valid_172_16_range.IsValid());

    util::cIPAddress valid_192_168_range(192,168,2,3);
    EXPECT_TRUE(valid_192_168_range.IsValid());

    util::cIPAddress valid_127_0_0_1(127,0,0,1);
    EXPECT_TRUE(valid_127_0_0_1.IsValid());
  }

  {
    // Test ParseAddress
    util::cIPAddress address;

    // Invalid addresses
    EXPECT_FALSE(util::ParseAddress("", address));
    EXPECT_FALSE(util::ParseAddress("1.2.3.4.5", address));
    EXPECT_FALSE(util::ParseAddress("1.2.3..4", address));
    EXPECT_FALSE(util::ParseAddress("1..2.3.4", address));
    EXPECT_FALSE(util::ParseAddress("1.2.3,4", address));
    EXPECT_FALSE(util::ParseAddress("1,2,3,4", address));
    EXPECT_FALSE(util::ParseAddress("1024.1024.1024.1024", address));
    EXPECT_FALSE(util::ParseAddress("-1024.-1024.-1024.-1024", address));

    // Valid addresses
    EXPECT_TRUE(util::ParseAddress("1.2.3.4", address));
    EXPECT_EQ(1, address.octet0);
    EXPECT_EQ(2, address.octet1);
    EXPECT_EQ(3, address.octet2);
    EXPECT_EQ(4, address.octet3);

    EXPECT_TRUE(util::ParseAddress("192.168.1.2", address));
    EXPECT_EQ(192, address.octet0);
    EXPECT_EQ(168, address.octet1);
    EXPECT_EQ(1, address.octet2);
    EXPECT_EQ(2, address.octet3);

    EXPECT_TRUE(util::ParseAddress("252.253.254.255", address));
    EXPECT_EQ(252, address.octet0);
    EXPECT_EQ(253, address.octet1);
    EXPECT_EQ(254, address.octet2);
    EXPECT_EQ(255, address.octet3);
  }

  {
    // Test ToString
    EXPECT_STREQ("1.2.3.4", util::ToString(util::cIPAddress(1, 2, 3, 4)).c_str());
    EXPECT_STREQ("192.168.12.34", util::ToString(util::cIPAddress(192, 168, 12, 34)).c_str());
    EXPECT_STREQ("252.253.254.255", util::ToString(util::cIPAddress(252, 253, 254, 255)).c_str());
    EXPECT_STREQ("0.0.0.0", util::ToString(util::cIPAddress(0, 0, 0, 0)).c_str());
  }
}
