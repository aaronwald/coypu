//http://www.alittlemadness.com/2009/03/31/c-unit-testing-with-boosttest/

#include "gtest/gtest.h"
#include "gmock/gmock.h"

int main(int argc, char **argv) {
  //::testing::InitGoogleTest(&argc, argv);
  ::testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
