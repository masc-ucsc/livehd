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

    Node node;

    if (child) {
      children[absl::StrCat(parent->get_name(), ":", child->get_name())]++;

      if (rand_r(&rseed)&1) // Should be the same because the lgraph is already created
        node = parent->create_node_sub(child->get_lgid());
      else
        node = parent->create_node_sub(child->get_name());

    }else{
      children[absl::StrCat(parent->get_name(), ":", iname)]++;

      node = parent->create_node_sub(iname);

      auto sub = node.get_type_sub_node();
      auto parent_sub = parent->get_self_sub_node();

      // Match parent names in tmap
      for(const auto &io_pin:parent_sub.get_io_pins()) {
        Port_ID pid;

        if (io_pin.dir == Sub_node::Direction::Input) {
          pid = sub.add_pin(io_pin.name, Sub_node::Direction::Input);

          auto dpin = parent->get_graph_input(io_pin.name);

          dpin.connect_sink(node.setup_sink_pin(pid));

        }else if (io_pin.dir == Sub_node::Direction::Output) {
          pid = sub.add_pin(io_pin.name, Sub_node::Direction::Output);
          auto spin = parent->get_graph_output(io_pin.name);
          if (!spin.get_node().has_inputs()) {
            node.setup_driver_pin(pid).connect_sink(spin);
          }
        }else{
          I(false);// For LGraph sub there should be no undefined iopins
          I(io_pin.graph_io_pid); // graph_io_pid must be defined too
        }
      }
      node.set_type_sub(sub.get_lgid());
    }

    if (rand_r(&rseed)&1 || !randomize)
      node.set_name(iname);
  }

  void add_io(LGraph *g) {
    int inps = rand_r(&rseed) % 4; // 0..3 inputs
    int pos = 0;
    for(int j = 0; j < inps; j++) {
      auto pin = g->add_graph_input(("i" + std::to_string(j)).c_str(), pos++);
      pin.set_bits(rand_r(&rseed)&15);
    }
    inps =rand_r(&rseed) % 5; // 0..4 outputs
    for(int j = 0; j < inps; j++) {
      auto pin = g->add_graph_output(("o" + std::to_string(j)), pos++);
      pin.set_bits(rand_r(&rseed)&15);
    }
  }

  std::vector<LGraph *> lgs;

  void SetUp() override {
    top = LGraph::create("lgdb_lgraph_each", "top", "nosource");
    ASSERT_NE(top,nullptr);
    c1 = LGraph::create("lgdb_lgraph_each", "c1", "nosource");
    ASSERT_NE(c1,nullptr);
    c2 = LGraph::create("lgdb_lgraph_each", "c2", "nosource");
    ASSERT_NE(c2,nullptr);
    c3 = LGraph::create("lgdb_lgraph_each", "c3", "nosource");
    ASSERT_NE(c3,nullptr);
    gc11 = LGraph::create("lgdb_lgraph_each", "gc11", "nosource");
    ASSERT_NE(gc11,nullptr);
    gc31 = LGraph::create("lgdb_lgraph_each", "gc31", "nosource");
    ASSERT_NE(gc31,nullptr);
    gc32 = LGraph::create("lgdb_lgraph_each", "gc32", "nosource");
    ASSERT_NE(gc32,nullptr);
    top2 = LGraph::create("lgdb_lgraph_each", "top2", "nosource");
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

    add_child(top, nullptr, "tmap1", randomize);

    add_child(c1, gc11, "ci1_11a", randomize);
    add_child(c1, nullptr, "tmap1", randomize);
    add_child(c1, nullptr, "tmap2", randomize);
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
    for(auto *lg:lgs) {
      delete lg;
    }
    lgs.clear();
  }
};

TEST_F(Setup_graphs_test, each_sub_graph) {

  absl::flat_hash_map<std::string,int> children2;

  for(auto &parent:lgs) {
    fmt::print("checking parent:{}\n", parent->get_name());
    parent->each_sub_fast([parent,&children2,this](Node &node) {
        LGraph *child = LGraph::open(parent->get_path(),node.get_type_sub());

        ASSERT_NE(child,nullptr);

        std::string_view iname = "NONAME";
        if (node.has_name())
          iname = node.get_name();

        fmt::print("parent:{} child:{} iname:{}\n",parent->get_name(), child->get_name(), iname);

        EXPECT_TRUE(children.find(absl::StrCat(parent->get_name(), ":", child->get_name())) != children.end());

        auto id = absl::StrCat(parent->get_name(), ":", child->get_name());
        if (children2.find(id) == children2.end())
          children2[id] = 1;
        else
          children2[id]++;

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
    parent->each_sub_fast([parent,&children2,this](Node &node) {
        LGraph *child = LGraph::open(parent->get_path(),node.get_type_sub());

        ASSERT_NE(child,nullptr);

        EXPECT_TRUE(children.find(absl::StrCat(parent->get_name(), ":", child->get_name())) != children.end());

        auto id = absl::StrCat(parent->get_name(), ":", child->get_name());

        std::string_view iname = "NONAME";
        if (node.has_name())
          iname = node.get_name();
        fmt::print("parent:{} child:{} iname:{} id:{}\n",parent->get_name(), child->get_name(), iname, id);

        if (children2.find(id) == children2.end())
          children2[id] = 1;
        else
          children2[id]++;

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

TEST_F(Setup_graphs_test, No_each_input) {

  for(auto &parent:lgs) {
    parent->each_graph_input([](Node_pin &pin) {
      EXPECT_TRUE(pin.is_graph_input());
      EXPECT_TRUE(!pin.is_graph_output());
      EXPECT_FALSE(pin.get_node().has_inputs());
    });
  }

  EXPECT_TRUE(true);
}

TEST_F(Setup_graphs_test, No_each_output) {

  for(auto &parent:lgs) {
    parent->each_graph_output([](Node_pin &pin) {
      EXPECT_TRUE(!pin.is_graph_input());
      EXPECT_TRUE(pin.is_graph_output());
      EXPECT_FALSE(pin.get_node().has_outputs());
    });
  }

  EXPECT_TRUE(true);
}

