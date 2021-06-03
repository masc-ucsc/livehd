//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graph_core.hpp"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "graph_core_compress.hpp"
#include "gtest/gtest.h"
#include "lbench.hpp"
#include "lrand.hpp"

#include <stdlib.h>
#include <time.h>
#include <unordered_map>
#include <iostream>


using namespace std;

using testing::HasSubstr;

class Setup_graph_core : public ::testing::Test {
protected:
  void SetUp() override {}

  void TearDown() override {
    // Graph_library::sync_all();
  }
};

TEST_F(Setup_graph_core, trivial_ops) {

  Lrand<int> rnum;
  Lrand<bool> rbool;

  Graph_core gc("lgdb_graph_core_test","trivial_ops");

  EXPECT_TRUE(gc.is_invalid(0));
  EXPECT_TRUE(gc.is_invalid(33));
  EXPECT_TRUE(gc.is_invalid(30));

  std::vector<uint32_t> master_id;
  std::vector<uint32_t> master_root_id;

  auto mid = gc.create_master_root();
  EXPECT_NE(mid, 0);
  EXPECT_FALSE(gc.is_invalid(mid));
  EXPECT_FALSE(gc.is_master(mid));
  EXPECT_TRUE (gc.is_master_root(mid));

  for(int i=0;i<200;++i) {
    if (rbool.any()) {
      auto id = gc.create_master(mid, master_id.size()+1);
      EXPECT_NE(id, 0);
      EXPECT_FALSE(gc.is_invalid(id));
      EXPECT_TRUE(gc.is_master(id));
      EXPECT_FALSE(gc.is_master_root(id));
      master_id.emplace_back(id);
    }else{
      auto id = gc.create_master_root();
      EXPECT_NE(id, 0);
      EXPECT_FALSE(gc.is_invalid(id));
      EXPECT_FALSE(gc.is_master(id));
      EXPECT_TRUE (gc.is_master_root(id));
      master_root_id.emplace_back(id);
    }
  }

  for(auto i=0u;i<master_id.size();++i) {
    auto id = master_id[i];
    EXPECT_NE(id, 0);
    EXPECT_FALSE(gc.is_invalid(id));
    EXPECT_TRUE(gc.is_master(id));
    EXPECT_FALSE(gc.is_master_root(id));
    EXPECT_EQ(gc.get_master_root(id), mid);
    EXPECT_EQ(gc.get_pid(id), i+1);
  }

  for(auto id:master_root_id) {
    EXPECT_NE(id, 0);
    EXPECT_FALSE(gc.is_invalid(id));
    EXPECT_FALSE(gc.is_master(id));
    EXPECT_TRUE (gc.is_master_root(id));
  }

}

