//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Ported from pass/label/path_test (2c-color) onto the current hhds::Graph API.

#include "color_common.hpp"
#include "color_path.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::color::Color_opts;
using livehd::color::Color_path;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::has_color;
using livehd::graph_util::node_color_of;

namespace {

Color_opts flat_opts() {
  Color_opts o;
  o.hier    = false;
  o.compact = true;
  return o;
}

}  // namespace

// A single flop with one combinational predecessor and one successor: all three
// share the flop's color.
TEST(ColorPath, SingleFlop) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_path_test");
  auto  gio = lib.create_io("single_flop");
  gio->add_input("inp", 1);
  gio->add_output("out", 2);
  auto g = gio->create_graph();

  auto xr = create_typed_node(*g, Ntype_op::Xor);
  g->get_input_pin("inp").connect_sink(xr.create_sink_pin(0));

  auto flop = create_typed_node(*g, Ntype_op::Flop);
  xr.create_driver_pin(0).connect_sink(flop.create_sink_pin(0));

  auto sm = create_typed_node(*g, Ntype_op::Sum);
  flop.create_driver_pin(0).connect_sink(sm.create_sink_pin(0));
  sm.create_driver_pin(0).connect_sink(g->get_output_pin("out"));

  Color_path labeler(flat_opts());
  labeler.label(g.get());

  auto fc = node_color_of(flop);
  EXPECT_NE(0, fc);
  EXPECT_EQ(fc, node_color_of(xr)) << "predecessor shares the flop path color";
  EXPECT_EQ(fc, node_color_of(sm)) << "successor shares the flop path color";
}

// Back-to-back flops share the same color.
TEST(ColorPath, BackToBackFlops) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_path_test");
  auto  gio = lib.create_io("b2b_flops");
  gio->add_input("inp", 1);
  gio->add_output("out", 2);
  auto g = gio->create_graph();

  auto f1 = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("inp").connect_sink(f1.create_sink_pin(0));
  auto f2 = create_typed_node(*g, Ntype_op::Flop);
  f1.create_driver_pin(0).connect_sink(f2.create_sink_pin(0));
  f2.create_driver_pin(0).connect_sink(g->get_output_pin("out"));

  Color_path labeler(flat_opts());
  labeler.label(g.get());

  auto c1 = node_color_of(f1);
  auto c2 = node_color_of(f2);
  EXPECT_NE(0, c1);
  EXPECT_EQ(c1, c2) << "back-to-back flops share a color";
}

// Two independent flops with no shared logic get different colors.
TEST(ColorPath, IndependentFlops) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_path_test");
  auto  gio = lib.create_io("indep_flops");
  gio->add_input("inp1", 1);
  gio->add_input("inp2", 2);
  gio->add_output("out1", 3);
  gio->add_output("out2", 4);
  auto g = gio->create_graph();

  auto f1 = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("inp1").connect_sink(f1.create_sink_pin(0));
  f1.create_driver_pin(0).connect_sink(g->get_output_pin("out1"));

  auto f2 = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("inp2").connect_sink(f2.create_sink_pin(0));
  f2.create_driver_pin(0).connect_sink(g->get_output_pin("out2"));

  Color_path labeler(flat_opts());
  labeler.label(g.get());

  auto c1 = node_color_of(f1);
  auto c2 = node_color_of(f2);
  EXPECT_NE(0, c1);
  EXPECT_NE(0, c2);
  EXPECT_NE(c1, c2) << "independent flops get different colors";
}
