#include <string>

#include "pass_sample.hpp"

#include "gtest/gtest.h"
#include <gmock/gmock.h>

using testing::HasSubstr;

class SampleMainTest : public ::testing::Test {
protected:
  void SetUp() override {
  }
};


TEST_F(SampleMainTest, EmptyLGraph) {

  LGraph *g = LGraph::create("pass_test_lgdb", "empty");

  Pass_sample pass;

  pass.trans(g);

  EXPECT_TRUE(true);
}

