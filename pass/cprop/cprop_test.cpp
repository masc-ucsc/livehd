//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cprop.hpp"

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace {

// The first latch sweep cannot know that `x | -1` is an always-open enable.
// The scalar sweep exposes that constant, so the SECOND latch sweep removes
// the latch. Its reserved clock-shaping input then becomes dead strictly after
// the earlier pack DCE point and must be collected by Cprop's final cleanup.
TEST(CpropCleanup, RunsAfterFinalCanonicalization) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cprop_test");
  auto  gio = lib.create_io("cprop_cleanup_after_latch");
  gio->add_input("d", 1);
  gio->set_bits("d", 1);
  gio->add_input("x", 2);
  gio->set_bits("x", 1);
  gio->add_input("aux", 3);
  gio->set_bits("aux", 1);
  gio->add_output("q", 4);
  gio->set_bits("q", 1);
  auto g = gio->create_graph();

  auto enable = livehd::graph_util::create_typed_node(*g, Ntype_op::Or, 1);
  g->get_input_pin("x").connect_sink(livehd::graph_util::setup_sink_by_name(enable, "as"));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(-1))
      .connect_sink(livehd::graph_util::setup_sink_by_name(enable, "as"));

  auto clock_shape = livehd::graph_util::create_typed_node(*g, Ntype_op::Not, 1);
  g->get_input_pin("aux").connect_sink(livehd::graph_util::setup_sink_by_name(clock_shape, "a"));

  auto latch = livehd::graph_util::create_typed_node(*g, Ntype_op::Latch, 1);
  g->get_input_pin("d").connect_sink(livehd::graph_util::setup_sink_by_name(latch, "din"));
  enable.create_driver_pin(0).connect_sink(livehd::graph_util::setup_sink_by_name(latch, "enable"));
  clock_shape.create_driver_pin(0).connect_sink(livehd::graph_util::setup_sink_by_name(latch, "clock_pin"));
  latch.create_driver_pin(0).connect_sink(g->get_output_pin("q"));

  Cprop cp;
  cp.do_trans(g, /*check_input_sized=*/false);

  EXPECT_TRUE(latch.is_invalid()) << "the now-always-open latch should become a wire";
  EXPECT_TRUE(clock_shape.is_invalid()) << "final cleanup must remove the control cone orphaned by that rewrite";
}

}  // namespace
