//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <vector>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

using testing::HasSubstr;

unsigned int rseed = 123;

class Setup_graphs_test : public ::testing::Test {
protected:
  LGraph *top=0;
  LGraph *c1=0;
  LGraph *c2=0;

  absl::flat_hash_map<std::string,int> children;

  std::vector<LGraph *> lgs;

  void SetUp() override {
    top = LGraph::create("lgdb_node_test", "top", "nosource");
    ASSERT_NE(top,nullptr);
    c1 = LGraph::create("lgdb_node_test", "c1", "nosource");
    ASSERT_NE(c1,nullptr);
    c2 = LGraph::create("lgdb_node_test", "c2", "nosource");
    ASSERT_NE(c2,nullptr);

    //---------------------------------------------------
    // Create graphs input/outputs
    auto top_a = top->add_graph_input("a", 10, 0);
    auto top_b = top->add_graph_input("b", 10, 0);
    auto top_z = top->add_graph_output("z", 10, 0);
    auto top_y = top->add_graph_output("Y", 10, 0);
    auto top_s2_out = top->add_graph_output("s2_out", 10, 0);

    auto c1_aaa = c1->add_graph_input("an_input", 10, 0);
    auto c1_sss = c1->add_graph_output("s1_output", 10, 0);

    auto c2_aaa = c2->add_graph_input("a1", 10, 0);
    auto c2_bbb = c2->add_graph_input("anotherinput", 10, 0);
    auto c2_sss = c2->add_graph_output("Y", 10, 0);

    //---------------------------------------------------
    // populate top graph with cells and instances

    auto s1 = top->create_node_sub(c1->lg_id());
    auto s2 = top->create_node_sub(c2->lg_id());
    auto sum = top->create_node(Sum_Op);
    auto mux = top->create_node(Mux_Op);
    auto mor = top->create_node(Xor_Op); // cell called mor to avoid xor reserved keyword

    auto s1_aaa = s1.setup_sink_pin("an_input");
    auto s1_sss = s1.setup_driver_pin("s1_output");

    I(s1_aaa.get_pid() == c1_aaa.get_pid());
    I(s1_sss.get_pid() == c1_sss.get_pid());

    auto s2_aaa = s2.setup_sink_pin("a1");
    auto s2_bbb = s2.setup_sink_pin("anotherinput");
    auto s2_sss = s2.setup_driver_pin("Y");
    I(s2_aaa.get_pid() == c2_aaa.get_pid());
    I(s2_bbb.get_pid() == c2_bbb.get_pid());
    I(s2_sss.get_pid() == c2_sss.get_pid());

    auto sum_a = sum.setup_sink_pin("AU");
    auto sum_b = sum.setup_sink_pin("BU");
    auto sum_y = sum.setup_driver_pin("Y");

    auto mor_a = mor.setup_sink_pin("A");
    auto mor_y = mor.setup_driver_pin("Y");

    auto mux_a = mux.setup_sink_pin("A");
    auto mux_b = mux.setup_sink_pin("B");
    auto mux_s = mux.setup_sink_pin("S");
    auto mux_y = mux.setup_driver_pin("Y");

    top->add_edge(top_a, sum_a);
    top->add_edge(top_b, sum_b);

    top->add_edge(top_a, mor_a);
    top->add_edge(top_b, mor_a);

    top->add_edge(top_a, s1_aaa);

    top->add_edge(top_a , s2_aaa);
    top->add_edge(s1_sss, s2_bbb);
    top->add_edge(s1_sss, top_z);

    top->add_edge(sum_y , mux_b);
    top->add_edge(s2_sss, mux_s);
    top->add_edge(mor_y , mux_a);

    top->add_edge(mux_y  , top_y);
    top->add_edge(s2_sss , top_s2_out);
  }

  void TearDown() override {
  }
};

TEST_F(Setup_graphs_test, each_sub_graph) {

  for(const auto &nid:top->forward()) {
    auto node = top->get_node(nid);
    for(const auto &out_edge : top->out_edges(nid)) {
      auto dpin = out_edge.get_out_pin();
      auto spin = out_edge.get_inp_pin();
      fmt::print("idx:{} pid:{} -> idx:{} pid:{}\n",dpin.get_idx(), dpin.get_pid(), spin.get_idx(), spin.get_pid());
    }
  }

  EXPECT_TRUE(true);
}

