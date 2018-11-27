
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <string>

#include "lgraph.hpp"
#include "pass_sample.hpp"

using testing::HasSubstr;

class SampleMainTest : public ::testing::Test {
protected:
  void SetUp() override {
  }
};

TEST_F(SampleMainTest, EmptyLGraph) {

  LGraph *g = LGraph::create("pass_test_lgdb", "empty");

  Pass_sample pass;

  pass.do_work(g);

  EXPECT_TRUE(true);
}
