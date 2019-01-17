//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <vector>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "lgraph.hpp"

using testing::HasSubstr;

unsigned int rseed = 123;

class Setup_graphs_test : public ::testing::Test {
protected:
  LGraph *top=0;
  LGraph *c1=0; // Child 1
  LGraph *c3=0; // Child 2
  LGraph *c2=0; // Child 3
  LGraph *gc11=0; // Grand Child from 1, 1st
  LGraph *gc31=0; // Grand child from 3, 1st
  LGraph *gc32=0; // Grand child from 3, 2nd
  LGraph *top2=0;

  absl::flat_hash_map<std::string,int> children;

  void add_child(LGraph *parent, LGraph *child, std::string_view iname, bool randomize) {

    children[absl::StrCat(parent->get_name(), ":", child->get_name())]++;

    auto nid = parent->create_node().get_nid();
    parent->node_subgraph_set(nid, child->lg_id());
    if (rand_r(&rseed)&1 || !randomize)
      parent->set_node_instance_name(nid, iname);
  }

  void add_io(LGraph *g) {
    int inps = rand_r(&rseed) % 4; // 0..3 inputs
    for(int j = 0; j < inps; j++) {
      g->add_graph_input(("i" + std::to_string(j)).c_str(), 0, rand_r(&rseed)&15, 0);
    }
    inps =rand_r(&rseed) % 5; // 0..4 outputs
    for(int j = 0; j < inps; j++) {
      g->add_graph_output(("o" + std::to_string(j)).c_str(), 0, rand_r(&rseed)&15, 0);
    }
  }

  std::vector<LGraph *> lgs;

  void SetUp() override {
    top = LGraph::create("lgraph_each_lgdb", "top", "nosource");
    ASSERT_NE(top,nullptr);
    c1 = LGraph::create("lgraph_each_lgdb", "c1", "nosource");
    ASSERT_NE(c1,nullptr);
    c2 = LGraph::create("lgraph_each_lgdb", "c2", "nosource");
    ASSERT_NE(c2,nullptr);
    c3 = LGraph::create("lgraph_each_lgdb", "c3", "nosource");
    ASSERT_NE(c3,nullptr);
    gc11 = LGraph::create("lgraph_each_lgdb", "gc11", "nosource");
    ASSERT_NE(gc11,nullptr);
    gc31 = LGraph::create("lgraph_each_lgdb", "gc31", "nosource");
    ASSERT_NE(gc31,nullptr);
    gc32 = LGraph::create("lgraph_each_lgdb", "gc32", "nosource");
    ASSERT_NE(gc32,nullptr);
    top2 = LGraph::create("lgraph_each_lgdb", "top2", "nosource");
    ASSERT_NE(top2,nullptr);

    lgs.push_back(top);
    lgs.push_back(c1);
    lgs.push_back(c2);
    lgs.push_back(c3);
    lgs.push_back(gc11);
    lgs.push_back(gc31);
    lgs.push_back(gc32);
    lgs.push_back(top2);

    for(auto &lg:lgs) {
      add_io(lg);
    }

    static bool randomize = false;

    fmt::print("lgraph_each random instance name {}\n",randomize?"set":"not set");

    add_child(top, c1, "ti1", randomize);
    add_child(top, c2, "ti2a", randomize);
    add_child(top, c2, "ti2b", randomize); // 2 instances of c2 in top

    add_child(c1, gc11, "ci1_11a", randomize);
    add_child(c1, gc11, "ci1_11b", randomize);

    add_child(c3, gc31, "ci3_31", randomize);
    add_child(c3, gc32, "ci3_32a", randomize);
    add_child(c3, gc32, "ci3_32b", randomize);

    add_child(top2, c1, "xi1", randomize);
    add_child(top2, c2, "xi2", randomize);
    add_child(top2, c3, "xi3", randomize);

    randomize = !randomize;
  }

  void TearDown() override {
    // No needed to clear/delete every time, but it should work too
    for(auto &lg:lgs) {
      if(lg) lg->close();
      lg = 0;
    }
    lgs.clear();
  }
};

TEST_F(Setup_graphs_test, each_sub_graph) {

  absl::flat_hash_map<std::string,int> children2;

  for(auto &parent:lgs) {
    fmt::print("checking parent:{}\n", parent->get_name());
    parent->each_sub_graph_fast([parent,&children2,this](Index_ID idx, Lg_type_id lgid, std::string_view iname) {
        LGraph *child = LGraph::open(parent->get_path(),lgid);

        ASSERT_NE(child,nullptr);

        fmt::print("parent:{} child:{} iname:{}\n",parent->get_name(), child->get_name(), iname);

        EXPECT_TRUE(children.find(absl::StrCat(parent->get_name(), ":", child->get_name())) != children.end());

        auto id = absl::StrCat(parent->get_name(), ":", child->get_name());
        if (children2.find(id) == children2.end())
          children2[id] = 1;
        else
          children2[id]++;

        child->close();
    });
  }

  for(auto &c:children) {
    EXPECT_EQ(c.second, children2[c.first]);
  }
  for(auto &c:children2) {
    EXPECT_EQ(c.second, children[c.first]);
  }
}

TEST_F(Setup_graphs_test, each_sub_graph_twice) {

  absl::flat_hash_map<std::string,int> children2;

  for(auto &parent:lgs) {
    fmt::print("checking parent:{}\n", parent->get_name());
    parent->each_sub_graph_fast([parent,&children2,this](Index_ID idx, Lg_type_id lgid, std::string_view iname) {
        LGraph *child = LGraph::open(parent->get_path(),lgid);

        ASSERT_NE(child,nullptr);


        EXPECT_TRUE(children.find(absl::StrCat(parent->get_name(), ":", child->get_name())) != children.end());

        auto id = absl::StrCat(parent->get_name(), ":", child->get_name());

        fmt::print("parent:{} child:{} iname:{} id:{}\n",parent->get_name(), child->get_name(), iname, id);

        if (children2.find(id) == children2.end())
          children2[id] = 1;
        else
          children2[id]++;

        child->close();
    });
  }

  for(auto &c:children) {
    EXPECT_EQ(c.second, children2[c.first]);
  }
  for(auto &c:children2) {
    EXPECT_EQ(c.second, children[c.first]);
  }
}

TEST_F(Setup_graphs_test, hierarchy) {

  for(auto &parent:lgs) {
    const auto hier = parent->get_hierarchy();
    fmt::print("hierarchy for {}\n",parent->get_name());
    for(auto &[name,lgid]:hier) {
      fmt::print("  {} {}\n",name,lgid);
    }
  }

  EXPECT_TRUE(true);
}

TEST_F(Setup_graphs_test, hierarchy_twice) {

  for(auto &parent:lgs) {
    const auto hier = parent->get_hierarchy();
    fmt::print("hierarchy for {}\n",parent->get_name());
    for(auto &[name,lgid]:hier) {
      fmt::print("  {} {}\n",name,lgid);
    }
  }

  EXPECT_TRUE(true);
}
