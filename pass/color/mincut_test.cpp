//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Ported from pass/label/mincut_test (2c-color) onto the current hhds::Graph
// API. VieCut runs file-based (METIS in, partition out), so this only asserts
// the coloring invariants, not a specific cut.

#include "color_common.hpp"
#include "color_mincut.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::color::Color_mincut;
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

// VieCut colors every non-const/non-IO node with a nonzero partition id.
TEST(ColorMincut, AllNodesColored) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_color_mincut_test");
  auto  gio = lib.create_io("mincut_simple");
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

  auto an = create_typed_node(*g, Ntype_op::And);
  sum.create_driver_pin(0).connect_sink(an.create_sink_pin(0));
  xr.create_driver_pin(0).connect_sink(an.create_sink_pin(1));

  auto mux = create_typed_node(*g, Ntype_op::Mux);
  g->get_input_pin("c").connect_sink(mux.create_sink_pin(0));
  an.create_driver_pin(0).connect_sink(mux.create_sink_pin(1));
  sum.create_driver_pin(0).connect_sink(mux.create_sink_pin(2));
  mux.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  Color_mincut labeler(flat_opts(), /*iters=*/1, /*seed=*/0, "vc");
  labeler.label(g.get());

  int total   = 0;
  int colored = 0;
  for (auto n : g->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    ++total;
    if (node_color_of(n) != 0) {
      ++colored;
    }
  }
  EXPECT_GT(total, 0);
  EXPECT_EQ(total, colored) << "VieCut should assign a nonzero color to every partitionable node";
}
