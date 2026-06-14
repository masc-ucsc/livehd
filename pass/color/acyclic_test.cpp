//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Ported from pass/label/acyclic_test (2c-color). Asserts the acyclic coloring
// invariants on the current hhds::Graph API rather than exact legacy ids.

#include "color_acyclic.hpp"
#include "color_common.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::color::Color_acyclic;
using livehd::color::Color_opts;
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

// A small combinational cone: every partitionable node must get a nonzero color.
TEST(ColorAcyclic, AllNodesColored) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_acyclic_test");
  auto  gio = lib.create_io("acyc_simple");
  gio->add_input("a", 1);
  gio->add_input("b", 2);
  gio->add_input("c", 3);
  gio->add_output("y", 4);
  auto g = gio->create_graph();

  auto sum = create_typed_node(*g, Ntype_op::Sum);
  g->get_input_pin("a").connect_sink(sum.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(sum.create_sink_pin(1));

  auto xr = create_typed_node(*g, Ntype_op::Xor);
  g->get_input_pin("a").connect_sink(xr.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(xr.create_sink_pin(1));

  auto mux = create_typed_node(*g, Ntype_op::Mux);
  g->get_input_pin("c").connect_sink(mux.create_sink_pin(0));
  sum.create_driver_pin(0).connect_sink(mux.create_sink_pin(1));
  xr.create_driver_pin(0).connect_sink(mux.create_sink_pin(2));
  mux.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  Color_acyclic labeler(flat_opts(), /*cutoff=*/1, /*merge_en=*/true);
  labeler.label(g.get());

  int colored = 0;
  for (auto n : g->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    EXPECT_NE(0, node_color_of(n)) << "node " << livehd::graph_util::debug_name(n) << " left uncolored";
    ++colored;
  }
  EXPECT_EQ(3, colored);  // Sum, Xor, Mux
}

// Two independent output cones get distinct partition ids; a node and the
// predecessors it pulls into its partition share an id.
TEST(ColorAcyclic, IndependentConesDifferentSharedWithinCone) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_acyclic_test");
  auto  gio = lib.create_io("acyc_two_cones");
  gio->add_input("a", 1);
  gio->add_input("b", 2);
  gio->add_output("y0", 3);
  gio->add_output("y1", 4);
  auto g = gio->create_graph();

  // cone 0: a -> not0 -> and0 -> y0
  auto not0 = create_typed_node(*g, Ntype_op::Not);
  g->get_input_pin("a").connect_sink(not0.create_sink_pin(0));
  auto and0 = create_typed_node(*g, Ntype_op::And);
  not0.create_driver_pin(0).connect_sink(and0.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(and0.create_sink_pin(1));
  and0.create_driver_pin(0).connect_sink(g->get_output_pin("y0"));

  // cone 1: b -> not1 -> y1
  auto not1 = create_typed_node(*g, Ntype_op::Not);
  g->get_input_pin("b").connect_sink(not1.create_sink_pin(0));
  not1.create_driver_pin(0).connect_sink(g->get_output_pin("y1"));

  Color_acyclic labeler(flat_opts(), /*cutoff=*/1, /*merge_en=*/false);
  labeler.label(g.get());

  auto c_and0 = node_color_of(and0);
  auto c_not0 = node_color_of(not0);
  auto c_not1 = node_color_of(not1);
  EXPECT_NE(0, c_and0);
  EXPECT_NE(0, c_not1);
  EXPECT_EQ(c_and0, c_not0) << "and0 should pull its sole predecessor not0 into its partition";
  EXPECT_NE(c_and0, c_not1) << "the two output cones are independent partitions";
}
