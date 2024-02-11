// Application headers
#include "settings.h"

// gtest headers
#include <gtest/gtest.h>

TEST(Application, TestSettings)
{
  application::cSettings settings;
  ASSERT_TRUE(settings.LoadFromFile("test/data/configuration.json"));

  const util::cIPAddress address = settings.GetIPAddress();
  EXPECT_EQ(192, address.octet0);
  EXPECT_EQ(168, address.octet1);
  EXPECT_EQ(0, address.octet2);
  EXPECT_EQ(123, address.octet3);

  EXPECT_EQ(8443, settings.GetPort());
}
