
#include <string>
#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "pass_sample.hpp"
#include "lgraph.hpp"


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

