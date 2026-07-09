//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Tests the `cgen` coloring: nodes are colored by WHICH primary output(s) they
// feed, so the color count tracks outputs (a few), not internal fan-out. This is
// the key contrast with `acyclic` (MFFC), which makes every fan-out point its own
// color.

#include "color_cgen.hpp"

#include "color_common.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::color::Color_cgen;
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

int distinct_colors(hhds::Graph* g) {
  absl::flat_hash_set<int> cs;
  for (auto n : g->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    if (auto c = node_color_of(n); c != livehd::color::NO_COLOR) {
      cs.insert(c);
    }
  }
  return static_cast<int>(cs.size());
}

}  // namespace

// Two independent outputs (x=a+b, y=c+d) get two distinct colors, one per cone.
TEST(ColorCgen, IndependentOutputsTwoColors) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_cgen_test");
  auto  gio = lib.create_io("cgen_two_out");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  gio->add_input("c", 8);
  gio->add_input("d", 8);
  gio->add_output("x", 9);
  gio->add_output("y", 9);
  auto g = gio->create_graph();

  auto sx = create_typed_node(*g, Ntype_op::Sum);
  g->get_input_pin("a").connect_sink(sx.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(sx.create_sink_pin(1));
  sx.create_driver_pin(0).connect_sink(g->get_output_pin("x"));

  auto sy = create_typed_node(*g, Ntype_op::Sum);
  g->get_input_pin("c").connect_sink(sy.create_sink_pin(0));
  g->get_input_pin("d").connect_sink(sy.create_sink_pin(1));
  sy.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  Color_cgen(flat_opts()).label(g.get());

  EXPECT_EQ(2, distinct_colors(g.get()));
  EXPECT_NE(node_color_of(sx), node_color_of(sy)) << "the two output cones must differ";
}

// A SINGLE output fed through an internal fan-out node must stay ONE color --
// the defining contrast with acyclic/MFFC, which would split at the fan-out.
// t = a+b (fan-out 2) -> p = t+a, q = t+b -> o = p+q.
TEST(ColorCgen, FanoutDoesNotFragmentSingleOutput) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_cgen_test");
  auto  gio = lib.create_io("cgen_fanout");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  gio->add_output("o", 12);
  auto g = gio->create_graph();

  auto t = create_typed_node(*g, Ntype_op::Sum);  // t = a+b, fan-out 2
  g->get_input_pin("a").connect_sink(t.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(t.create_sink_pin(1));

  auto p = create_typed_node(*g, Ntype_op::Sum);  // p = t+a
  t.create_driver_pin(0).connect_sink(p.create_sink_pin(0));
  g->get_input_pin("a").connect_sink(p.create_sink_pin(1));

  auto q = create_typed_node(*g, Ntype_op::Sum);  // q = t+b
  t.create_driver_pin(0).connect_sink(q.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(q.create_sink_pin(1));

  auto o = create_typed_node(*g, Ntype_op::Sum);  // o = p+q
  p.create_driver_pin(0).connect_sink(o.create_sink_pin(0));
  q.create_driver_pin(0).connect_sink(o.create_sink_pin(1));
  o.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  Color_cgen(flat_opts()).label(g.get());

  EXPECT_EQ(1, distinct_colors(g.get())) << "all logic feeds the single output -> one cone, despite the fan-out";
}

// Shared sub-cone: t=a+b feeds BOTH x and y. The shared node gets its own color
// (computed once); x-only and y-only logic get theirs -> 3 colors.
TEST(ColorCgen, SharedSubconeGetsOwnColor) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_cgen_test");
  auto  gio = lib.create_io("cgen_shared");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  gio->add_input("c", 8);
  gio->add_input("d", 8);
  gio->add_output("x", 10);
  gio->add_output("y", 10);
  auto g = gio->create_graph();

  auto t = create_typed_node(*g, Ntype_op::Sum);  // t = a+b, shared
  g->get_input_pin("a").connect_sink(t.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(t.create_sink_pin(1));

  auto sx = create_typed_node(*g, Ntype_op::Sum);  // x = t+c
  t.create_driver_pin(0).connect_sink(sx.create_sink_pin(0));
  g->get_input_pin("c").connect_sink(sx.create_sink_pin(1));
  sx.create_driver_pin(0).connect_sink(g->get_output_pin("x"));

  auto sy = create_typed_node(*g, Ntype_op::Sum);  // y = t+d
  t.create_driver_pin(0).connect_sink(sy.create_sink_pin(0));
  g->get_input_pin("d").connect_sink(sy.create_sink_pin(1));
  sy.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  Color_cgen(flat_opts()).label(g.get());

  EXPECT_EQ(3, distinct_colors(g.get()));
  EXPECT_NE(node_color_of(t), node_color_of(sx)) << "shared t must differ from x-exclusive logic";
  EXPECT_NE(node_color_of(t), node_color_of(sy)) << "shared t must differ from y-exclusive logic";
  EXPECT_NE(node_color_of(sx), node_color_of(sy)) << "x-exclusive and y-exclusive must differ";
}
