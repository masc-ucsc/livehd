//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Ported from pass/label/synth_test (2c-color) onto the current hhds::Graph API.

#include "color_common.hpp"
#include "color_synth.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::color::Color_opts;
using livehd::color::Color_synth;
using livehd::color::is_partitionable;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::node_color_of;

namespace {

Color_opts flat_opts() {
  Color_opts o;
  o.hier    = false;
  o.compact = true;
  return o;
}

}  // namespace

// In pipe mode every combinational node is clustered (nonzero), and a forward
// chain shares a single cluster id.
TEST(ColorSynth, ChainSharesCluster) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_synth_test");
  auto  gio = lib.create_io("synth_chain");
  gio->add_input("a", 1);
  gio->add_output("y", 2);
  auto g = gio->create_graph();

  auto n1 = create_typed_node(*g, Ntype_op::Xor);
  g->get_input_pin("a").connect_sink(n1.create_sink_pin(0));
  auto n2 = create_typed_node(*g, Ntype_op::And);
  n1.create_driver_pin(0).connect_sink(n2.create_sink_pin(0));
  auto n3 = create_typed_node(*g, Ntype_op::Or);
  n2.create_driver_pin(0).connect_sink(n3.create_sink_pin(0));
  n3.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  Color_synth labeler(flat_opts(), "pipe");
  labeler.label(g.get());

  EXPECT_NE(0, node_color_of(n1));
  EXPECT_EQ(node_color_of(n1), node_color_of(n2)) << "forward-propagated cluster id";
  EXPECT_EQ(node_color_of(n2), node_color_of(n3));
}

// In synth mode a Mult opens a fresh synthesis boundary (it is skipped, so it is
// not folded into its driver's cluster).
TEST(ColorSynth, MultIsSynthBoundary) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_synth_test");
  auto  gio = lib.create_io("synth_mult");
  gio->add_input("a", 1);
  gio->add_input("b", 2);
  gio->add_output("y", 3);
  auto g = gio->create_graph();

  auto add = create_typed_node(*g, Ntype_op::Sum);
  g->get_input_pin("a").connect_sink(add.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(add.create_sink_pin(1));

  auto mul = create_typed_node(*g, Ntype_op::Mult);
  add.create_driver_pin(0).connect_sink(mul.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(mul.create_sink_pin(1));
  mul.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  Color_synth labeler(flat_opts(), "synth");
  labeler.label(g.get());

  // The Sum is clustered; the Mult is a boundary and is not given the Sum's id.
  EXPECT_NE(0, node_color_of(add));
  EXPECT_NE(node_color_of(add), node_color_of(mul)) << "Mult opens a fresh synthesis boundary";
}
