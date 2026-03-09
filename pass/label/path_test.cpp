#include <format>
#include <iostream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "label_path.hpp"
#include "lgraph.hpp"

class Label_path_test : public ::testing::Test {
public:
  void SetUp() override {}
  void TearDown() override {}
};

// Test: single flop with combinational neighbors
TEST_F(Label_path_test, single_flop) {
  auto   *lib = Graph_library::instance("lgdb_path_test");
  Lgraph *g   = lib->create_lgraph("single_flop", "-");
  ASSERT_NE(g, nullptr);

  Label_path labeler(true, false);

  auto inp = g->add_graph_input("inp", 1, 10);
  auto out = g->add_graph_output("out", 3, 10);

  auto xor_node     = g->create_node(Ntype_op::Xor);
  auto xor_inp      = xor_node.setup_sink_pin();
  auto xor_out      = xor_node.setup_driver_pin(std::string_view("Y"));

  auto flop_node    = g->create_node(Ntype_op::Flop);
  auto flop_inp_din = flop_node.setup_sink_pin(std::string_view("din"));
  auto flop_out_q   = flop_node.setup_driver_pin(std::string_view("Q"));

  auto sum_node     = g->create_node(Ntype_op::Sum);
  auto sum_inp      = sum_node.setup_sink_pin(std::string_view("A"));
  auto sum_out      = sum_node.setup_driver_pin(std::string_view("Y"));

  g->add_edge(inp, xor_inp, 10);
  g->add_edge(xor_out, flop_inp_din, 10);
  g->add_edge(flop_out_q, sum_inp, 10);
  g->add_edge(sum_out, out, 10);

  labeler.label(g);

  // flop, xor (predecessor), and sum (successor) should share the same color
  auto flop_color = flop_node.get_color();
  EXPECT_NE(0, flop_color);
  EXPECT_EQ(flop_color, xor_node.get_color());
  EXPECT_EQ(flop_color, sum_node.get_color());
}

// Test: back-to-back flops share color
TEST_F(Label_path_test, back_to_back_flops) {
  auto   *lib = Graph_library::instance("lgdb_path_test");
  Lgraph *g   = lib->create_lgraph("back_to_back_flops", "-");
  ASSERT_NE(g, nullptr);

  Label_path labeler(true, false);

  auto inp = g->add_graph_input("inp", 1, 10);
  auto out = g->add_graph_output("out", 3, 10);

  auto flop1     = g->create_node(Ntype_op::Flop);
  auto flop1_din = flop1.setup_sink_pin(std::string_view("din"));
  auto flop1_q   = flop1.setup_driver_pin(std::string_view("Q"));

  auto flop2     = g->create_node(Ntype_op::Flop);
  auto flop2_din = flop2.setup_sink_pin(std::string_view("din"));
  auto flop2_q   = flop2.setup_driver_pin(std::string_view("Q"));

  g->add_edge(inp, flop1_din, 10);
  g->add_edge(flop1_q, flop2_din, 10);
  g->add_edge(flop2_q, out, 10);

  labeler.label(g);

  // Back-to-back flops should share the same color
  auto c1 = flop1.get_color();
  auto c2 = flop2.get_color();
  EXPECT_NE(0, c1);
  EXPECT_EQ(c1, c2);
}

// Test: independent flops get different colors
TEST_F(Label_path_test, independent_flops) {
  auto   *lib = Graph_library::instance("lgdb_path_test");
  Lgraph *g   = lib->create_lgraph("independent_flops", "-");
  ASSERT_NE(g, nullptr);

  Label_path labeler(true, false);

  auto inp1 = g->add_graph_input("inp1", 1, 10);
  auto inp2 = g->add_graph_input("inp2", 3, 10);
  auto out1 = g->add_graph_output("out1", 5, 10);
  auto out2 = g->add_graph_output("out2", 7, 10);

  auto flop1     = g->create_node(Ntype_op::Flop);
  auto flop1_din = flop1.setup_sink_pin(std::string_view("din"));
  auto flop1_q   = flop1.setup_driver_pin(std::string_view("Q"));

  auto flop2     = g->create_node(Ntype_op::Flop);
  auto flop2_din = flop2.setup_sink_pin(std::string_view("din"));
  auto flop2_q   = flop2.setup_driver_pin(std::string_view("Q"));

  g->add_edge(inp1, flop1_din, 10);
  g->add_edge(flop1_q, out1, 10);
  g->add_edge(inp2, flop2_din, 10);
  g->add_edge(flop2_q, out2, 10);

  labeler.label(g);

  // Independent flops should get different colors
  auto c1 = flop1.get_color();
  auto c2 = flop2.get_color();
  EXPECT_NE(0, c1);
  EXPECT_NE(0, c2);
  EXPECT_NE(c1, c2);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
