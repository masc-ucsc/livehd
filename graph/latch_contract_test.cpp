//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// todo/livehd/2f-latch M8 steps 0b + 0c — the commit-class analysis and the
// edge-normalization TRIGGER, tested on hand-built graphs.
//
// Hand-built rather than driven through tolg on purpose: this is the ONE place
// the (net, edge, role) triple is decided, and every consumer (the M8 pass, the
// LEC encoder, cgen_sim) keys its behaviour off it. A fixture that went through
// the front end would also be testing tolg's mux/eq shaping, so a shape change
// there would move this test's meaning without failing it.
//
// The three distinctions pinned here each cost a silent full-cycle error if
// collapsed:
//   * DATA-gated vs CLOCK-gated latch — M8 slots them differently (last slot vs
//     slot-from-polarity). Collapse them and latch_verify_low's polarity canary
//     goes blind, which is the double-negation miscompile the task exists for.
//   * master/slave = ONE net, OPPOSITE parity — the whole reason the enable cone
//     is resolved back to a root instead of compared per node.
//   * an implicitly-clocked `reg x = 0` HAS a commit class. Returning nullopt
//     for it (the pre-M8 behaviour) hid the commonest state element in the tree.

#include "latch_contract.hpp"

#include <string>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

using livehd::latch_contract::commit_class_of;
using livehd::latch_contract::Design_clocks;
using livehd::latch_contract::needs_single_edge;
using livehd::latch_contract::Net_role;

namespace {

// A latch as tolg emits one: `enable` from the control cone, `din` from the
// hold mux. Only the pins this analysis reads are wired.
hhds::Node_class add_latch(hhds::Graph& g, const std::string& name, const hhds::Pin_class& enable, bool active_low) {
  auto l = gu::create_typed_node(g, Ntype_op::Latch);
  l.attr(hhds::attrs::name).set(name);
  auto q = l.create_driver_pin(0);
  gu::set_bits(q, 8);
  gu::set_unsign(q);
  enable.connect_sink(gu::setup_sink_by_name(l, "enable"));
  if (active_low) {
    // posclk on a Latch is the ENABLE POLARITY:
    // known-false = active LOW. This is the yosys-importer shape; from Pyrope
    // the polarity lives in the enable CONE instead.
    gu::create_const(g, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(l, "posclk"));
  }
  return l;
}

hhds::Node_class add_flop(hhds::Graph& g, const std::string& name, const hhds::Pin_class& clk, bool negedge) {
  auto f = gu::create_typed_node(g, Ntype_op::Flop);
  f.attr(hhds::attrs::name).set(name);
  auto q = f.create_driver_pin(0);
  gu::set_bits(q, 8);
  gu::set_unsign(q);
  if (!clk.is_invalid()) {
    clk.connect_sink(gu::setup_sink_by_name(f, "clock_pin"));
  }
  if (negedge) {
    gu::create_const(g, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(f, "posclk"));
  }
  return f;
}

}  // namespace

// `latch_verify_hold`'s shape: the gate is COMPUTED (`en = (c==0) or (c==4)`),
// so its root is an Or node — not a clock. It must classify as DATA even though
// the design also has a real clock net for its flops.
TEST(LatchContract, DataGatedLatchIsRoleData) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_lcontract_data");
  auto  gio = lib.create_io("data_gated");
  gio->add_input("clk", 1);
  gio->add_input("a", 1);
  gio->add_input("b", 1);
  gio->add_output("y", 8);
  auto g = gio->create_graph();

  auto orn = gu::create_typed_node(*g, Ntype_op::Or);
  g->get_input_pin("a").connect_sink(orn.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(orn.create_sink_pin(0));
  auto en = orn.create_driver_pin(0);
  gu::set_bits(en, 1);
  gu::set_unsign(en);

  auto l = add_latch(*g, "l", en, /*active_low=*/false);
  add_flop(*g, "f", g->get_input_pin("clk"), /*negedge=*/false);

  const Design_clocks clocks(g.get());
  auto                cc = commit_class_of(l, &clocks);
  ASSERT_TRUE(cc.has_value()) << "a latch with a resolvable enable must have a commit class";
  EXPECT_EQ(cc->role, Net_role::Data) << "a computed enable is DATA, not a gate clock";
  EXPECT_FALSE(cc->rising) << "active-HIGH enable commits on the enable's FALL";
  EXPECT_FALSE(cc->implicit_clock);
}

// A master/slave pair written `if clk` / `if !clk` must resolve to the SAME net
// at OPPOSITE parity, and that net must be recognized as a CLOCK — here with no
// flop in the design at all (the `latch_master_slave` fixture's shape), so the
// conventional-spelling fallback is what carries it.
TEST(LatchContract, MasterSlaveIsOneClockNetOppositeParity) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_lcontract_ms");
  auto  gio = lib.create_io("ms");
  gio->add_input("clk", 1);
  gio->add_output("q", 8);
  auto g = gio->create_graph();

  auto notn = gu::create_typed_node(*g, Ntype_op::Not);
  g->get_input_pin("clk").connect_sink(notn.create_sink_pin(0));
  auto nclk = notn.create_driver_pin(0);
  gu::set_bits(nclk, 1);
  gu::set_unsign(nclk);

  auto m = add_latch(*g, "m", nclk, /*active_low=*/false);                     // if !clk
  auto s = add_latch(*g, "s", g->get_input_pin("clk"), /*active_low=*/false);  // if clk

  const Design_clocks clocks(g.get());
  EXPECT_EQ(clocks.n_clock_inputs(), 0u) << "no flop clocks on anything: the name fallback must carry this design";

  auto cm = commit_class_of(m, &clocks);
  auto cs = commit_class_of(s, &clocks);
  ASSERT_TRUE(cm.has_value());
  ASSERT_TRUE(cs.has_value());
  EXPECT_EQ(cm->role, Net_role::Clock);
  EXPECT_EQ(cs->role, Net_role::Clock);
  EXPECT_EQ(cm->net_key(), cs->net_key()) << "`if clk` and `if !clk` must land on ONE root net";
  EXPECT_NE(cm->rising, cs->rising) << "a master/slave pair is OPPOSITE phases; collapsing them loses the pair";
  EXPECT_NE(cm->key(), cs->key());
}

// `reg x = 0` with no clock_pin: a real commit class on the module's implicit
// clock, and the key must be the SAME canonical spelling on every side of a
// miter (never a nid).
TEST(LatchContract, ImplicitlyClockedFlopHasACommitClass) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_lcontract_implicit");
  auto  gio = lib.create_io("implicit");
  gio->add_input("d", 8);
  gio->add_output("q", 8);
  auto g = gio->create_graph();

  auto f = add_flop(*g, "x", hhds::Pin_class{}, /*negedge=*/false);

  const Design_clocks clocks(g.get());
  EXPECT_TRUE(clocks.has_implicit_clock());
  auto cc = commit_class_of(f, &clocks);
  ASSERT_TRUE(cc.has_value()) << "an implicitly clocked reg is a CLASS, not an unresolvable cone";
  EXPECT_TRUE(cc->implicit_clock);
  EXPECT_EQ(cc->role, Net_role::Clock);
  EXPECT_TRUE(cc->rising);
  EXPECT_EQ(cc->net_key(), "\x01implicit");
  EXPECT_EQ(cc->key(), "\x01implicit+");
}

// posclk known-false on a FLOP means negedge (commits on the fall); on a LATCH
// it means active-LOW enable (transparent while 0, so commits on the RISE).
// Same pin, opposite direction — this is the double-negation hazard.
TEST(LatchContract, PosclkFalseMeansFallForFlopAndRiseForLatch) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_lcontract_posclk");
  auto  gio = lib.create_io("posclk");
  gio->add_input("clk", 1);
  gio->add_output("q", 8);
  auto g = gio->create_graph();

  auto fneg = add_flop(*g, "fn", g->get_input_pin("clk"), /*negedge=*/true);
  auto llow = add_latch(*g, "ll", g->get_input_pin("clk"), /*active_low=*/true);

  const Design_clocks clocks(g.get());
  auto                cf = commit_class_of(fneg, &clocks);
  auto                cl = commit_class_of(llow, &clocks);
  ASSERT_TRUE(cf.has_value());
  ASSERT_TRUE(cl.has_value());
  EXPECT_FALSE(cf->rising) << "negedge flop commits on the clock's FALL";
  EXPECT_TRUE(cl->rising) << "transparent-LOW latch is open while the gate is 0, so it commits on the RISE";
  EXPECT_EQ(cf->net_key(), cl->net_key()) << "both are on the same clock net";
  EXPECT_EQ(cf->role, Net_role::Clock);
  EXPECT_EQ(cl->role, Net_role::Clock);
}

// The M8 trigger. A plain posedge single-clock design must be QUIET — that is
// the whole blast-radius argument for landing the pass without re-baselining
// the corpus.
TEST(LatchContract, TriggerQuietOnPlainPosedgeDesign) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_lcontract_quiet");
  auto  gio = lib.create_io("quiet");
  gio->add_input("clk", 1);
  gio->add_output("q", 8);
  auto g = gio->create_graph();

  add_flop(*g, "a", g->get_input_pin("clk"), /*negedge=*/false);
  add_flop(*g, "b", g->get_input_pin("clk"), /*negedge=*/false);

  const auto need = needs_single_edge(g.get());
  EXPECT_FALSE(need.needed) << "trigger fired on an all-posedge single-clock design: " << need.why;
  EXPECT_EQ(need.n_clock_nets, 1);
}

TEST(LatchContract, TriggerFiresOnLatchNegedgeAndTwoClocks) {
  {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_lcontract_trig_latch");
    auto  gio = lib.create_io("trig_latch");
    gio->add_input("clk", 1);
    gio->add_input("en", 1);
    gio->add_output("q", 8);
    auto g = gio->create_graph();
    add_flop(*g, "f", g->get_input_pin("clk"), false);
    add_latch(*g, "l", g->get_input_pin("en"), false);
    const auto need = needs_single_edge(g.get());
    EXPECT_TRUE(need.needed);
    EXPECT_EQ(need.n_latches, 1);
  }
  {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_lcontract_trig_neg");
    auto  gio = lib.create_io("trig_neg");
    gio->add_input("clk", 1);
    gio->add_output("q", 8);
    auto g = gio->create_graph();
    add_flop(*g, "a", g->get_input_pin("clk"), false);
    add_flop(*g, "b", g->get_input_pin("clk"), true);
    const auto need = needs_single_edge(g.get());
    EXPECT_TRUE(need.needed);
    EXPECT_EQ(need.n_negedge_flops, 1);
  }
  {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_lcontract_trig_2clk");
    auto  gio = lib.create_io("trig_2clk");
    gio->add_input("clk", 1);
    gio->add_input("clk2", 1);
    gio->add_output("q", 8);
    auto g = gio->create_graph();
    add_flop(*g, "a", g->get_input_pin("clk"), false);
    add_flop(*g, "b", g->get_input_pin("clk2"), false);
    const auto need = needs_single_edge(g.get());
    EXPECT_TRUE(need.needed);
    EXPECT_EQ(need.n_clock_nets, 2);
  }
}

// The name fallback is what decides a gate clock in a flop-less design, so its
// boundaries are load-bearing: a false positive re-slots real state, a false
// negative drops a transparent-LOW latch into the wrong slot.
TEST(LatchContract, ClockNameHeuristicBoundaries) {
  EXPECT_TRUE(Design_clocks::name_looks_like_clock("clk"));
  EXPECT_TRUE(Design_clocks::name_looks_like_clock("clock"));
  EXPECT_TRUE(Design_clocks::name_looks_like_clock("CLK"));
  EXPECT_TRUE(Design_clocks::name_looks_like_clock("core_clk"));
  EXPECT_TRUE(Design_clocks::name_looks_like_clock("clk2"));

  EXPECT_FALSE(Design_clocks::name_looks_like_clock("en"));
  EXPECT_FALSE(Design_clocks::name_looks_like_clock("d"));
  EXPECT_FALSE(Design_clocks::name_looks_like_clock("clock_en")) << "a gated/qualified signal is not the clock itself";
  EXPECT_FALSE(Design_clocks::name_looks_like_clock("gclk_gate"));
  EXPECT_FALSE(Design_clocks::name_looks_like_clock("clocked"));
}
