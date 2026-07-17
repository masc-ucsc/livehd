//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Ported from the old pass/label synth test (2c-color) onto the current hhds::Graph API.

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

// THE stage-coloring regression. A pipeline register takes `din` from its
// stage's data cone and `enable` from the stall/hazard cone. Those two cones
// must NOT end up in one region: when the flop inherited an id from every
// fan-in, set_id unioned them THROUGH the flop, and on a real CPU the weld
// propagated until the whole design was one color (measured on a flattened
// dino: 99.4% of nodes in a single region).
TEST(ColorSynth, FlopDoesNotWeldDinAndEnableCones) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_synth_test");
  auto  gio = lib.create_io("synth_flop_weld");
  gio->add_input("a", 1);
  gio->add_input("b", 1);
  gio->add_output("q", 1);
  auto g = gio->create_graph();

  auto data = create_typed_node(*g, Ntype_op::Xor);  // the din cone
  g->get_input_pin("a").connect_sink(data.create_sink_pin(0));

  auto ctrl = create_typed_node(*g, Ntype_op::And);  // the enable (stall) cone
  g->get_input_pin("b").connect_sink(ctrl.create_sink_pin(0));

  auto flop = create_typed_node(*g, Ntype_op::Flop);
  data.create_driver_pin(0).connect_sink(flop.create_sink_pin(3));  // din
  ctrl.create_driver_pin(0).connect_sink(flop.create_sink_pin(4));  // enable
  flop.create_driver_pin(0).connect_sink(g->get_output_pin("q"));

  Color_synth labeler(flat_opts(), "pipe");
  labeler.label(g.get());

  EXPECT_NE(0, node_color_of(data));
  EXPECT_NE(0, node_color_of(ctrl));
  EXPECT_NE(node_color_of(data), node_color_of(ctrl)) << "the flop must not union its din and enable cones";
}

// A cut owns an id, and every node of the cone it feeds is colored too.
// Previously an id was minted and handed only to the sinks, so a cone head that
// no primary input seeded stayed uncolored -- and a flop fed straight from a
// primary input was skipped by both loops and never colored at all.
TEST(ColorSynth, FlopAndItsFanoutConeAreColored) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_synth_test");
  auto  gio = lib.create_io("synth_flop_cone");
  gio->add_input("a", 1);
  gio->add_output("y", 1);
  auto g = gio->create_graph();

  auto flop = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("a").connect_sink(flop.create_sink_pin(3));  // din

  auto n1 = create_typed_node(*g, Ntype_op::Not);  // cone head: fed by the flop, not by an input
  flop.create_driver_pin(0).connect_sink(n1.create_sink_pin(0));
  auto n2 = create_typed_node(*g, Ntype_op::Or);
  n1.create_driver_pin(0).connect_sink(n2.create_sink_pin(0));
  n2.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  Color_synth labeler(flat_opts(), "pipe");
  labeler.label(g.get());

  EXPECT_NE(0, node_color_of(flop)) << "a flop fed straight from a primary input still gets a region";
  EXPECT_NE(0, node_color_of(n1)) << "the head of the flop's fan-out cone must be colored";
  EXPECT_NE(node_color_of(flop), node_color_of(n1)) << "a register is a barrier: its cone is a separate region";
  EXPECT_EQ(node_color_of(n1), node_color_of(n2)) << "the cone itself stays one region";
}

// The mirror of FlopDoesNotWeldDinAndEnableCones: two cones DRIVEN by one flop
// that reconverge must not be pulled into the flop's region either. A register
// is a barrier in both directions -- propagating its id downstream re-welds the
// design through every reconvergence (measured: per-def dino 45 regions -> 30,
// and the top def collapsed from many regions to exactly 1).
TEST(ColorSynth, FlopDoesNotAbsorbReconvergentFanoutCones) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_synth_test");
  auto  gio = lib.create_io("synth_flop_reconv");
  gio->add_input("a", 1);
  gio->add_output("y", 1);
  auto g = gio->create_graph();

  auto flop = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("a").connect_sink(flop.create_sink_pin(3));
  auto qpin = flop.create_driver_pin(0);

  auto l = create_typed_node(*g, Ntype_op::Not);  // two cones out of one register...
  qpin.connect_sink(l.create_sink_pin(0));
  auto r = create_typed_node(*g, Ntype_op::Not);
  qpin.connect_sink(r.create_sink_pin(0));

  auto join = create_typed_node(*g, Ntype_op::Or);  // ...that reconverge
  l.create_driver_pin(0).connect_sink(join.create_sink_pin(0));
  r.create_driver_pin(0).connect_sink(join.create_sink_pin(1));
  join.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  Color_synth labeler(flat_opts(), "pipe");
  labeler.label(g.get());

  EXPECT_NE(0, node_color_of(flop));
  EXPECT_NE(node_color_of(flop), node_color_of(join)) << "the register must stay out of the cone it drives";
  EXPECT_EQ(node_color_of(l), node_color_of(join)) << "reconvergent cones are one region";
  EXPECT_EQ(node_color_of(r), node_color_of(join));
}

// Two flops that share no combinational cone stay in separate regions -- the
// property that makes the coloring a partition rather than one blob.
TEST(ColorSynth, DisjointFlopsDoNotMerge) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_synth_test");
  auto  gio = lib.create_io("synth_two_flops");
  gio->add_input("a", 1);
  gio->add_input("b", 1);
  gio->add_output("x", 1);
  gio->add_output("y", 1);
  auto g = gio->create_graph();

  auto f1 = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("a").connect_sink(f1.create_sink_pin(3));
  auto u1 = create_typed_node(*g, Ntype_op::Not);
  f1.create_driver_pin(0).connect_sink(u1.create_sink_pin(0));
  u1.create_driver_pin(0).connect_sink(g->get_output_pin("x"));

  auto f2 = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("b").connect_sink(f2.create_sink_pin(3));
  auto u2 = create_typed_node(*g, Ntype_op::Not);
  f2.create_driver_pin(0).connect_sink(u2.create_sink_pin(0));
  u2.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  Color_synth labeler(flat_opts(), "pipe");
  labeler.label(g.get());

  EXPECT_NE(node_color_of(f1), node_color_of(f2)) << "independent registers are independent regions";
  EXPECT_NE(node_color_of(u1), node_color_of(u2)) << "their disjoint cones stay disjoint";
  EXPECT_NE(0, node_color_of(u1));
  EXPECT_NE(0, node_color_of(u2));
}

// A wide Sum is its own synthesis boundary in `synth` mode, so the cone feeding
// it and the cone it feeds are different regions.
TEST(ColorSynth, WideSumOpensBoundary) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_synth_test");
  auto  gio = lib.create_io("synth_wide_sum");
  gio->add_input("a", 16);
  gio->add_output("y", 16);
  auto g = gio->create_graph();

  auto head = create_typed_node(*g, Ntype_op::Not);
  g->get_input_pin("a").connect_sink(head.create_sink_pin(0));

  auto wide = create_typed_node(*g, Ntype_op::Sum);
  head.create_driver_pin(0).connect_sink(wide.create_sink_pin(0));
  auto wpin = wide.create_driver_pin(0);
  livehd::graph_util::set_bits(wpin, 16);  // > 8 => its own synthesis boundary
  wpin.connect_sink(g->get_output_pin("y"));

  Color_synth labeler(flat_opts(), "synth");
  labeler.label(g.get());

  EXPECT_NE(0, node_color_of(head));
  EXPECT_NE(0, node_color_of(wide));
  EXPECT_NE(node_color_of(head), node_color_of(wide)) << "a wide Sum opens a fresh synthesis boundary";
}
