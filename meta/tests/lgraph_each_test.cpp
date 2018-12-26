
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <vector>
#include <string>

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

  std::map<std::string,int> children;

  void add_child(LGraph *parent, LGraph *child, const std::string &iname) {

    children[parent->get_name() + ":" + child->get_name()]++;

    auto nid = parent->create_node().get_nid();
    parent->node_subgraph_set(nid, child->lg_id());
    if (rand_r(&rseed)&1)
      parent->set_node_instance_name(nid, iname);

#if 0
    // Nodes are disconnected in this test. No need and unclear how to connect randomly created subgraphs
    child->each_input([child,parent](Index_ID idx) {
        const char *iname = child->get_graph_input_name(idx);
        auto dst_pid = sub_graph->get_graph_input(iname).get_pid();

        Node_Pin dst_pin(nid, dst_pid, true);
    });
#endif

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

    lgs.push_back(top);
    lgs.push_back(c1);
    lgs.push_back(c2);
    lgs.push_back(c3);
    lgs.push_back(gc11);
    lgs.push_back(gc31);
    lgs.push_back(gc32);

    for(auto &lg:lgs) {
      add_io(lg);
    }

    add_child(top, c1, "ti1");
    add_child(top, c2, "ti2a");
    add_child(top, c2, "ti2b"); // 2 instances of c2 in top

    add_child(c1, gc11, "ci1_11a");
    add_child(c1, gc11, "ci1_11b");

    add_child(c3, gc31, "ci3_31");
    add_child(c3, gc32, "ci3_32a");
    add_child(c3, gc32, "ci3_32b");
  }

  void TearDown() override {
    for(auto &lg:lgs) {
      if(lg) lg->close();
      lg = 0;
    }
  }
};

TEST_F(Setup_graphs_test, EmptyLGraph) {

  std::map<std::string,int> children2;

  for(auto &parent:lgs) {
    fmt::print("checking parent:{}\n", parent->get_name());
    parent->each_sub_graph_fast([parent,&children2,this](Index_ID idx, Lg_type_id lgid, const std::string &iname) {
        LGraph *child = LGraph::open(parent->get_path(),lgid);

        ASSERT_NE(child,nullptr);

        fmt::print("parent:{} child:{} iname:{}\n",parent->get_name(), child->get_name(), iname);

        EXPECT_TRUE(children.find(parent->get_name() + ":" + child->get_name()) != children.end());

        children2[parent->get_name() + ":" + child->get_name()]++;
    });
  }

  EXPECT_TRUE(std::equal(children.begin(), children.end(), children2.begin()));
}

