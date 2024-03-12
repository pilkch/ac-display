// Application headers
#include "settings.h"

// gtest headers
#include <gtest/gtest.h>

TEST(Application, TestSettings)
{
  application::cSettings settings;
  ASSERT_TRUE(settings.LoadFromFile("test/data/configuration.json"));

  const util::cIPAddress acudp_host = settings.GetACUDPHost();
  EXPECT_EQ(192, acudp_host.octet0);
  EXPECT_EQ(168, acudp_host.octet1);
  EXPECT_EQ(0, acudp_host.octet2);
  EXPECT_EQ(2, acudp_host.octet3);

  EXPECT_EQ(9997, settings.GetACUDPPort());

  const util::cIPAddress https_host = settings.GetHTTPSHost();
  EXPECT_EQ(192, https_host.octet0);
  EXPECT_EQ(168, https_host.octet1);
  EXPECT_EQ(0, https_host.octet2);
  EXPECT_EQ(3, https_host.octet3);

  EXPECT_EQ(8443, settings.GetHTTPSPort());

  const std::string https_private_key = settings.GetHTTPSPrivateKey();
  EXPECT_STREQ("./server.key", https_private_key.c_str());

  const std::string https_public_cert = settings.GetHTTPSPublicCert();
  EXPECT_STREQ("./server.crt", https_public_cert.c_str());
}
