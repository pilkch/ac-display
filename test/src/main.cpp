#include "gnutlsmm.h"

// gtest headers
#include <gtest/gtest.h>

int main(int argc, char** argv)
{
  gnutlsmm::helper gnutlsHelper;

  ::testing::InitGoogleTest(&argc, argv);

  const int result = RUN_ALL_TESTS();

  return result;
}
