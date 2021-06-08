//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "graph_core.hpp"

#include <string>
#include <vector>

#include "boost/graph/adjacency_list.hpp"
#include "boost/graph/graph_utility.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lbench.hpp"
#include "lrand.hpp"

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

TEST_F(Setup_graph_core, trivial_ops_insert1) {
  Graph_core gc("lgdb_graph_core_test","trivial_ops_insert");

  auto m1 = gc.create_node();

  std::vector<uint32_t> nodes;

  for(auto i=0u;i<1'000;++i) {
    auto m = gc.create_node();
    nodes.emplace_back(m);
  }
  std::random_shuffle(nodes.begin(), nodes.end());

  auto n=0u;
  for(const auto &m:nodes) {
    gc.add_edge(m1, m);
    //fmt::print("ADDING {}\n",m);
    //gc.dump(m1);
    ++n;
    EXPECT_EQ(gc.get_num_pin_outputs(m1), n);
    EXPECT_EQ(gc.get_num_pin_inputs(m)  , 1);
  }

  auto m2 = gc.create_node();
  auto m3 = gc.create_node();

  for(auto i=0u;i<1'000;++i) {
    auto m = gc.create_node();
    gc.add_edge(m2, m);
    gc.add_edge(m, m3);
    //fmt::print("ADDING {}\n",m);
    //gc.dump(m2);
    ++n;
    EXPECT_EQ(gc.get_num_pin_outputs(m2), i+1);
    EXPECT_EQ(gc.get_num_pin_inputs(m3),  i+1);
    EXPECT_EQ(gc.get_num_pin_inputs(m),   1);
    EXPECT_EQ(gc.get_num_pin_outputs(m),  1);
  }
}

TEST_F(Setup_graph_core, trivial_ops_insert2) {

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
  for(auto i=0;i<149;++i) {
    auto m = gc.create_node();
    nodes.emplace_back(m);
    gc.add_edge(m1,m);
    EXPECT_EQ(gc.get_num_pin_outputs(m1),i+2); // --
  }

  for(auto i=0;i<70000;++i) { // lots of nodes to force long edges
    gc.create_node();
  }

  for(auto i=0;i<150;++i) {
    auto m = gc.create_node();
    nodes.emplace_back(m);
    gc.add_edge(m1,m);
    EXPECT_EQ(gc.get_num_pin_outputs(m1),i+151); // --
  }

  for(auto i=nodes.size()-1;i>0;--i) {
    gc.add_edge(nodes[i],m3); // add in reverse order
  }
  gc.add_edge(nodes[0],m3); // add in reverse order

  //gc.dump(m1);

  EXPECT_EQ(gc.get_num_pin_inputs(m1),0);
  EXPECT_EQ(gc.get_num_pin_outputs(m1),300); // --
  EXPECT_EQ(gc.get_num_pin_inputs(m2),0);
  EXPECT_EQ(gc.get_num_pin_outputs(m2),0);
  EXPECT_EQ(gc.get_num_pin_inputs(m3),300); // --
  EXPECT_EQ(gc.get_num_pin_outputs(m3),0);
}

TEST_F(Setup_graph_core, delete_edge) {

  Graph_core gc("lgdb_graph_core_test","delete_edge");

  auto m1 = gc.create_node();
  auto m2 = gc.create_node();

  EXPECT_EQ(gc.get_num_pin_outputs(m1),0);
  EXPECT_EQ(gc.get_num_pin_inputs(m2) ,0);

  gc.add_edge(m1,m2);

  EXPECT_EQ(gc.get_num_pin_outputs(m1),1);
  EXPECT_EQ(gc.get_num_pin_inputs(m2) ,1);

  gc.del_edge(m1,m2);

  EXPECT_EQ(gc.get_num_pin_outputs(m1),0);
  EXPECT_EQ(gc.get_num_pin_inputs(m2) ,0);

  Lrand<bool> rbool;

  std::vector<uint32_t> nodes;
  for(auto i=0;i<20000;++i) {
    auto m = gc.create_node();
    nodes.emplace_back(m);
  }
  std::random_shuffle(nodes.begin(), nodes.end());

  std::vector<uint32_t> sink_nodes;
  std::vector<uint32_t> driver_nodes;
  for(const auto &m:nodes) {
    if (rbool.any()) {
      sink_nodes.emplace_back(m);
      gc.add_edge(m1,m);
    }else{
      driver_nodes.emplace_back(m);
      gc.add_edge(m,m1);
    }
  }

  EXPECT_EQ(gc.get_num_pin_outputs(m1),   sink_nodes.size());
  EXPECT_EQ(gc.get_num_pin_inputs (m1), driver_nodes.size());

#if 1
  std::random_shuffle(  sink_nodes.begin(),   sink_nodes.end());
  std::random_shuffle(driver_nodes.begin(), driver_nodes.end());

  //gc.dump(m1);

  while(true) {
    if (sink_nodes.empty() && driver_nodes.empty())
      break;

    auto do_sink = rbool.any() && !sink_nodes.empty();

    if (do_sink || driver_nodes.empty()) {
      auto m = sink_nodes.back();
      sink_nodes.pop_back();
      //fmt::print("DELETING sink node:{}\n", m);
      gc.del_edge(m1,m);
      //gc.dump(m1);
    }else{
      I(!driver_nodes.empty());
      auto m = driver_nodes.back();
      driver_nodes.pop_back();
      //fmt::print("DELETING driver node:{}\n", m);
      gc.del_edge(m,m1);
      //gc.dump(m1);
    }
    //EXPECT_EQ(gc.get_num_pin_outputs(m1),   sink_nodes.size());
    //EXPECT_EQ(gc.get_num_pin_inputs (m1), driver_nodes.size());
  }

  EXPECT_EQ(gc.get_num_pin_outputs(m1), 0);
  EXPECT_EQ(gc.get_num_pin_inputs (m1), 0);
#endif
}

TEST_F(Setup_graph_core, bench_boost) {

  for(auto sz=100u;sz<100'000;sz=sz*10)
  { // test1

    Lbench b("test1_boost_insert_" + std::to_string(sz));

    boost::adjacency_list< boost::vecS, boost::vecS, boost::bidirectionalS, boost::no_property,
      boost::no_property >
        g; // create a boost mutable (adjecency_list)

    auto m1 = boost::add_vertex(g);
    for(auto i=0u;i<sz;++i) {
      auto m = boost::add_vertex(g);
      boost::add_edge(m1, m, g);
    }

    EXPECT_EQ(boost::out_degree(m1, g),sz);
  }

  for(auto sz=100u;sz<100'000;sz=sz*10)
  { // test2

    Lbench b("test2_boost_insert_" + std::to_string(sz));

    boost::adjacency_list< boost::vecS, boost::vecS, boost::bidirectionalS, boost::no_property,
      boost::no_property >
        g; // create a boost mutable (adjecency_list)

    auto m1 = boost::add_vertex(g);
    std::vector<uint32_t> nodes;
    for(auto i=0u;i<sz;++i) {
      auto m = boost::add_vertex(g);
      nodes.emplace_back(m);
      boost::add_edge(m1, m, g);
    }

    EXPECT_EQ(boost::out_degree(m1, g),sz);

    for(const auto &m:nodes) {
      boost::remove_edge(m1, m, g);
    }

    EXPECT_EQ(boost::out_degree(m1, g),0);
  }

}

TEST_F(Setup_graph_core, bench_gc) {

  for(auto sz=100u;sz<100'000;sz=sz*10)
  { // test1
    Lbench b("test1_gc_insert_" + std::to_string(sz));

    Graph_core gc("lgdb_graph_core_test","bench_test1");

    auto m1 = gc.create_node();

    for(auto i=0u;i<sz;++i) {
      auto m = gc.create_node();
      gc.add_edge(m1, m);
    }

    EXPECT_EQ(gc.get_num_pin_outputs(m1),sz);
  }

  for(auto sz=100u;sz<100'000;sz=sz*10)
  { // test2
    Lbench b("test2_gc_insert_" + std::to_string(sz));

    Graph_core gc("lgdb_graph_core_test","bench_test1");

    auto m1 = gc.create_node();

    std::vector<uint32_t> nodes;
    for(auto i=0u;i<sz;++i) {
      auto m = gc.create_node();
      nodes.emplace_back(m);
      gc.add_edge(m1, m);
    }

    EXPECT_EQ(gc.get_num_pin_outputs(m1),sz);

    for(const auto &m:nodes) {
      gc.del_edge(m1, m);
    }

    EXPECT_EQ(gc.get_num_pin_outputs(m1),0);
  }
}
