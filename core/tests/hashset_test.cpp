
#include "fmt/format.h"
#include "gtest/gtest.h"

#include "hashset.hpp"

class GTest1 : public ::testing::Test {
protected:
  void SetUp() override {}
};


TEST_F(GTest1, trivial) {
  Hashset h1;

  h1.insert(10);
  h1.insert(20);
  h1.insert(30);

  Hashset h2;

  h2.insert(10);
  h2.insert(30);
  h2.insert(20);

  Hashset h3;

  h3.insert(1);
  h3.insert(30);
  h3.insert(20);

  EXPECT_EQ(h1.get_value(), h2.get_value());
  EXPECT_NE(h1.get_value(), h3.get_value());
}

