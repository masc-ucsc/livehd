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
  auto*   lib = Graph_library::instance("lgdb_path_test");
  Lgraph* g   = lib->create_lgraph("single_flop", "-");
  ASSERT_NE(g, nullptr);

  Label_path labeler(true, false);

  auto inp = g->add_graph_input("inp", 1, 10);
  auto out = g->add_graph_output("out", 3, 10);

  auto xor_node = g->create_node(Ntype_op::Xor);
  auto xor_inp  = xor_node.setup_sink_pin();
  auto xor_out  = xor_node.setup_driver_pin(std::string_view("Y"));

  auto flop_node    = g->create_node(Ntype_op::Flop);
  auto flop_inp_din = flop_node.setup_sink_pin(std::string_view("din"));
  auto flop_out_q   = flop_node.setup_driver_pin(std::string_view("Q"));

  auto sum_node = g->create_node(Ntype_op::Sum);
  auto sum_inp  = sum_node.setup_sink_pin(std::string_view("A"));
  auto sum_out  = sum_node.setup_driver_pin(std::string_view("Y"));

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
  auto*   lib = Graph_library::instance("lgdb_path_test");
  Lgraph* g   = lib->create_lgraph("back_to_back_flops", "-");
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
  auto*   lib = Graph_library::instance("lgdb_path_test");
  Lgraph* g   = lib->create_lgraph("independent_flops", "-");
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

// Test: instance mode only propagates forward through _instance* named nodes
TEST_F(Label_path_test, instance_forward_only) {
  auto*   lib = Graph_library::instance("lgdb_path_test");
  Lgraph* g   = lib->create_lgraph("instance_forward_only", "-");
  ASSERT_NE(g, nullptr);

  Label_path labeler(true, false, "foo");

  auto inp = g->add_graph_input("inp", 1, 10);
  auto out = g->add_graph_output("out", 3, 10);

  auto src     = g->create_node(Ntype_op::And);
  auto src_inp = src.setup_sink_pin(std::string_view("A"));
  auto src_out = src.setup_driver_pin(std::string_view("Y"));

  auto seed     = g->create_node(Ntype_op::Or);
  auto seed_inp = seed.setup_sink_pin(std::string_view("A"));
  auto seed_out = seed.setup_driver_pin(std::string_view("Y"));
  seed.set_name("foo");

  auto mid1     = g->create_node(Ntype_op::Xor);
  auto mid1_inp = mid1.setup_sink_pin(std::string_view("A"));
  auto mid1_out = mid1.setup_driver_pin(std::string_view("Y"));
  mid1.set_name("_foo_stage0");

  auto mid2     = g->create_node(Ntype_op::Sum);
  auto mid2_inp = mid2.setup_sink_pin(std::string_view("A"));
  auto mid2_out = mid2.setup_driver_pin(std::string_view("Y"));

  auto stop     = g->create_node(Ntype_op::Not);
  auto stop_inp = stop.setup_sink_pin();
  auto stop_out = stop.setup_driver_pin(std::string_view("Y"));
  stop.set_name("bar");

  g->add_edge(inp, src_inp, 10);
  g->add_edge(src_out, seed_inp, 10);
  g->add_edge(seed_out, mid1_inp, 10);
  g->add_edge(mid1_out, mid2_inp, 10);
  g->add_edge(mid2_out, stop_inp, 10);
  g->add_edge(stop_out, out, 10);

  labeler.label(g);

  auto seed_color = seed.get_color();
  EXPECT_NE(0, seed_color);
  EXPECT_FALSE(src.has_color());
  EXPECT_EQ(seed_color, mid1.get_color());
  EXPECT_EQ(seed_color, mid2.get_color());
  EXPECT_EQ(seed_color, stop.get_color());
}

TEST_F(Label_path_test, instance_multiple_seeds_get_different_colors) {
  auto*   lib = Graph_library::instance("lgdb_path_test");
  Lgraph* g   = lib->create_lgraph("instance_multiple_seeds_get_different_colors", "-");
  ASSERT_NE(g, nullptr);

  Label_path labeler(true, false, "foo,bb");

  auto foo_seed     = g->create_node(Ntype_op::Or);
  auto foo_seed_out = foo_seed.setup_driver_pin(std::string_view("Y"));
  foo_seed.set_name("foo");

  auto foo_mid     = g->create_node(Ntype_op::Xor);
  auto foo_mid_inp = foo_mid.setup_sink_pin(std::string_view("A"));
  foo_mid.setup_driver_pin(std::string_view("Y"));
  foo_mid.set_name("_foo_stage0");

  auto bb_seed     = g->create_node(Ntype_op::And);
  auto bb_seed_out = bb_seed.setup_driver_pin(std::string_view("Y"));
  bb_seed.set_name("bb");

  auto bb_mid     = g->create_node(Ntype_op::Sum);
  auto bb_mid_inp = bb_mid.setup_sink_pin(std::string_view("A"));
  auto bb_mid_out = bb_mid.setup_driver_pin(std::string_view("Y"));
  bb_mid.set_name("_bb_stage0");

  auto bb_stop     = g->create_node(Ntype_op::Not);
  auto bb_stop_inp = bb_stop.setup_sink_pin();
  bb_stop.set_name("after_bb");

  g->add_edge(foo_seed_out, foo_mid_inp, 10);
  g->add_edge(bb_seed_out, bb_mid_inp, 10);
  g->add_edge(bb_mid_out, bb_stop_inp, 10);

  labeler.label(g);

  auto foo_color = foo_seed.get_color();
  auto bb_color  = bb_seed.get_color();

  EXPECT_NE(0, foo_color);
  EXPECT_NE(0, bb_color);
  EXPECT_NE(foo_color, bb_color);
  EXPECT_EQ(foo_color, foo_mid.get_color());
  EXPECT_EQ(bb_color, bb_mid.get_color());
  EXPECT_EQ(bb_color, bb_stop.get_color());
}

TEST_F(Label_path_test, alias_passthrough_nodes_keep_same_color) {
  auto*   lib = Graph_library::instance("lgdb_path_test");
  Lgraph* g   = lib->create_lgraph("alias_passthrough_nodes_keep_same_color", "-");
  ASSERT_NE(g, nullptr);

  Label_path labeler(false, false);

  auto flop     = g->create_node(Ntype_op::Flop);
  auto flop_q   = flop.setup_driver_pin(std::string_view("Q"));
  auto flop_din = flop.setup_sink_pin(std::string_view("din"));
  g->add_edge(g->create_node_const(1).setup_driver_pin(), flop_din, 1);
  flop_q.set_name("pipeB_id_ex.reg_isValid");

  auto mask      = g->create_node(Ntype_op::Get_mask);
  auto mask_a    = mask.setup_sink_pin(std::string_view("a"));
  auto mask_sel  = mask.setup_sink_pin(std::string_view("mask"));
  auto mask_y    = mask.setup_driver_pin(std::string_view("Y"));
  g->add_edge(flop_q, mask_a, 1);
  g->add_edge(g->create_node_const(-1).setup_driver_pin(), mask_sel, 1);

  auto alias     = g->create_node(Ntype_op::Or);
  auto alias_a   = alias.setup_sink_pin(std::string_view("A"));
  auto alias_out = alias.setup_driver_pin(std::string_view("Y"));
  alias_out.set_name("nextPCmod.io_pipeB_valid");
  g->add_edge(mask_y, alias_a, 1);

  labeler.label(g);

  auto flop_color = flop.get_color();
  EXPECT_NE(0, flop_color);
  EXPECT_EQ(flop_color, mask.get_color());
  EXPECT_EQ(flop_color, alias.get_color());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
