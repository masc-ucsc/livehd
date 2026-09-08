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

// Conditional lane writes mint Get_mask reads while factoring their word muxes.
// Once those reads resolve, the intermediate writers become private and must
// collapse in the SAME invocation, without another compile to discover them.
TEST(CpropCleanup, ConditionalPackReachesFixedPoint) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_pack_test");
  auto  io     = lib.create_io("conditional_pack");
  io->add_input("d", 1);
  io->set_bits("d", 4);
  for (int i = 0; i < 9; ++i) {
    auto name = "s" + std::to_string(i);
    io->add_input(name, i + 2);
    io->set_bits(name, 1);
  }
  io->add_output("q", 11);
  io->set_bits("q", 12);
  auto g     = io->create_graph();
  auto value = gu::create_const(*g, *Dlop::create_integer(0));
  for (int i = 0; i < 9; ++i) {
    auto write = gu::create_typed_node(*g, Ntype_op::Set_mask, 12);
    gu::set_ubits(write.create_driver_pin(0), 12);
    gu::setup_sink_by_name(write, "a").connect_driver(value);
    gu::setup_sink_by_name(write, "mask").connect_driver(gu::create_const(*g, *Dlop::create_integer(15LL << (4 * (i / 3)))));
    gu::setup_sink_by_name(write, "value").connect_driver(g->get_input_pin("d"));
    auto mux = gu::create_typed_node(*g, Ntype_op::Mux, 12);
    gu::set_ubits(mux.create_driver_pin(0), 12);
    mux.create_sink_pin(0).connect_driver(g->get_input_pin("s" + std::to_string(i)));
    mux.create_sink_pin(1).connect_driver(value);
    mux.create_sink_pin(2).connect_driver(write.create_driver_pin(0));
    value = mux.create_driver_pin(0);
  }
  value.connect_sink(g->get_output_pin("q"));
  Cprop cp;
  cp.do_trans(g);
  size_t before = 0;
  for (auto n : g->body().nodes()) {
    ++before;
    EXPECT_NE(gu::type_op_of(n), Ntype_op::Set_mask);
  }
  cp.do_trans(g);
  size_t after = 0;
  for ([[maybe_unused]] auto n : g->body().nodes()) {
    ++after;
  }
  EXPECT_EQ(before, after);
}

TEST(CpropCleanup, PackedWritesKeepTruncation) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_truncated_pack_test");
  auto  io     = lib.create_io("truncated_pack");
  io->add_input("d", 1);
  io->set_bits("d", 16);
  io->add_input("s", 2);
  io->set_bits("s", 1);
  io->add_output("q", 3);
  io->set_bits("q", 8);
  auto g     = io->create_graph();
  auto value = gu::create_const(*g, *Dlop::create_integer(0));
  // The frontend's enclosing expression hint is narrower than this unlimited
  // mux's actual input. It must not justify removing a lane's truncation.
  auto mux   = gu::create_typed_node(*g, Ntype_op::Mux, 4);
  gu::set_ubits(mux.create_driver_pin(0), 4);
  mux.create_sink_pin(0).connect_driver(g->get_input_pin("s"));
  mux.create_sink_pin(1).connect_driver(value);
  mux.create_sink_pin(2).connect_driver(g->get_input_pin("d"));
  for (int lane = 0; lane < 2; ++lane) {
    auto write = gu::create_typed_node(*g, Ntype_op::Set_mask, 8);
    gu::set_ubits(write.create_driver_pin(0), 8);
    gu::setup_sink_by_name(write, "a").connect_driver(value);
    gu::setup_sink_by_name(write, "mask").connect_driver(gu::create_const(*g, *Dlop::create_integer(15 << (4 * lane))));
    gu::setup_sink_by_name(write, "value").connect_driver(mux.create_driver_pin(0));
    value = write.create_driver_pin(0);
  }
  value.connect_sink(g->get_output_pin("q"));
  Cprop cp;
  cp.do_trans(g);
  size_t concats = 0;
  for (auto n : g->body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::Concat) {
      continue;
    }
    ++concats;
    auto lanes = gu::concat_lanes(n);
    EXPECT_EQ(gu::concat_total_width(lanes), 8);
    EXPECT_TRUE(gu::concat_lane_violation(lanes).empty());
    for (const auto& lane : lanes) {
      EXPECT_EQ(gu::type_op_of(lane.value.get_master_node()), Ntype_op::Get_mask);
      EXPECT_EQ(gu::bits_of(lane.value), 4);
    }
  }
  EXPECT_EQ(concats, 1);
}

TEST(CpropMux, NonzeroConstantConditionSelectsTrueArm) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_mux_condition");
  int   id     = 0;
  for (const auto literal : {"0", "1", "32", "-1", "0x100000000000000000000"}) {
    for (bool constant_arms : {false, true}) {
      auto io = lib.create_io("mux_condition_" + std::to_string(id++));
      io->add_input("a", 1);
      io->set_bits("a", 4);
      io->add_input("b", 2);
      io->set_bits("b", 4);
      io->add_output("q", 3);
      io->set_bits("q", 4);
      auto g         = io->create_graph();
      auto mux       = gu::create_typed_node(*g, Ntype_op::Mux, 4);
      auto false_arm = constant_arms ? gu::create_const(*g, *Dlop::create_integer(5)) : g->get_input_pin("a");
      auto true_arm  = constant_arms ? gu::create_const(*g, *Dlop::create_integer(9)) : g->get_input_pin("b");
      mux.create_sink_pin(0).connect_driver(gu::create_const(*g, *Dlop::from_pyrope(literal)));
      mux.create_sink_pin(1).connect_driver(false_arm);
      mux.create_sink_pin(2).connect_driver(true_arm);
      mux.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
      Cprop cp;
      cp.do_trans(g);
      const auto drivers = g->get_output_pin("q").get_driver_pins();
      ASSERT_EQ(drivers.size(), 1);
      EXPECT_EQ(drivers.front(), std::string_view(literal) == "0" ? false_arm : true_arm) << literal;
    }
  }
}

}  // namespace

TEST(CpropCleanup, EnabledFlopDoesNotNeedItsDataHoldMux) {
  namespace gu = livehd::graph_util;
  for (bool shared : {false, true}) {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_cprop_flop_hold_test");
    auto  io  = lib.create_io(shared ? "shared_hold" : "private_hold");
    io->add_input("d", 1);
    io->set_bits("d", 8);
    io->add_input("en", 2);
    io->set_bits("en", 1);
    io->add_input("clock", 3);
    io->set_bits("clock", 1);
    io->add_output("q", 4);
    io->set_bits("q", 8);
    if (shared) {
      io->add_output("observe", 5);
      io->set_bits("observe", 8);
    }
    auto g    = io->create_graph();
    auto flop = gu::create_typed_node(*g, Ntype_op::Flop, 8);
    auto q    = flop.create_driver_pin(0);
    gu::set_ubits(q, 8);
    q.connect_sink(g->get_output_pin("q"));
    g->get_input_pin("en").connect_sink(gu::setup_sink_by_name(flop, "enable"));
    g->get_input_pin("clock").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
    gu::create_const(*g, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(flop, "posclk"));
    auto mux = gu::create_typed_node(*g, Ntype_op::Mux, 8);
    auto d   = mux.create_driver_pin(0);
    gu::set_ubits(d, 8);
    g->get_input_pin("en").connect_sink(gu::setup_sink_by_name(mux, "s"));
    q.connect_sink(gu::setup_sink_by_name(mux, "p1"));
    g->get_input_pin("d").connect_sink(gu::setup_sink_by_name(mux, "p2"));
    d.connect_sink(gu::setup_sink_by_name(flop, "din"));
    if (shared) {
      d.connect_sink(g->get_output_pin("observe"));
    }
    Cprop{}.do_trans(g, false);
    EXPECT_FALSE(flop.is_invalid());
    EXPECT_EQ(gu::get_driver_of_sink_name(flop, "din"), g->get_input_pin("d"));
    if (shared) {
      EXPECT_FALSE(mux.is_invalid());
      EXPECT_EQ(gu::get_driver_of_sink_name(mux, "p1"), q);
    }
  }
}

TEST(CpropHotmux, ConstantControlsSelectValuesAndDefault) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_hotmux");
  for (int selected : {-1, 0, 1, 64}) {
    for (bool fallback : {false, true}) {
      auto io = lib.create_io("hotmux_" + std::to_string(selected + 1) + (fallback ? "_default" : "_zero"));
      io->add_output("q", 1);
      io->set_bits("q", 8);
      auto g   = io->create_graph();
      auto hot = gu::create_typed_node(*g, Ntype_op::Hotmux, 8);
      gu::set_ubits(hot.create_driver_pin(0), 8);
      for (int i = 0; i < 65; ++i) {
        hot.create_sink_pin(2 * i).connect_driver(gu::create_const(*g, *Dlop::create_integer(i == selected ? 1 : 0)));
        hot.create_sink_pin(2 * i + 1).connect_driver(gu::create_const(*g, *Dlop::create_integer(i + 10)));
      }
      if (fallback) {
        hot.create_sink_pin(130).connect_driver(gu::create_const(*g, *Dlop::create_integer(99)));
      }
      hot.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
      Cprop cp;
      cp.do_trans(g);
      auto edges = g->get_output_pin("q").inp_edges();
      ASSERT_EQ(edges.size(), 1);
      ASSERT_TRUE(edges[0].driver.is_const());
      EXPECT_EQ(gu::const_of(edges[0].driver).to_just_i64(), selected >= 0 ? selected + 10 : fallback ? 99 : 0);
    }
  }
}

TEST(CpropHotmux, UnusedOverlapSurvivesForFormal) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_hotmux_overlap");
  auto  io     = lib.create_io("unused_overlap");
  auto  g      = io->create_graph();
  auto  hot    = gu::create_typed_node(*g, Ntype_op::Hotmux, 8);
  for (int i = 0; i < 4; ++i) {
    hot.create_sink_pin(i).connect_driver(gu::create_const(*g, *Dlop::create_integer(1)));
  }
  Cprop cp;
  cp.do_trans(g);
  EXPECT_FALSE(hot.is_invalid());
}

// RULING (2026-09-07): identical arms collapse for a Hotmux exactly as for a
// Mux. When every arm value AND the all-controls-zero result are the same pin,
// the controls cannot change the output, so the cell (and the one-hot obligation
// riding on it) is dropped rather than kept alive as a decode cone plus its own
// ABC region. Without a default port the zero-control result is a literal 0, so
// that shape collapses only when the shared value IS zero.
TEST(CpropHotmux, IdenticalArmsCollapse) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_hotmux_identical");
  // shared: 0 = the arms share a runtime value, 1 = they share the constant 0.
  for (int shared : {0, 1}) {
    for (bool fallback : {false, true}) {
      auto io = lib.create_io(std::string{"identical_"} + (shared ? "zero" : "value") + (fallback ? "_default" : "_nodefault"));
      io->add_input("c0", 1);
      io->set_bits("c0", 1);
      io->add_input("c1", 2);
      io->set_bits("c1", 1);
      io->add_input("v", 3);
      io->set_bits("v", 8);
      io->add_output("q", 4);
      io->set_bits("q", 8);
      auto g     = io->create_graph();
      auto value = shared ? gu::create_const(*g, *Dlop::create_integer(0)) : g->get_input_pin("v");

      auto hot = gu::create_typed_node(*g, Ntype_op::Hotmux, 8);
      gu::set_ubits(hot.create_driver_pin(0), 8);
      hot.create_sink_pin(0).connect_driver(g->get_input_pin("c0"));
      hot.create_sink_pin(1).connect_driver(value);
      hot.create_sink_pin(2).connect_driver(g->get_input_pin("c1"));
      hot.create_sink_pin(3).connect_driver(value);
      if (fallback) {
        hot.create_sink_pin(4).connect_driver(value);
      }
      hot.create_driver_pin(0).connect_sink(g->get_output_pin("q"));

      Cprop cp;
      cp.do_trans(g);

      // No default port and a non-zero shared value: the zero-control case reads
      // 0, which the arms do not, so the cell must SURVIVE.
      const bool collapses = fallback || shared;
      EXPECT_EQ(hot.is_invalid(), collapses);
      auto edges = g->get_output_pin("q").inp_edges();
      ASSERT_EQ(edges.size(), 1);
      if (collapses) {
        EXPECT_TRUE(edges[0].driver == value);
      }
    }
  }
}
