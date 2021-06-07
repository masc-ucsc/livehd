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

  std::vector<uint32_t> pin_id;
  std::vector<uint32_t> node_id;

  auto mid = gc.create_node();
  EXPECT_NE(mid, 0);
  EXPECT_FALSE(gc.is_invalid(mid));
  EXPECT_FALSE(gc.is_pin(mid));
  EXPECT_TRUE (gc.is_node(mid));

  for(int i=0;i<200;++i) {
    if (rbool.any()) {
      auto id = gc.create_pin(mid, pin_id.size()+1);
      EXPECT_NE(id, 0);
      EXPECT_FALSE(gc.is_invalid(id));
      EXPECT_TRUE(gc.is_pin(id));
      EXPECT_FALSE(gc.is_node(id));
      pin_id.emplace_back(id);
    }else{
      auto id = gc.create_node();
      EXPECT_NE(id, 0);
      EXPECT_FALSE(gc.is_invalid(id));
      EXPECT_FALSE(gc.is_pin(id));
      EXPECT_TRUE (gc.is_node(id));
      node_id.emplace_back(id);
    }
  }

  for(auto i=0u;i<pin_id.size();++i) {
    auto id = pin_id[i];
    EXPECT_NE(id, 0);
    EXPECT_FALSE(gc.is_invalid(id));
    EXPECT_TRUE(gc.is_pin(id));
    EXPECT_FALSE(gc.is_node(id));
    EXPECT_EQ(gc.get_node(id), mid);
    EXPECT_EQ(gc.get_pid(id), i+1);
  }

  for(auto id:node_id) {
    EXPECT_NE(id, 0);
    EXPECT_FALSE(gc.is_invalid(id));
    EXPECT_FALSE(gc.is_pin(id));
    EXPECT_TRUE (gc.is_node(id));
  }
}


TEST_F(Setup_graph_core, trivial_ops_insert) {

  Graph_core gc("lgdb_graph_core_test","trivial_ops_insert");

  auto m1 = gc.create_node();
  auto m2 = gc.create_node();
  auto m3 = gc.create_node();

  EXPECT_FALSE(gc.has_edges(m1));
  EXPECT_FALSE(gc.has_edges(m2));
  EXPECT_FALSE(gc.has_edges(m3));

  EXPECT_EQ(gc.get_num_pin_inputs(m1),0);
  EXPECT_EQ(gc.get_num_pin_outputs(m1),0);
  EXPECT_EQ(gc.get_num_pin_inputs(m2),0);
  EXPECT_EQ(gc.get_num_pin_outputs(m2),0);
  EXPECT_EQ(gc.get_num_pin_inputs(m3),0);
  EXPECT_EQ(gc.get_num_pin_outputs(m3),0);

  gc.add_edge(m1,m3);

  EXPECT_TRUE(gc.has_edges(m1));
  EXPECT_FALSE(gc.has_edges(m2));
  EXPECT_TRUE(gc.has_edges(m3));

  EXPECT_EQ(gc.get_num_pin_inputs(m1),0);
  EXPECT_EQ(gc.get_num_pin_outputs(m1),1); // --
  EXPECT_EQ(gc.get_num_pin_inputs(m2),0);
  EXPECT_EQ(gc.get_num_pin_outputs(m2),0);
  EXPECT_EQ(gc.get_num_pin_inputs(m3),1); // --
  EXPECT_EQ(gc.get_num_pin_outputs(m3),0);

  std::vector<uint32_t> nodes;
  for(auto i=0;i<150;++i) {
    auto m = gc.create_node();
    nodes.emplace_back(m);
    gc.add_edge(m1,m);
  }

  for(auto i=0;i<70000;++i) { // lots of nodes to force long edges
    gc.create_node();
  }

  for(auto i=0;i<149;++i) {
    auto m = gc.create_node();
    nodes.emplace_back(m);
    gc.add_edge(m1,m);
  }

  for(auto i=nodes.size()-1;i>0;--i) {
    gc.add_edge(nodes[i],m3); // add in reverse order
  }
  gc.add_edge(nodes[0],m3); // add in reverse order

  gc.dump(m1);

  EXPECT_EQ(gc.get_num_pin_inputs(m1),0);
  EXPECT_EQ(gc.get_num_pin_outputs(m1),300); // --
  EXPECT_EQ(gc.get_num_pin_inputs(m2),0);
  EXPECT_EQ(gc.get_num_pin_outputs(m2),0);
  EXPECT_EQ(gc.get_num_pin_inputs(m3),300); // --
  EXPECT_EQ(gc.get_num_pin_outputs(m3),0);
}
