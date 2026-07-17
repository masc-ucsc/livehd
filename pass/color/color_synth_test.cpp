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

// The RAW algorithm: cut rules only, no size window. Every test below pins what
// `synth` cuts, so the window must stay out of the way -- min_ge/max_ge default
// to 0 (inert) precisely so an algorithm asked for the raw coloring gives the raw
// coloring. Stated explicitly here rather than relied on: the shipped CLI policy
// is 1000/200000, and a test that silently inherited it would be pinning the
// window instead of the cut rules.
Color_opts flat_opts() {
  Color_opts o;
  o.hier    = false;
  o.compact = true;
  o.min_ge  = 0;
  o.max_ge  = 0;
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

// With the window on, the cut rules still decide the SHAPE but no longer the
// SIZE: the per-node regions `synth` opens for a chain of wide Sums (each its own
// boundary) get merged up to min. This is the XSCore singleton story in
// miniature.
TEST(ColorSynth, SizeWindowMergesTheBoundarySingletons) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_synth_window");
  auto  gio = lib.create_io("synth_window");
  gio->add_input("a", 0);
  gio->add_output("y", 1);
  auto g = gio->create_graph();

  auto in = g->get_input_pin("a");
  livehd::graph_util::set_bits(in, 16);

  // A chain of wide Sums: every one is a cut, so raw `synth` gives each its own
  // region (16 GE apiece).
  auto prev = in;
  for (int i = 0; i < 12; ++i) {
    auto s = create_typed_node(*g, Ntype_op::Sum);
    prev.connect_sink(s.create_sink_pin(0));
    in.connect_sink(s.create_sink_pin(1));
    auto d = s.create_driver_pin(0);
    livehd::graph_util::set_bits(d, 16);
    prev = d;
  }
  prev.connect_sink(g->get_output_pin("y"));

  auto count_regions = [&]() {
    absl::flat_hash_set<int> ids;
    for (auto n : g->forward_class()) {
      if (is_partitionable(n)) {
        ids.insert(node_color_of(n));
      }
    }
    return ids.size();
  };

  Color_synth raw(flat_opts(), "synth");
  raw.label(g.get());
  const auto raw_regions = count_regions();
  EXPECT_GT(raw_regions, 4u) << "raw synth cuts at every wide Sum";

  auto o   = flat_opts();
  o.min_ge = 64;  // four 16-GE Sums' worth
  Color_synth windowed(o, "synth");
  windowed.label(g.get());

  EXPECT_LT(count_regions(), raw_regions) << "the window must merge the boundary singletons";
}

// Source-seeded regions (the 2opt-freq block-attribute channel) are the user's,
// not the algorithm's: they win outright and the window must not resize them.
// pass.abc binds its per-region `region_opts` by exact color id, so renumbering
// a seeded region silently binds its ABC flow to the wrong logic.
TEST(ColorSynth, SeededRegionsSurviveTheWindow) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_synth_seeded");
  auto  gio = lib.create_io("synth_seeded");
  gio->add_input("a", 0);
  gio->add_output("y", 1);
  auto g = gio->create_graph();

  auto in = g->get_input_pin("a");
  livehd::graph_util::set_bits(in, 8);

  // Three plain nodes, the middle one hand-colored as a seeded block region.
  auto n0 = create_typed_node(*g, Ntype_op::And);
  in.connect_sink(n0.create_sink_pin(0));
  auto d0 = n0.create_driver_pin(0);
  livehd::graph_util::set_bits(d0, 8);

  auto seeded = create_typed_node(*g, Ntype_op::Not);
  d0.connect_sink(seeded.create_sink_pin(0));
  auto ds = seeded.create_driver_pin(0);
  livehd::graph_util::set_bits(ds, 8);

  auto n2 = create_typed_node(*g, Ntype_op::And);
  ds.connect_sink(n2.create_sink_pin(0));
  in.connect_sink(n2.create_sink_pin(1));
  auto d2 = n2.create_driver_pin(0);
  livehd::graph_util::set_bits(d2, 8);
  d2.connect_sink(g->get_output_pin("y"));

  constexpr int kSeededId = 7;
  livehd::graph_util::set_color(seeded, kSeededId);
  livehd::color::set_coloring_info(g.get(), R"({"schema_version":1,"algorithm":"block-attr","params":{}})");
  ASSERT_TRUE(livehd::color::has_seeded_coloring(g.get()));

  auto o   = flat_opts();
  o.min_ge = 1000;  // far above the whole def: the window would fuse everything
  o.max_ge = 200000;
  Color_synth labeler(o, "synth");
  labeler.label(g.get());

  EXPECT_EQ(node_color_of(seeded), kSeededId) << "the seeded region keeps its exact id";
  EXPECT_NE(node_color_of(n0), kSeededId) << "and nothing is merged into it";
  EXPECT_NE(node_color_of(n2), kSeededId);
  EXPECT_GT(node_color_of(n0), kSeededId) << "algorithm ids shift above the max seeded id";
}
