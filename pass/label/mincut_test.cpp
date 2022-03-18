#include "label_mincut.hpp"

#include <strings.h>
#include <unistd.h>
#include <stdlib.h>

#include <iostream>
#include <vector>

#include "fmt/format.h"
#include "gtest/gtest.h"
#include "lbench.hpp"
#include "lrand.hpp"
#include "gmock/gmock.h"
#include "lgraph.hpp"

#define RUN 0

#define TEST1 0
#define TEST2 0
#define TEST3 0

//#define DEBUG 0

class Label_mincut_test : public ::testing::Test { 
  public: 
    void SetUp() override { }
    void TearDown() override { }
};


#if TEST1
TEST_F(Label_mincut_test, test1) { 
  Lgraph* a_graph = Lgraph::create("lgdb", "a_graph", "-");
  ASSERT_NE(a_graph, nullptr);

  auto verbose = false;
  auto hier = false;
  auto iters = 1;
  auto seed = 0;
  std::string_view alg("vc");
  Label_mincut labeler(verbose, hier, iters, seed, alg);

  // input/output
  auto graph_inp_A = a_graph->add_graph_input("A", 1, 10);
  auto graph_inp_B = a_graph->add_graph_input("B", 3, 10);
  auto graph_inp_C = a_graph->add_graph_input("C", 5, 10);
  auto graph_out_Y = a_graph->add_graph_output("Y", 9, 10);
 
  // create nodes
  auto sum_node = a_graph->create_node(Ntype_op::Sum);
  auto sum_node_inp_A = sum_node.setup_sink_pin("A");
  auto sum_node_inp_B = sum_node.setup_sink_pin("B");
  auto sum_node_out_Y = sum_node.setup_driver_pin("Y");
  
  auto mux_node = a_graph->create_node(Ntype_op::Mux);
  auto mux_node_inp_A = mux_node.setup_sink_pin("A");
  auto mux_node_inp_B = mux_node.setup_sink_pin("B");
  auto mux_node_inp_S = mux_node.setup_sink_pin("S");
  auto mux_node_out_Y = mux_node.setup_driver_pin("Y");
 
  auto xor_node = a_graph->create_node(Ntype_op::Xor);
  auto xor_node_inp_A = xor_node.setup_sink_pin("A");
  auto xor_node_out_Y = xor_node.setup_driver_pin("Y");

  //---------------------------------------------------
  // creating edges
  a_graph->add_edge(graph_inp_A, sum_node_inp_A, 10); // input a -> Sum input
  a_graph->add_edge(graph_inp_B, sum_node_inp_B, 10); // input b -> Sum input
  
  a_graph->add_edge(graph_inp_A, xor_node_inp_A, 10); // input a -> Xor input
  a_graph->add_edge(graph_inp_B, xor_node_inp_A, 10); // input a -> Xor input
 
  // Edges into mux node 
  a_graph->add_edge(sum_node_out_Y, mux_node_inp_B, 10); // Sum output -> Mux input
  a_graph->add_edge(xor_node_out_Y, mux_node_inp_A, 10); // Xor output -> Mux input
  a_graph->add_edge(graph_inp_C, mux_node_inp_S, 1);  // input c -> Mux (sel) input

  // overall output  
  a_graph->add_edge(mux_node_out_Y, graph_out_Y, 10); // Mux output -> overall output
                                                      //
  labeler.label(a_graph);
  for (const auto &n : a_graph->forward(hier)) {
    //ASSERT_EQ(0, static_cast<int>(n.get_color()));
#ifdef DEBUG
    fmt::print("Node Name:{} , Node Color:{}\n", n.debug_name(), n.get_color()); 
#endif
  }
}
#endif


#if TEST2
TEST_F(Label_mincut_test, test2) {
  Lgraph* b_graph = Lgraph::create("lgdb", "b_graph", "-");
  ASSERT_NE(b_graph, nullptr);

  auto verbose = false;
  auto hier = false;
  auto iters = 1;
  auto seed = 0;
  std::string_view alg("vc");
  Label_mincut labeler(verbose, hier, iters, seed, alg);

  // input/output
  auto graph_inp_A = b_graph->add_graph_input("A", 1, 10);
  auto graph_inp_B = b_graph->add_graph_input("B", 3, 10);
  auto graph_inp_C = b_graph->add_graph_input("C", 5, 1);
  auto graph_out_Y = b_graph->add_graph_output("Y", 9, 10);
  
  auto graph_inp_D = b_graph->add_graph_input("A", 11, 10);
  auto graph_inp_E = b_graph->add_graph_input("B", 13, 10);
  auto graph_inp_F = b_graph->add_graph_input("C", 15, 1);
 
  // create nodes
  auto sum_node = b_graph->create_node(Ntype_op::Sum);
  auto sum_node_inp_A = sum_node.setup_sink_pin("A");
  auto sum_node_inp_B = sum_node.setup_sink_pin("B");
  auto sum_node_out_Y = sum_node.setup_driver_pin("Y");
  
  auto sum_node2 = b_graph->create_node(Ntype_op::Sum);
  auto sum_node2_inp_A = sum_node2.setup_sink_pin("A");
  auto sum_node2_inp_B = sum_node2.setup_sink_pin("B");
  auto sum_node2_out_Y = sum_node2.setup_driver_pin("Y");

  auto mux_node = b_graph->create_node(Ntype_op::Mux);
  auto mux_node_inp_A = mux_node.setup_sink_pin("A");
  auto mux_node_inp_B = mux_node.setup_sink_pin("B");
  auto mux_node_inp_S = mux_node.setup_sink_pin("S");
  auto mux_node_out_Y = mux_node.setup_driver_pin("Y");
  
  auto mux_node2 = b_graph->create_node(Ntype_op::Mux);
  auto mux_node2_inp_A = mux_node2.setup_sink_pin("A");
  auto mux_node2_inp_B = mux_node2.setup_sink_pin("B");
  auto mux_node2_inp_S = mux_node2.setup_sink_pin("S");
  auto mux_node2_out_Y = mux_node2.setup_driver_pin("Y");
 
  auto xor_node = b_graph->create_node(Ntype_op::Xor);
  auto xor_node_inp_A = xor_node.setup_sink_pin("A");
  auto xor_node_out_Y = xor_node.setup_driver_pin("Y");
  
  auto xor_node2 = b_graph->create_node(Ntype_op::Xor);
  auto xor_node2_inp_A = xor_node2.setup_sink_pin("A");

  //---------------------------------------------------
  // creating edges
  b_graph->add_edge(sum_node2_out_Y, sum_node_inp_A, 10);
  b_graph->add_edge(graph_inp_B, sum_node_inp_B, 10); // input b -> Sum1 input
  
  b_graph->add_edge(graph_inp_A, sum_node2_inp_B, 10);
  b_graph->add_edge(mux_node_out_Y, sum_node2_inp_A, 10); // Mux output -> overall output

  b_graph->add_edge(graph_inp_A, xor_node_inp_A, 10); // input a -> Xor input
  b_graph->add_edge(graph_inp_B, xor_node_inp_A, 10); // input a -> Xor input
 
  b_graph->add_edge(sum_node_out_Y, mux_node_inp_B, 10); // Sum output -> Mux input
  b_graph->add_edge(xor_node_out_Y, mux_node_inp_A, 10); // Xor output -> Mux input
  b_graph->add_edge(graph_inp_C, mux_node_inp_S, 1);  // input c -> Mux (sel) input

  b_graph->add_edge(graph_inp_D, mux_node2_inp_A, 10);
  b_graph->add_edge(graph_inp_E, mux_node2_inp_B, 10);
  b_graph->add_edge(graph_inp_F, mux_node2_inp_S, 1);

  b_graph->add_edge(mux_node2_out_Y, xor_node2_inp_A, 10);
  b_graph->add_edge(mux_node2_out_Y, xor_node2_inp_A, 10);

  // overall output  
  b_graph->add_edge(mux_node_out_Y, graph_out_Y, 10); // Mux output -> overall output

  labeler.label(b_graph); 
  for (const auto &n : b_graph->forward(hier)) {
    //ASSERT_EQ(expected[n.debug_name()], static_cast<int>(n.get_color()));
#ifdef DEBUG
    fmt::print("Node Name:{} , Node Color:{}\n", n.debug_name(), n.get_color());  
#endif
  }   
}
#endif

#if ESSENT_TEST
TEST_F(Label_acyclic_test, essent_test) { 
  Lgraph* c_graph = Lgraph::create("lgdb", "c_graph", "-");
  ASSERT_NE(c_graph, nullptr);
  
  auto verbose = false;
  auto hier = false;
  auto iters = 1;
  auto seed = 0;
  std::string_view alg("vc");
  Label_mincut labeler(verbose, hier, iters, seed, alg);

  // input/output
  auto graph_inp_A = c_graph->add_graph_input("A", 1, 10);
  //auto graph_out_Y = c_graph->add_graph_output("Y", 3, 10);
 
  // create nodes
  auto xor_node1 = c_graph->create_node(Ntype_op::Xor);
  auto xor_node1_inp_A = xor_node1.setup_sink_pin("A");
  auto xor_node1_out_Y = xor_node1.setup_driver_pin("Y");

  auto xor_node2 = c_graph->create_node(Ntype_op::Xor);
  auto xor_node2_inp_A = xor_node2.setup_sink_pin("A");
  //auto xor_node2_out_Y = xor_node2.setup_driver_pin("Y");

  auto xor_node3 = c_graph->create_node(Ntype_op::Xor);
  //auto xor_node3_inp_A = xor_node3.setup_sink_pin("A");
  auto xor_node3_out_Y = xor_node3.setup_driver_pin("Y");

  auto xor_node4 = c_graph->create_node(Ntype_op::Xor);
  auto xor_node4_inp_A = xor_node4.setup_sink_pin("A");
  auto xor_node4_out_Y = xor_node4.setup_driver_pin("Y");

  auto xor_node5 = c_graph->create_node(Ntype_op::Xor);
  auto xor_node5_inp_A = xor_node5.setup_sink_pin("A");
  //auto xor_node5_out_Y = xor_node5.setup_driver_pin("Y");

  auto xor_node6 = c_graph->create_node(Ntype_op::Xor);
  //auto xor_node6_inp_A = xor_node6.setup_sink_pin("A");
  auto xor_node6_out_Y = xor_node6.setup_driver_pin("Y");

  auto xor_node7 = c_graph->create_node(Ntype_op::Xor);
  auto xor_node7_inp_A = xor_node7.setup_sink_pin("A");
  //auto xor_node7_out_Y = xor_node7.setup_driver_pin("Y");

  auto xor_node0 = c_graph->create_node(Ntype_op::Xor);
  auto xor_node0_inp_A = xor_node0.setup_sink_pin("A");
  auto xor_node0_out_Y = xor_node0.setup_driver_pin("Y");
  
  // creating edges
  c_graph->add_edge(graph_inp_A, xor_node0_inp_A, 10); // input a -> Xor input
  //c_graph->add_edge(graph_inp_A, xor_node6_inp_A, 10); // input a -> Xor input

  c_graph->add_edge(xor_node0_out_Y, xor_node1_inp_A, 10); // 0 -> 1
  c_graph->add_edge(xor_node1_out_Y, xor_node2_inp_A, 10); // 1 -> 2
  c_graph->add_edge(xor_node3_out_Y, xor_node4_inp_A, 10); // 3 -> 4
  c_graph->add_edge(xor_node4_out_Y, xor_node5_inp_A, 10); // 4 -> 5
  c_graph->add_edge(xor_node4_out_Y, xor_node2_inp_A, 10); // 4 -> 2
  c_graph->add_edge(xor_node6_out_Y, xor_node7_inp_A, 10); // 6 -> 7

  labeler.label(c_graph);
  
  for (const auto &n : c_graph->forward(hier)) {
    //ASSERT_EQ(expected[n.debug_name()], static_cast<int>(n.get_color()));
#ifdef DEBUG
    fmt::print("Node Name:{} , Node Color:{}\n", n.debug_name(), n.get_color()); 
#endif
  }
}
#endif

#if RUN
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#else
int main(int argc, char **argv) { return 0; }
#endif

