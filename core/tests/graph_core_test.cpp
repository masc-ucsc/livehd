//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <vector>
#include <string>

#include "lbench.hpp"
#include "lrand.hpp"

#include "graph_core.hpp"

using testing::HasSubstr;

class Setup_graph_core : public ::testing::Test {
protected:

  void SetUp() override {
  }

  void TearDown() override {
    //Graph_library::sync_all();
  }
};

TEST_F(Setup_graph_core, shallow_tree) {
  Lbench b("shallow_tree");

  Graph_core c1("lgdb_gc","shallow_tree");

  // TEST now
}

