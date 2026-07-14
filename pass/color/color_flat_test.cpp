//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Tests the `flat` coloring: every partitionable node in a def is marked with a
// single color, regardless of structure -- the coloring equivalent of
// flattening the design into one region. Contrast with cgen/acyclic, which
// split by cone/fan-out.

#include "color_flat.hpp"

#include "color_common.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::color::Color_flat;
using livehd::color::Color_opts;
using livehd::color::is_partitionable;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::node_color_of;

namespace {

Color_opts base_opts() {
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

int colored_node_cnt(hhds::Graph* g) {
  int cnt = 0;
  for (auto n : g->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    if (node_color_of(n) != livehd::color::NO_COLOR) {
      ++cnt;
    }
  }
  return cnt;
}

}  // namespace

// Two independent outputs (x=a+b, y=c+d): cgen would give 2 colors, flat gives 1.
TEST(ColorFlat, IndependentConesOneColor) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_flat_test");
  auto  gio = lib.create_io("flat_two_out");
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

  Color_flat(base_opts()).label(g.get());

  EXPECT_EQ(1, distinct_colors(g.get())) << "disconnected cones must still share one flat color";
  EXPECT_EQ(2, colored_node_cnt(g.get())) << "both regular nodes are colored";
  EXPECT_EQ(node_color_of(sx), node_color_of(sy));
}

// `continuous` must NOT re-split the flat color into per-region ids: two
// disconnected sub-circuits stay one color even with continuous requested.
TEST(ColorFlat, ContinuousForcedOff) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_flat_test");
  auto  gio = lib.create_io("flat_continuous");
  gio->add_input("a", 8);
  gio->add_input("b", 8);
  gio->add_input("c", 8);
  gio->add_input("d", 8);
  gio->add_output("x", 9);
  gio->add_output("y", 9);
  auto g = gio->create_graph();

  auto sx = create_typed_node(*g, Ntype_op::Sum);  // island 1
  g->get_input_pin("a").connect_sink(sx.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(sx.create_sink_pin(1));
  sx.create_driver_pin(0).connect_sink(g->get_output_pin("x"));

  auto sy = create_typed_node(*g, Ntype_op::Sum);  // island 2 (no edge to island 1)
  g->get_input_pin("c").connect_sink(sy.create_sink_pin(0));
  g->get_input_pin("d").connect_sink(sy.create_sink_pin(1));
  sy.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  Color_opts o = base_opts();
  o.continuous = true;  // Color_flat must ignore this
  Color_flat(o).label(g.get());

  EXPECT_EQ(1, distinct_colors(g.get())) << "flat ignores continuous: two islands, still one color";
}

// A deeper connected cone (fan-out, multiple levels) is also a single color.
TEST(ColorFlat, ConnectedLogicOneColor) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_flat_test");
  auto  gio = lib.create_io("flat_deep");
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

  Color_flat(base_opts()).label(g.get());

  EXPECT_EQ(1, distinct_colors(g.get()));
  EXPECT_EQ(4, colored_node_cnt(g.get())) << "every regular node is colored";
}
