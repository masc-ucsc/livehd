#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <format>
#include <iostream>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "label_acyclic.hpp"
#include "lgraph.hpp"
#include "lrand.hpp"

// Defined with #ifdef
// #define RUN 1
#define GENERIC_CHECK 1
#define DEBUG         1

// Defined with #if
#define TEST1 1
#define TEST2 1
#define TEST3 1

class Label_acyclic_test : public ::testing::Test {
public:
  void SetUp() override {}
  void TearDown() override {}
};

#if TEST1
TEST_F(Label_acyclic_test, simple_graph_no_loop) {
  auto   *lib     = Graph_library::instance("lgdb");
  Lgraph *a_graph = lib->create_lgraph("a_graph", "-");

  ASSERT_NE(a_graph, nullptr);

  auto          verbose  = false;
  auto          hier     = false;
  auto          cutoff   = 1;
  auto          merge_en = true;
  Label_acyclic labeler(verbose, hier, cutoff, merge_en);

  // input/output
  auto graph_inp_A = a_graph->add_graph_input("a_graph_in", 1, 10);
  auto graph_inp_B = a_graph->add_graph_input("b_graph_in", 3, 10);
  auto graph_inp_C = a_graph->add_graph_input("c_graph_in", 5, 10);
  auto graph_out_Y = a_graph->add_graph_output("d_graph_in", 9, 10);

  // create nodes
  auto sum_node       = a_graph->create_node(Ntype_op::Sum);
  auto sum_node_inp_A = sum_node.setup_sink_pin(std::string_view("A"));
  auto sum_node_inp_B = sum_node.setup_sink_pin(std::string_view("B"));
  auto sum_node_out_Y = sum_node.setup_driver_pin(std::string_view("Y"));

  auto mux_node       = a_graph->create_node(Ntype_op::Mux);
  auto mux_node_inp_A = mux_node.setup_sink_pin(std::string_view("1"));
  auto mux_node_inp_B = mux_node.setup_sink_pin(std::string_view("2"));
  auto mux_node_inp_S = mux_node.setup_sink_pin(std::string_view("3"));
  auto mux_node_out_Y = mux_node.setup_driver_pin(std::string_view("Y"));

  auto xor_node       = a_graph->create_node(Ntype_op::Xor);
  auto xor_node_inp_A = xor_node.setup_sink_pin();
  auto xor_node_out_Y = xor_node.setup_driver_pin(std::string_view("Y"));

  //---------------------------------------------------
  // creating edges
  a_graph->add_edge(graph_inp_A, sum_node_inp_A, 10);  // input a -> Sum input
  a_graph->add_edge(graph_inp_B, sum_node_inp_B, 10);  // input b -> Sum input

  a_graph->add_edge(graph_inp_A, xor_node_inp_A, 10);  // input a -> Xor input
  a_graph->add_edge(graph_inp_B, xor_node_inp_A, 10);  // input a -> Xor input

  // Edges into mux node
  a_graph->add_edge(sum_node_out_Y, mux_node_inp_B, 10);  // Sum output -> Mux input
  a_graph->add_edge(xor_node_out_Y, mux_node_inp_A, 10);  // Xor output -> Mux input
  a_graph->add_edge(graph_inp_C, mux_node_inp_S, 1);      // input c -> Mux (sel) input

  // overall output
  a_graph->add_edge(mux_node_out_Y, graph_out_Y, 10);  // Mux output -> overall output

  labeler.label(a_graph);
  for (const auto &n : a_graph->forward(hier)) {
    ASSERT_EQ(0, static_cast<int>(n.get_color()));
#ifdef DEBUG
    std::print("Node Name:{} , Node Color:{}\n", n.debug_name(), n.get_color());
#endif
  }
}
#endif

#if TEST2
TEST_F(Label_acyclic_test, simple_graph_loop) {
  auto   *lib     = Graph_library::instance("lgdb");
  Lgraph *b_graph = lib->create_lgraph("b_graph", "-");
  ASSERT_NE(b_graph, nullptr);
  std::vector<int>                      expected_gen = {3, 0, 0, 0, 0, 3};
  absl::flat_hash_map<std::string, int> expected;
  expected["n18_mux_lgb_graph"] = 3;
  expected["n22_xor_lgb_graph"] = 0;
  expected["n12_sum_lgb_graph"] = 0;
  expected["n10_sum_lgb_graph"] = 0;
  expected["n14_mux_lgb_graph"] = 0;
  expected["n23_xor_lgb_graph"] = 3;

  auto          verbose  = false;
  auto          hier     = false;
  auto          cutoff   = 1;
  auto          merge_en = true;
  Label_acyclic labeler(verbose, hier, cutoff, merge_en);

  // input/output
  auto graph_inp_A = b_graph->add_graph_input("a_graph_in", 1, 10);
  auto graph_inp_B = b_graph->add_graph_input("b_graph_in", 3, 10);
  auto graph_inp_C = b_graph->add_graph_input("c_graph_in", 5, 1);
  auto graph_out_Y = b_graph->add_graph_output("y_graph_out", 9, 10);

  auto graph_inp_D = b_graph->add_graph_input("d_graph_in", 11, 10);
  auto graph_inp_E = b_graph->add_graph_input("e_graph_in", 13, 10);
  auto graph_inp_F = b_graph->add_graph_input("f_graph_in", 15, 1);

  // create nodes
  auto sum_node       = b_graph->create_node(Ntype_op::Sum);
  auto sum_node_inp_A = sum_node.setup_sink_pin(std::string_view("A"));
  auto sum_node_inp_B = sum_node.setup_sink_pin(std::string_view("B"));
  auto sum_node_out_Y = sum_node.setup_driver_pin(std::string_view("Y"));

  auto sum_node2       = b_graph->create_node(Ntype_op::Sum);
  auto sum_node2_inp_A = sum_node2.setup_sink_pin(std::string_view("A"));
  auto sum_node2_inp_B = sum_node2.setup_sink_pin(std::string_view("B"));
  auto sum_node2_out_Y = sum_node2.setup_driver_pin(std::string_view("Y"));

  auto mux_node       = b_graph->create_node(Ntype_op::Mux);
  auto mux_node_inp_A = mux_node.setup_sink_pin(std::string_view("1"));
  auto mux_node_inp_B = mux_node.setup_sink_pin(std::string_view("2"));
  auto mux_node_inp_S = mux_node.setup_sink_pin(std::string_view("3"));
  auto mux_node_out_Y = mux_node.setup_driver_pin(std::string_view("Y"));

  auto mux_node2       = b_graph->create_node(Ntype_op::Mux);
  auto mux_node2_inp_A = mux_node2.setup_sink_pin(std::string_view("1"));
  auto mux_node2_inp_B = mux_node2.setup_sink_pin(std::string_view("2"));
  auto mux_node2_inp_S = mux_node2.setup_sink_pin(std::string_view("3"));
  auto mux_node2_out_Y = mux_node2.setup_driver_pin(std::string_view("Y"));

  auto xor_node       = b_graph->create_node(Ntype_op::Xor);
  auto xor_node_inp_A = xor_node.setup_sink_pin();
  auto xor_node_out_Y = xor_node.setup_driver_pin(std::string_view("Y"));

  auto xor_node2       = b_graph->create_node(Ntype_op::Xor);
  auto xor_node2_inp_A = xor_node2.setup_sink_pin();

  //---------------------------------------------------
  // creating edges
  b_graph->add_edge(sum_node2_out_Y, sum_node_inp_A, 10);
  b_graph->add_edge(graph_inp_B, sum_node_inp_B, 10);  // input b -> Sum1 input

  b_graph->add_edge(graph_inp_A, sum_node2_inp_B, 10);
  b_graph->add_edge(mux_node_out_Y, sum_node2_inp_A, 10);  // Mux output -> overall output

  b_graph->add_edge(graph_inp_A, xor_node_inp_A, 10);  // input a -> Xor input
  b_graph->add_edge(graph_inp_B, xor_node_inp_A, 10);  // input a -> Xor input

  b_graph->add_edge(sum_node_out_Y, mux_node_inp_B, 10);  // Sum output -> Mux input
  b_graph->add_edge(xor_node_out_Y, mux_node_inp_A, 10);  // Xor output -> Mux input
  b_graph->add_edge(graph_inp_C, mux_node_inp_S, 1);      // input c -> Mux (sel) input

  b_graph->add_edge(graph_inp_D, mux_node2_inp_A, 10);
  b_graph->add_edge(graph_inp_E, mux_node2_inp_B, 10);
  b_graph->add_edge(graph_inp_F, mux_node2_inp_S, 1);

  b_graph->add_edge(mux_node2_out_Y, xor_node2_inp_A, 10);
  b_graph->add_edge(mux_node2_out_Y, xor_node2_inp_A, 10);

  // overall output
  b_graph->add_edge(mux_node_out_Y, graph_out_Y, 10);  // Mux output -> overall output

  labeler.label(b_graph);

#ifdef DEBUG
  for (const auto &n : b_graph->forward(hier)) {
    std::print("Node Name:{} , Node Color:{}\n", n.debug_name(), n.get_color());
  }
#endif

#ifdef GENERIC_CHECK
  int i = 0;
  for (const auto &n : b_graph->forward(hier)) {
    ASSERT_EQ(expected_gen[i], static_cast<int>(n.get_color()));
    i++;
  }
#else
  for (const auto &n : b_graph->forward(hier)) {
    ASSERT_EQ(expected[n.debug_name()], static_cast<int>(n.get_color()));
  }
#endif
}

#endif

#if TEST3
TEST_F(Label_acyclic_test, essent_test) {
  auto   *lib     = Graph_library::instance("lgdb");
  Lgraph *c_graph = lib->create_lgraph("c_graph", "-");
  ASSERT_NE(c_graph, nullptr);
  std::vector<int>                      expected_gen = {1, 1, 3, 1, 1, 1, 1, 3};
  absl::flat_hash_map<std::string, int> expected;
  expected["n6_xor_lgc_graph"]  = 1;
  expected["n7_xor_lgc_graph"]  = 1;
  expected["n9_xor_lgc_graph"]  = 3;
  expected["n11_xor_lgc_graph"] = 1;
  expected["n4_xor_lgc_graph"]  = 1;
  expected["n5_xor_lgc_graph"]  = 1;
  expected["n8_xor_lgc_graph"]  = 1;
  expected["n10_xor_lgc_graph"] = 3;

  auto          verbose  = false;
  auto          hier     = false;
  auto          cutoff   = 1;
  auto          merge_en = true;
  Label_acyclic labeler(verbose, hier, cutoff, merge_en);

  // input/output
  auto graph_inp_A = c_graph->add_graph_input("a_graph_in", 1, 10);
  // auto graph_out_Y = c_graph->add_graph_output("Y", 3, 10);

  // create nodes
  auto xor_node1       = c_graph->create_node(Ntype_op::Xor);
  auto xor_node1_inp_A = xor_node1.setup_sink_pin();
  auto xor_node1_out_Y = xor_node1.setup_driver_pin(std::string_view("Y"));

  auto xor_node2       = c_graph->create_node(Ntype_op::Xor);
  auto xor_node2_inp_A = xor_node2.setup_sink_pin();
  // auto xor_node2_out_Y = xor_node2.setup_driver_pin("Y");

  auto xor_node3 = c_graph->create_node(Ntype_op::Xor);
  // auto xor_node3_inp_A = xor_node3.setup_sink_pin("A");
  auto xor_node3_out_Y = xor_node3.setup_driver_pin(std::string_view("Y"));

  auto xor_node4       = c_graph->create_node(Ntype_op::Xor);
  auto xor_node4_inp_A = xor_node4.setup_sink_pin();
  auto xor_node4_out_Y = xor_node4.setup_driver_pin(std::string_view("Y"));

  auto xor_node5       = c_graph->create_node(Ntype_op::Xor);
  auto xor_node5_inp_A = xor_node5.setup_sink_pin();
  // auto xor_node5_out_Y = xor_node5.setup_driver_pin("Y");

  auto xor_node6 = c_graph->create_node(Ntype_op::Xor);
  // auto xor_node6_inp_A = xor_node6.setup_sink_pin("A");
  auto xor_node6_out_Y = xor_node6.setup_driver_pin(std::string_view("Y"));

  auto xor_node7       = c_graph->create_node(Ntype_op::Xor);
  auto xor_node7_inp_A = xor_node7.setup_sink_pin();
  // auto xor_node7_out_Y = xor_node7.setup_driver_pin("Y");

  auto xor_node0       = c_graph->create_node(Ntype_op::Xor);
  auto xor_node0_inp_A = xor_node0.setup_sink_pin();
  auto xor_node0_out_Y = xor_node0.setup_driver_pin(std::string_view("Y"));

  // creating edges
  c_graph->add_edge(graph_inp_A, xor_node0_inp_A, 10);  // input a -> Xor input
  // c_graph->add_edge(graph_inp_A, xor_node6_inp_A, 10); // input a -> Xor input

  c_graph->add_edge(xor_node0_out_Y, xor_node1_inp_A, 10);  // 0 -> 1
  c_graph->add_edge(xor_node1_out_Y, xor_node2_inp_A, 10);  // 1 -> 2
  c_graph->add_edge(xor_node3_out_Y, xor_node4_inp_A, 10);  // 3 -> 4
  c_graph->add_edge(xor_node4_out_Y, xor_node5_inp_A, 10);  // 4 -> 5
  c_graph->add_edge(xor_node4_out_Y, xor_node2_inp_A, 10);  // 4 -> 2
  c_graph->add_edge(xor_node6_out_Y, xor_node7_inp_A, 10);  // 6 -> 7

  labeler.label(c_graph);

#ifdef DEBUG
  for (const auto &n : c_graph->forward(hier)) {
    std::print("Node Name:{} , Node Color:{}\n", n.debug_name(), n.get_color());
  }
#endif

#ifdef GENERIC_CHECK
  int i = 0;
  for (const auto &n : c_graph->forward(hier)) {
    ASSERT_EQ(expected_gen[i], static_cast<int>(n.get_color()));
    i++;
  }
#else
  for (const auto &n : c_graph->forward(hier)) {
    ASSERT_EQ(expected[n.debug_name()], static_cast<int>(n.get_color()));
  }
#endif
}

#endif

#ifdef RUN
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#else
int main(int argc, char **argv) { return 0; }
#endif
