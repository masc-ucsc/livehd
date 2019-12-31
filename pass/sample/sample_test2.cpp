//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>
#include <unistd.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"


#include "lgraph.hpp"
#include "pass_sample.hpp"

using testing::HasSubstr;

class SampleMainTest : public ::testing::Test {
protected:
  void SetUp() override {
  }
};

TEST_F(SampleMainTest, EmptyLGraph) {

  rmdir("pass_test_lgdb");
  LGraph *g = LGraph::create("pass_test_lgdb", "empty", "nosource");

  Eprp_var var;
  var.add("data","hello");

  EXPECT_FALSE(g->get_library().has_name("pass_sample"));

  Pass_sample::work(var);

  EXPECT_TRUE(g->get_library().has_name("pass_sample"));
}
