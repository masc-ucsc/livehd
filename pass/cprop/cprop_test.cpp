//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cprop.hpp"

#include <chrono>
#include <functional>
#include <tuple>
#include <unordered_map>

#include "bitwidth.hpp"
#include "enableopt.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hlop/dlop.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"

namespace {
void optimize_state(const std::shared_ptr<hhds::Graph>& graph) {
  Cprop{}.do_trans(graph);
  Bitwidth{10}.do_trans(graph);
  Enableopt{}.do_trans(graph);
  Cprop{}.do_trans(graph);
  Bitwidth{10}.do_trans(graph);
}

TEST(CpropConstants, FoldedValueIgnoresResultAndOutputWidthHints) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_hint_independence");
  for (int hint : {0, 1, 8, 32}) {
    auto io = lib.create_io("constant_hint_" + std::to_string(hint));
    io->add_output("out", 1);
    io->set_bits("out", 1);
    auto g   = io->create_graph();
    auto sum = gu::create_typed_node(*g, Ntype_op::Sum);
    gu::set_ubits(sum.create_driver_pin(0), hint);
    // ONE DRIVER PER SINK PIN: each `as` operand gets its own pid, which is
    // what setup_sink_pid appends on a banked op (graph/cell.hpp).
    gu::create_const(*g, *Dlop::create_integer(255)).connect_sink(gu::setup_sink_pid(sum, 0));
    gu::create_const(*g, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_pid(sum, 0));
    sum.create_driver_pin(0).connect_sink(g->get_output_pin("out"));
    Cprop{}.do_trans(g);
    // One driver per sink pin: a graph output is driven by exactly one pin.
    auto out_drv = g->get_output_pin("out").get_driver_pin();
    ASSERT_FALSE(out_drv.is_invalid());
    ASSERT_TRUE(out_drv.is_const());
    EXPECT_EQ(gu::const_of(out_drv).to_just_i64(), 256);
  }
}

TEST(CpropMasks, BoundaryAnnotationsCannotProveAMaskRedundant) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_boundary_hints");
  for (bool state : {false, true}) {
    auto io = lib.create_io(state ? "state_mask" : "input_mask");
    io->add_input("in", 1);
    io->set_bits("in", 1);
    io->add_output("out", 2);
    io->set_bits("out", 8);
    auto g     = io->create_graph();
    auto input = g->get_input_pin("in");
    gu::set_ubits(input, 1);
    if (state) {
      auto flop = gu::create_typed_node(*g, Ntype_op::Flop);
      gu::setup_sink_by_name(flop, "din").connect_driver(input);
      input = flop.create_driver_pin(0);
      gu::set_ubits(input, 1);
    }
    auto mask = gu::create_typed_node(*g, Ntype_op::Get_mask);
    gu::setup_sink_by_name(mask, "a").connect_driver(input);
    gu::setup_sink_by_name(mask, "mask").connect_driver(gu::create_const(*g, *Dlop::create_integer(255)));
    mask.create_driver_pin(0).connect_sink(g->get_output_pin("out"));
    Cprop{}.do_trans(g);
    // One driver per sink pin: a graph output is driven by exactly one pin.
    auto out_drv = g->get_output_pin("out").get_driver_pin();
    ASSERT_FALSE(out_drv.is_invalid());
    EXPECT_EQ(gu::type_op_of(out_drv.get_master_node()), Ntype_op::Get_mask);
  }
}

// A folded producer and an existing literal intern to the SAME driver pin (the
// constant pool hands out one pin per value), so a cell reading both reads that
// pin twice. Its arithmetic MULTIPLICITY must survive: `2 + 2` is 4, not 2.
//
// Under ONE DRIVER PER SINK PIN each operand owns a sink pid of its own
// (graph/cell.hpp), so the multiset is representable directly and the repeat
// cannot be deduped away -- which is exactly what this test pins down. It used
// to build the cell by connecting every operand to ONE `create_sink_pin(pid)`,
// where hhds edge storage (a set keyed by (driver, sink)) silently collapsed
// the repeat and cprop had to fold `2 + 2` into a literal 4 by hand to keep the
// value. The shape below is the legal one; the expected values are unchanged.
TEST(CpropConstants, FoldedConstantsPreserveOperandMultiplicity) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_CpropConstants_multiplicity");
  int   index  = 0;
  for (const auto& [op, pid, expected] : {
           std::tuple{ Ntype_op::Xor, 0,  0},
           std::tuple{ Ntype_op::Sum, 0,  8},
           std::tuple{ Ntype_op::Sum, 1, -8},
           std::tuple{Ntype_op::Mult, 0, 16},
           std::tuple{  Ntype_op::LT, 0,  0},
           std::tuple{  Ntype_op::GT, 0,  1}
  }) {
    auto io = lib.create_io("multiplicity_" + std::to_string(index++));
    io->add_output("out", 1);
    auto g        = io->create_graph();
    auto constant = [&](int value) { return gu::create_const(*g, *Dlop::create_integer(value)); };
    auto producer = gu::create_typed_node(*g, Ntype_op::SHL);
    constant(1).connect_sink(producer.create_sink_pin(0));
    constant(1).connect_sink(producer.create_sink_pin(1));
    auto       consumer = gu::create_typed_node(*g, op);
    // One sink pin per operand: setup_sink_by_name APPENDS a fresh slot to the
    // named bank each time it is called on a commutative cell.
    const auto bank     = pid == 0 ? "as" : "bs";
    constant(2).connect_sink(gu::setup_sink_by_name(consumer, bank));
    producer.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(consumer, bank));
    if (op == Ntype_op::Sum || op == Ntype_op::Mult) {
      constant(4).connect_sink(gu::setup_sink_by_name(consumer, bank));
    } else if (op == Ntype_op::LT || op == Ntype_op::GT) {
      constant(3).connect_sink(gu::setup_sink_by_name(consumer, "bs"));
    }
    consumer.create_driver_pin(0).connect_sink(g->get_output_pin("out"));
    Cprop{}.do_trans(g);
    // One driver per sink pin: a graph output is driven by exactly one pin.
    auto out_drv = g->get_output_pin("out").get_driver_pin();
    ASSERT_FALSE(out_drv.is_invalid());
    ASSERT_TRUE(out_drv.is_const()) << index;
    EXPECT_EQ(gu::const_of(out_drv).to_just_i64(), expected) << index;
  }
}

// `(lo + 7) + 1 - lo` is the width upass.tolg builds for `x#[lo ..+ 8]`. The
// forward Sum merge lands the shared `lo` on the consumer's OPPOSITE port, so
// the two occurrences cancel and the literal 8 is left. The same-port twin
// `(lo + 7) + lo` must keep both contributions (a parallel edge would dedup).
TEST(CpropConstants, ForwardSumCancelsOppositePortOperand) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_CpropConstants_sum_cancel");
  for (const bool cancel : {true, false}) {
    auto io = lib.create_io(cancel ? "sum_cancel_opposite" : "sum_keep_same_port");
    io->add_input("lo", 1);
    io->set_bits("lo", 8);
    io->add_output("out", 2);
    auto g        = io->create_graph();
    auto constant = [&](int value) { return gu::create_const(*g, *Dlop::create_integer(value)); };
    auto lo       = g->get_input_pin("lo");
    auto inner    = gu::create_typed_node(*g, Ntype_op::Sum);
    // One sink pin per operand on a banked op (graph/cell.hpp).
    lo.connect_sink(gu::setup_sink_pid(inner, 0));
    constant(7).connect_sink(gu::setup_sink_pid(inner, 0));
    auto outer = gu::create_typed_node(*g, Ntype_op::Sum);
    inner.create_driver_pin(0).connect_sink(gu::setup_sink_pid(outer, 0));
    constant(1).connect_sink(gu::setup_sink_pid(outer, 0));
    lo.connect_sink(gu::setup_sink_pid(outer, cancel ? 1 : 0));
    outer.create_driver_pin(0).connect_sink(g->get_output_pin("out"));
    Cprop{}.do_trans(g);
    // One driver per sink pin: a graph output is driven by exactly one pin.
    auto out_drv = g->get_output_pin("out").get_driver_pin();
    ASSERT_FALSE(out_drv.is_invalid());
    if (cancel) {
      ASSERT_TRUE(out_drv.is_const());
      EXPECT_EQ(gu::const_of(out_drv).to_just_i64(), 8);
    } else {
      EXPECT_FALSE(out_drv.is_const());
      size_t lo_uses = 0;
      for ([[maybe_unused]] const auto& e : lo.out_edges()) {
        ++lo_uses;
      }
      EXPECT_EQ(lo_uses, 2u);  // 2*lo + 8: neither `lo` contribution was dropped
    }
  }
}

// A driver the MERGED node reads twice is not a reason to refuse: `Sum(as:lo,
// bs:lo)` puts its two occurrences on the consumer's two DIFFERENT ports, so
// nothing races over one edge. The blanket "node reads it twice" refusal left
// this merge -- and the `lo - lo` cancellation it exposes -- on the table.
TEST(CpropConstants, ForwardSumMergesNodeInternalDuplicateDriver) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_CpropConstants_sum_dup_driver");
  auto  io     = lib.create_io("sum_dup_driver");
  io->add_input("lo", 1);
  io->set_bits("lo", 8);
  io->add_output("out", 2);
  auto g        = io->create_graph();
  auto constant = [&](int value) { return gu::create_const(*g, *Dlop::create_integer(value)); };
  auto lo       = g->get_input_pin("lo");

  auto inner = gu::create_typed_node(*g, Ntype_op::Sum);  // lo - lo
  lo.connect_sink(gu::setup_sink_pid(inner, 0));
  lo.connect_sink(gu::setup_sink_pid(inner, 1));

  auto outer = gu::create_typed_node(*g, Ntype_op::Sum);  // inner + 5
  inner.create_driver_pin(0).connect_sink(gu::setup_sink_pid(outer, 0));
  constant(5).connect_sink(gu::setup_sink_pid(outer, 0));
  outer.create_driver_pin(0).connect_sink(g->get_output_pin("out"));

  Cprop{}.do_trans(g);

  // One driver per sink pin: a graph output is driven by exactly one pin.
  auto out_drv = g->get_output_pin("out").get_driver_pin();
  ASSERT_FALSE(out_drv.is_invalid());
  ASSERT_TRUE(out_drv.is_const());
  EXPECT_EQ(gu::const_of(out_drv).to_just_i64(), 5);
}

// `lo = x + 1; hi = lo + 8` with `lo` also a module output: the shared `x + 1`
// is not absorbed, but its literal still folds into the consumer, so `hi` is
// `x + 9` (one adder deep) rather than a second adder chained off `lo`.
TEST(CpropConstants, ForwardSumFoldsLiteralThroughSharedOperand) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_CpropConstants_sum_shared_literal");
  auto  io     = lib.create_io("sum_shared_literal");
  io->add_input("x", 1);
  io->set_bits("x", 8);
  io->add_output("lo", 2);
  io->add_output("hi", 3);
  auto g        = io->create_graph();
  auto constant = [&](int value) { return gu::create_const(*g, *Dlop::create_integer(value)); };
  auto x        = g->get_input_pin("x");

  auto inner = gu::create_typed_node(*g, Ntype_op::Sum);  // x + 1
  x.connect_sink(gu::setup_sink_pid(inner, 0));
  constant(1).connect_sink(gu::setup_sink_pid(inner, 0));
  inner.create_driver_pin(0).connect_sink(g->get_output_pin("lo"));

  auto outer = gu::create_typed_node(*g, Ntype_op::Sum);  // inner + 8
  inner.create_driver_pin(0).connect_sink(gu::setup_sink_pid(outer, 0));
  constant(8).connect_sink(gu::setup_sink_pid(outer, 0));
  outer.create_driver_pin(0).connect_sink(g->get_output_pin("hi"));

  Cprop{}.do_trans(g);

  EXPECT_EQ(g->get_output_pin("lo").get_driver_pin(), inner.create_driver_pin(0));
  auto hi_drv = g->get_output_pin("hi").get_driver_pin();
  ASSERT_FALSE(hi_drv.is_invalid());
  ASSERT_FALSE(hi_drv.is_const());
  auto hi_sum = hi_drv.get_master_node();
  ASSERT_EQ(gu::type_op_of(hi_sum), Ntype_op::Sum);
  int  literal  = 0;
  bool reads_x  = false;
  int  operands = 0;
  for (const auto& sink : hi_sum.inp_sorted_pins()) {
    ++operands;
    EXPECT_EQ(Ntype::sink_bank(Ntype_op::Sum, sink.get_port_id()), 0u);
    const auto drv = sink.get_driver_pin();
    if (drv.is_const()) {
      literal = static_cast<int>(gu::const_of(drv).to_just_i64());
    } else {
      reads_x = drv == x;
    }
  }
  EXPECT_EQ(operands, 2);
  EXPECT_TRUE(reads_x);
  EXPECT_EQ(literal, 9);
}

// `if s { q = 1 }` under a separate enable: `s ? 1 : q` over 0/1 values would
// otherwise become `s | q`. Q must stay a DIRECT Mux arm, the one hold shape
// the latch contract exempts; through an Or it is a transparent self-update.
TEST(CpropLatch, HoldArmStaysAMuxArm) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_latch_hold_arm");
  auto  io     = lib.create_io("latch_hold_arm");
  for (const auto* name : {"s", "e", "q"}) {
    if (name[0] == 'q') {
      io->add_output(name, 3);
    } else {
      io->add_input(name, name[0] == 's' ? 1 : 2);
    }
    io->set_bits(name, 1);
    io->set_unsign(name, true);
  }
  auto g     = io->create_graph();
  auto latch = gu::create_typed_node(*g, Ntype_op::Latch);
  auto q     = latch.create_driver_pin(0);
  gu::set_ubits(q, 1);  // the declared `reg q:u1`
  auto hold = gu::create_typed_node(*g, Ntype_op::Mux);
  g->get_input_pin("s").connect_sink(gu::setup_sink_pid(hold, 0));
  q.connect_sink(gu::setup_sink_pid(hold, 1));
  gu::create_const(*g, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_pid(hold, 2));
  gu::setup_sink_by_name(latch, "din").connect_driver(hold.create_driver_pin(0));
  gu::setup_sink_by_name(latch, "enable").connect_driver(g->get_input_pin("e"));
  q.connect_sink(g->get_output_pin("q"));
  optimize_state(g);
  ASSERT_FALSE(latch.is_invalid());
  EXPECT_TRUE(livehd::latch_contract::check(g.get()));
}

TEST(CpropLatch, DeepFeedbackIsNotMistakenForIndependence) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_deep_latch_feedback");
  auto  io     = lib.create_io("deep_latch_feedback");
  io->add_output("q", 1);
  auto g     = io->create_graph();
  auto latch = gu::create_typed_node(*g, Ntype_op::Latch);
  auto q     = latch.create_driver_pin(0);
  auto value = q;
  for (int i = 0; i < 300; ++i) {
    auto invert = gu::create_typed_node(*g, Ntype_op::Not);
    gu::setup_sink_by_name(invert, "a").connect_driver(value);
    value = invert.create_driver_pin(0);
  }
  gu::setup_sink_by_name(latch, "din").connect_driver(value);
  gu::setup_sink_by_name(latch, "enable").connect_driver(gu::create_const(*g, *Dlop::create_integer(1)));
  q.connect_sink(g->get_output_pin("q"));
  optimize_state(g);
  ASSERT_FALSE(latch.is_invalid());
  EXPECT_EQ(gu::type_op_of(latch), Ntype_op::Latch);
  EXPECT_EQ(g->get_output_pin("q").get_driver_pin(), q);
}

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

  optimize_state(g);

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
    optimize_state(g);
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
      // One driver per sink pin: a graph output is driven by exactly one pin.
      auto out_drv = g->get_output_pin("q").get_driver_pin();
      ASSERT_FALSE(out_drv.is_invalid());
      ASSERT_TRUE(out_drv.is_const());
      EXPECT_EQ(gu::const_of(out_drv).to_just_i64(), selected >= 0 ? selected + 10 : fallback ? 99 : 0);
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
      // One driver per sink pin: a graph output is driven by exactly one pin.
      auto out_drv = g->get_output_pin("q").get_driver_pin();
      ASSERT_FALSE(out_drv.is_invalid());
      if (collapses) {
        EXPECT_TRUE(out_drv == value);
      }
    }
  }
}

namespace {
namespace gu      = livehd::graph_util;
using Test_pin    = hhds::Pin_class;
using Test_values = std::unordered_map<uint64_t, int64_t>;

// Independent integer evaluator for the small combinational transition cones
// below. Inputs and current state are supplied explicitly; memoization preserves
// DAG sharing. Unsupported operators fail rather than supplying a golden value.
int64_t mux_eval(Test_pin pin, Test_values& values) {
  if (pin.is_const()) {
    return gu::const_of(pin).to_just_i64();
  }
  auto key = static_cast<uint64_t>(pin.get_class_index().value);
  if (auto it = values.find(key); it != values.end()) {
    return it->second;
  }
  auto node = pin.get_master_node();
  auto op   = gu::type_op_of(node);
  auto at   = [&](int pid) {
    auto ds = node.get_sink_pin(pid).get_driver_pins();
    EXPECT_EQ(ds.size(), 1);
    return ds.empty() ? int64_t{0} : mux_eval(ds.front(), values);
  };
  int64_t result = 0;
  if (op == Ntype_op::Mux) {
    result = at(at(0) != 0 ? 2 : 1);
  } else if (op == Ntype_op::Hotmux) {
    auto inputs = gu::hotmux_inputs(node);
    bool found  = false;
    for (const auto& [control, value] : inputs.arms) {
      if (mux_eval(control, values) != 0) {
        EXPECT_FALSE(found) << "a rewritten Hotmux must remain one-hot";
        found  = true;
        result = mux_eval(value, values);
      }
    }
    if (!found && !inputs.fallback.is_invalid()) {
      result = mux_eval(inputs.fallback, values);
    }
  } else if (op == Ntype_op::Concat) {
    for (const auto& lane : gu::concat_lanes(node)) {
      const auto mask  = (uint64_t{1} << lane.width) - 1;
      result          |= static_cast<int64_t>((static_cast<uint64_t>(mux_eval(lane.value, values)) & mask) << lane.offset);
    }
  } else if (op == Ntype_op::Set_mask) {
    const auto [lo, hi] = gu::const_of(gu::get_driver_of_sink_name(node, "mask")).get_mask_range();
    const auto mask     = ((uint64_t{1} << (hi - lo)) - 1) << lo;
    const auto base     = static_cast<uint64_t>(mux_eval(gu::get_driver_of_sink_name(node, "a"), values));
    const auto value    = static_cast<uint64_t>(mux_eval(gu::get_driver_of_sink_name(node, "value"), values));
    result              = static_cast<int64_t>((base & ~mask) | ((value << lo) & mask));
  } else if (op == Ntype_op::Get_mask) {
    // Constant contiguous window only: bits [lo,hi) of `a`, LSB-aligned.
    const auto [lo, hi] = gu::const_of(gu::get_driver_of_sink_name(node, "mask")).get_mask_range();
    EXPECT_GE(lo, 0);
    const auto value = mux_eval(gu::get_driver_of_sink_name(node, "a"), values);
    result           = (value >> lo) & ((int64_t{1} << (hi - lo)) - 1);
  } else if (op == Ntype_op::SHL || op == Ntype_op::SRA) {
    // Non-negative test values only, so SRA is a logical shift here.
    const auto value  = mux_eval(gu::get_driver_of_sink_name(node, "a"), values);
    const auto amount = mux_eval(gu::get_driver_of_sink_name(node, "b"), values);
    result            = op == Ntype_op::SHL ? value << amount : value >> amount;
  } else if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Ror || op == Ntype_op::EQ || op == Ntype_op::Xor) {
    result           = op == Ntype_op::And ? -1 : 0;
    bool    first    = true;
    int64_t previous = 0;
    for (auto e_sink : node.inp_sorted_pins()) {
      for (auto e_drv : e_sink.get_driver_pins()) {
        auto value = mux_eval(e_drv, values);
        if (op == Ntype_op::And) {
          result &= value;
        } else if (op == Ntype_op::Or || op == Ntype_op::Ror) {
          result |= value;
        } else if (op == Ntype_op::Xor) {
          result ^= value;
        } else {
          if (first) {
            result = 1;
          } else {
            result &= previous == value;
          }
          previous = value;
          first    = false;
        }
      }
    }
    if (op == Ntype_op::Ror) {
      result = result != 0;
    }
  } else {
    ADD_FAILURE() << "unbound input/state or unsupported evaluator op " << gu::debug_name(node);
  }
  values.emplace(key, result);
  return result;
}

struct Mux_graph {
  std::shared_ptr<hhds::Graph> graph;
  int                          width;
  bool                         signed_data;
  std::vector<Test_pin>        controls;
  Test_pin                     a, b, en, clock;

  Mux_graph(const std::string& name, int count, int bits = 32, bool sign = false, bool wide_control = false)
      : width(bits), signed_data(sign) {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_cprop_mux_sharing");
    auto  io  = lib.create_io(name);
    for (int i = 0; i < count; ++i) {
      const auto s = "c" + std::to_string(i);
      io->add_input(s, i + 1);
      io->set_bits(s, wide_control ? 8 : 1);
    }
    io->add_input("a", count + 1);
    io->set_bits("a", bits);
    io->add_input("b", count + 2);
    io->set_bits("b", bits);
    io->add_input("en", count + 3);
    io->set_bits("en", 1);
    io->add_input("clock", count + 4);
    io->set_bits("clock", 1);
    io->add_output("out", count + 5);
    io->set_bits("out", bits);
    io->add_output("observe", count + 6);
    io->set_bits("observe", bits);
    graph = io->create_graph();
    for (int i = 0; i < count; ++i) {
      controls.push_back(graph->get_input_pin("c" + std::to_string(i)));
    }
    a     = graph->get_input_pin("a");
    b     = graph->get_input_pin("b");
    en    = graph->get_input_pin("en");
    clock = graph->get_input_pin("clock");
    if (sign) {
      gu::set_sbits(a, bits);
      gu::set_sbits(b, bits);
    }
  }
  Test_pin         constant(int64_t value) { return gu::create_const(*graph, *Dlop::create_integer(value)); }
  hhds::Node_class node(Ntype_op op, int bits = 0) {
    auto n = gu::create_typed_node(*graph, op, bits ? bits : width);
    if (signed_data && !bits) {
      gu::set_sbits(n.create_driver_pin(0), width);
    } else {
      gu::set_ubits(n.create_driver_pin(0), bits ? bits : width);
    }
    return n;
  }
  Test_pin mux(Test_pin s, Test_pin f, Test_pin t) {
    auto n = node(Ntype_op::Mux);
    n.create_sink_pin(0).connect_driver(s);
    n.create_sink_pin(1).connect_driver(f);
    n.create_sink_pin(2).connect_driver(t);
    return n.create_driver_pin(0);
  }
  Test_pin eq(Test_pin selector, int64_t value) {
    auto n = node(Ntype_op::EQ, 1);
    // EQ is single-bank: its two operands take consecutive pids, not one pin.
    gu::setup_sink_pid(n, 0).connect_driver(selector);
    gu::setup_sink_pid(n, 0).connect_driver(constant(value));
    return n.create_driver_pin(0);
  }
  // tolg's `x != 0`: (x == 0) ^ 1.
  Test_pin not_zero(Test_pin x) {
    auto n = node(Ntype_op::Xor, 1);
    gu::setup_sink_pid(n, 0).connect_driver(eq(x, 0));
    gu::setup_sink_pid(n, 0).connect_driver(constant(1));
    return n.create_driver_pin(0);
  }
  Test_pin logic(Ntype_op op, std::initializer_list<Test_pin> operands) {
    auto n = node(op, 1);
    for (auto p : operands) {
      gu::setup_sink_pid(n, 0).connect_driver(p);
    }
    return n.create_driver_pin(0);
  }
  Test_pin output() { return graph->get_output_pin("out").get_driver_pins().front(); }
  Test_pin observed() { return graph->get_output_pin("observe").get_driver_pins().front(); }
  size_t   count(Ntype_op op) {
    size_t result = 0;
    for (auto n : graph->body().nodes()) {
      result += gu::type_op_of(n) == op;
    }
    return result;
  }
  Test_values inputs(uint64_t mask, int64_t av, int64_t bv, bool enabled = true, bool wide = false) {
    Test_values result;
    auto        put = [&](Test_pin p, int64_t v) { result.emplace(p.get_class_index().value, v); };
    put(a, av);
    put(b, bv);
    put(en, enabled);
    put(clock, 0);
    for (size_t i = 0; i < controls.size(); ++i) {
      put(controls[i], mask & (uint64_t{1} << (i % 64)) ? (wide ? 32 : 1) : 0);
    }
    return result;
  }
};

TEST(CpropMuxSharing, PriorityTreePreservesEveryControlCombination) {
  for (bool sign : {false, true}) {
    Mux_graph f(sign ? "priority_signed" : "priority_unsigned", 6, 32, sign, true);
    auto      root = f.b;
    for (int i = 5; i >= 0; --i) {
      root = f.mux(f.controls[i], root, i % 2 ? f.b : f.a);
    }
    root.connect_sink(f.graph->get_output_pin("out"));
    Cprop{}.do_trans(f.graph);
    EXPECT_EQ(f.count(Ntype_op::Mux), 0);
    EXPECT_EQ(f.count(Ntype_op::Hotmux), 1);
    EXPECT_EQ(gu::bits_of(f.output()), 32);
    EXPECT_EQ(gu::is_unsign(f.output()), !sign);
    for (uint64_t mask = 0; mask < 64; ++mask) {
      for (int64_t av : {0, 1, 127}) {
        const int64_t bv       = sign ? -117 : 219;
        int64_t       expected = bv;
        for (int i = 0; i < 6; ++i) {
          if (mask & (uint64_t{1} << i)) {
            expected = i % 2 ? bv : av;
            break;
          }
        }
        auto values = f.inputs(mask, av, bv, true, true);
        EXPECT_EQ(mux_eval(f.output(), values), expected) << mask;
      }
    }
    Cprop{}.do_trans(f.graph);
    EXPECT_EQ(f.count(Ntype_op::Hotmux), 1);
  }
}

TEST(CpropMuxSharing, HotmuxGroupsOnlyEstablishedExclusiveControls) {
  for (int proof : {0, 1, 2, 3}) {
    for (bool fallback : {false, true}) {
      Mux_graph f("hot_group_" + std::to_string(proof) + (fallback ? "_default" : "_zero"), 4);
      auto      n = f.node(Ntype_op::Hotmux);
      for (int i = 0; i < 4; ++i) {
        // Proof 1 is a decode; proof 2 is an existing formal certificate;
        // proof 3 deliberately repeats a decoded constant and must be refused.
        auto c = proof == 1 || proof == 3 ? f.eq(f.a, proof == 3 ? i % 2 : i) : f.controls[i];
        n.create_sink_pin(2 * i).connect_driver(c);
        n.create_sink_pin(2 * i + 1).connect_driver(i % 2 ? f.b : f.constant(17));
      }
      if (fallback) {
        n.create_sink_pin(8).connect_driver(f.constant(99));
      }
      if (proof == 2) {
        gu::set_proven(n, gu::kFormalOnehot);
      }
      n.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
      Cprop{}.do_trans(f.graph);
      const auto arms = gu::hotmux_inputs(f.output().get_master_node());
      EXPECT_EQ(arms.arms.size(), proof == 1 || proof == 2 ? 2 : 4);
      if (proof == 0 || proof == 3) {
        EXPECT_FALSE(gu::has_proven(n));
        continue;
      }
      for (uint64_t selection = 0; selection < 5; ++selection) {
        auto          values   = f.inputs(selection < 4 ? uint64_t{1} << selection : 0, selection, 211);
        const int64_t expected = selection == 4 ? (fallback ? 99 : 0) : (selection % 2 ? 211 : 17);
        EXPECT_EQ(mux_eval(f.output(), values), expected);
      }
    }
  }
}

TEST(CpropMuxSharing, DistributedHoldExtractsEnableOnlyForSingleStagePrivateFlop) {
  for (int variant : {0, 1, 2}) {
    Mux_graph f("distributed_hold_" + std::to_string(variant), 6);
    auto      flop = f.node(Ntype_op::Flop);
    auto      q    = flop.create_driver_pin(0);
    gu::setup_sink_by_name(flop, "enable").connect_driver(f.en);
    gu::setup_sink_by_name(flop, "clock_pin").connect_driver(f.clock);
    gu::setup_sink_by_name(flop, "posclk").connect_driver(f.constant(1));
    gu::setup_sink_by_name(flop, "reset_pin").connect_driver(f.controls[5]);
    gu::setup_sink_by_name(flop, "initial").connect_driver(f.constant(37));
    if (variant == 1) {
      gu::setup_sink_by_name(flop, "pipe_min").connect_driver(f.constant(2));
    }
    auto root = q;
    for (int i = 4; i >= 0; --i) {
      root = f.mux(f.controls[i], root, i % 2 ? q : f.a);
    }
    root.connect_sink(gu::setup_sink_by_name(flop, "din"));
    if (variant == 2) {
      root.connect_sink(f.graph->get_output_pin("observe"));
    }
    q.connect_sink(f.graph->get_output_pin("out"));
    optimize_state(f.graph);
    const auto enable = gu::get_driver_of_sink_name(flop, "enable");
    const auto data   = gu::get_driver_of_sink_name(flop, "din");
    EXPECT_EQ(enable == f.en, variant != 0);
    EXPECT_EQ(gu::get_driver_of_sink_name(flop, "reset_pin"), f.controls[5]);
    EXPECT_EQ(gu::const_of(gu::get_driver_of_sink_name(flop, "initial")).to_just_i64(), 37);
    for (uint64_t mask = 0; mask < 64; ++mask) {
      for (bool enabled : {false, true}) {
        auto values                       = f.inputs(mask, 73, 211, enabled);
        values[q.get_class_index().value] = 149;
        int64_t expected                  = 149;
        if (enabled) {
          for (int i = 0; i < 5; ++i) {
            if (mask & (uint64_t{1} << i)) {
              expected = i % 2 ? 149 : 73;
              break;
            }
          }
        }
        // The single-stage transition includes reset, whose priority must stay
        // outside the newly derived enable. For a pipeline compare the input
        // and enable separately, rather than pretending its last Q is stage 1.
        const auto actual = mux_eval(enable, values) ? mux_eval(data, values) : 149;
        EXPECT_EQ(actual, expected);
        if (variant == 0) {
          EXPECT_EQ(mask & 32 ? 37 : actual, mask & 32 ? 37 : expected);
        }
      }
    }
  }
}

TEST(CpropMuxSharing, SharedSubconeRemainsAnOpaqueTerminal) {
  Mux_graph f("shared_boundary", 6);
  auto      shared = f.mux(f.controls[0], f.a, f.b);
  shared.connect_sink(f.graph->get_output_pin("observe"));
  auto root = shared;
  for (int i = 5; i >= 1; --i) {
    root = f.mux(f.controls[i], root, i % 2 ? shared : f.a);
  }
  root.connect_sink(f.graph->get_output_pin("out"));
  Cprop{}.do_trans(f.graph);
  EXPECT_FALSE(shared.is_invalid());
  EXPECT_EQ(gu::type_op_of(shared.get_master_node()), Ntype_op::Mux);
  for (uint64_t mask = 0; mask < 64; ++mask) {
    auto values          = f.inputs(mask, 71, 193);
    auto expected_shared = mask & 1 ? 193 : 71;
    auto expected        = expected_shared;
    for (int i = 1; i < 6; ++i) {
      if (mask & (uint64_t{1} << i)) {
        expected = i % 2 ? expected_shared : 71;
        break;
      }
    }
    EXPECT_EQ(mux_eval(f.output(), values), expected);
    EXPECT_EQ(mux_eval(shared, values), expected_shared);
  }
}

TEST(CpropMuxSharing, DeepPriorityChainHasLinearGeneratedSize) {
  // A path-literal implementation would copy roughly depth^2/2 predicates.
  // This exercises the iterative traversal and checks the generated graph,
  // rather than putting a flaky wall-clock threshold in a regression.
  constexpr int depth = 2048;
  Mux_graph     f("deep_chain", depth);
  auto          root = f.b;
  for (int i = depth - 1; i >= 0; --i) {
    root = f.mux(f.controls[i], root, i % 2 ? f.b : f.a);
  }
  root.connect_sink(f.graph->get_output_pin("out"));
  Cprop{}.do_trans(f.graph);
  size_t nodes = 0, edges = 0;
  for (auto n : f.graph->body().nodes()) {
    ++nodes;
    edges += n.inp_pins_snapshot().size();
  }
  EXPECT_EQ(f.count(Ntype_op::Mux), 0);
  EXPECT_EQ(f.count(Ntype_op::Hotmux), 1);
  EXPECT_LT(nodes, 5 * depth);
  EXPECT_LT(edges, 12 * depth);
}

// The per-bit `if (rst) b <= 1; else if (upd) b <= gi ? 0 : gj ? 1 : q;` nest
// (bedrock's LRU matrix under BR_REGLI) and tolg's shadow enable chain next to
// it. As Mux trees they reached mux sharing, which rebuilt each as a Hotmux
// over path conjunctions; they must become And/Or logic instead, value-exact
// for every input.
TEST(CpropBool, OneBitIfChainsBecomeAndOrLogic) {
  Mux_graph f("bool_if_chain", 4, 1);
  auto      rst  = f.not_zero(f.controls[0]);
  auto      upd  = f.not_zero(f.controls[1]);
  auto      gi   = f.not_zero(f.controls[2]);
  auto      gj   = f.not_zero(f.controls[3]);
  auto      q    = f.not_zero(f.a);
  auto      en   = f.mux(rst, f.mux(upd, f.constant(0), f.constant(1)), f.constant(1));
  auto      next = f.mux(gi, f.mux(gj, q, f.constant(1)), f.constant(0));
  auto      d    = f.mux(rst, f.mux(upd, q, next), f.constant(1));
  en.connect_sink(f.graph->get_output_pin("out"));
  d.connect_sink(f.graph->get_output_pin("observe"));
  optimize_state(f.graph);
  EXPECT_EQ(f.count(Ntype_op::Hotmux), 0);
  EXPECT_EQ(f.count(Ntype_op::Xor), 0);
  // Left: the `upd ? q : next` hold (no flop here to make it dead), and the
  // negated-selector swap's Mux(x,1,0), which scalar_mux keeps on purpose.
  EXPECT_LE(f.count(Ntype_op::Mux), 2);
  for (uint64_t mask = 0; mask < 16; ++mask) {
    for (int64_t qv : {0, 1}) {
      const bool    r = mask & 1, u = mask & 2, i = mask & 4, j = mask & 8;
      const int64_t nxt    = i ? 0 : (j ? 1 : qv);
      auto          values = f.inputs(mask, qv, 0);
      EXPECT_EQ(mux_eval(f.output(), values), r || u ? 1 : 0) << mask;
      auto values2 = f.inputs(mask, qv, 0);
      EXPECT_EQ(mux_eval(f.observed(), values2), r ? 1 : (u ? nxt : qv)) << mask << " q=" << qv;
    }
  }
}

TEST(CpropBool, ComplementaryLiteralsFold) {
  Mux_graph f("bool_complement", 2, 1);
  auto      x  = f.not_zero(f.controls[0]);
  auto      nx = f.eq(f.controls[0], 0);
  auto      y  = f.not_zero(f.controls[1]);
  // and(x, y, !x) is 0 at any width; or(x, !x) is 1 because both are 0/1.
  f.logic(Ntype_op::And, {x, y, nx}).connect_sink(f.graph->get_output_pin("out"));
  f.logic(Ntype_op::Or, {x, nx}).connect_sink(f.graph->get_output_pin("observe"));
  Cprop{}.do_trans(f.graph);
  ASSERT_TRUE(f.output().is_const());
  EXPECT_TRUE(gu::const_of(f.output()).is_known_zero());
  ASSERT_TRUE(f.observed().is_const());
  EXPECT_EQ(gu::const_of(f.observed()).to_just_i64(), 1);
}

// A flop enabled by `rst | upd` samples din only when one of them holds. After
// the reset arm is peeled into an Or, the per-lane hold `upd ? next : q[k]` is
// dead: rst false and the enable true force upd. canonicalize_flop_hold only
// sees a Mux directly on din; this one sits inside a Set_mask lane.
TEST(CpropBool, EnableMakesLaneHoldMuxDead) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cprop_flop_enable_lane");
  auto  io  = lib.create_io("lane_hold");
  io->add_input("rst", 1);
  io->set_bits("rst", 1);
  io->add_input("upd", 2);
  io->set_bits("upd", 1);
  io->add_input("g", 3);
  io->set_bits("g", 1);
  io->add_input("clock", 4);
  io->set_bits("clock", 1);
  io->add_output("q", 5);
  io->set_bits("q", 4);
  auto g        = io->create_graph();
  auto constant = [&](int64_t v) { return gu::create_const(*g, *Dlop::create_integer(v)); };
  auto not_zero = [&](Test_pin x) {
    auto e = gu::create_typed_node(*g, Ntype_op::EQ, 1);
    gu::setup_sink_pid(e, 0).connect_driver(x);
    gu::setup_sink_pid(e, 0).connect_driver(constant(0));
    gu::set_ubits(e.create_driver_pin(0), 1);
    auto n = gu::create_typed_node(*g, Ntype_op::Xor, 1);
    gu::setup_sink_pid(n, 0).connect_driver(e.create_driver_pin(0));
    gu::setup_sink_pid(n, 0).connect_driver(constant(1));
    gu::set_ubits(n.create_driver_pin(0), 1);
    return n.create_driver_pin(0);
  };
  auto mux = [&](Test_pin s, Test_pin f, Test_pin t) {
    auto n = gu::create_typed_node(*g, Ntype_op::Mux, 1);
    n.create_sink_pin(0).connect_driver(s);
    n.create_sink_pin(1).connect_driver(f);
    n.create_sink_pin(2).connect_driver(t);
    gu::set_ubits(n.create_driver_pin(0), 1);
    return n.create_driver_pin(0);
  };
  auto rst  = not_zero(g->get_input_pin("rst"));
  auto upd  = not_zero(g->get_input_pin("upd"));
  auto next = not_zero(g->get_input_pin("g"));
  auto flop = gu::create_typed_node(*g, Ntype_op::Flop, 4);
  auto q    = flop.create_driver_pin(0);
  gu::set_ubits(q, 4);
  q.connect_sink(g->get_output_pin("q"));
  g->get_input_pin("clock").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
  auto enable = gu::create_typed_node(*g, Ntype_op::Or, 1);
  gu::setup_sink_pid(enable, 0).connect_driver(rst);
  gu::setup_sink_pid(enable, 0).connect_driver(upd);
  gu::set_ubits(enable.create_driver_pin(0), 1);
  enable.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(flop, "enable"));
  auto lane_read = gu::create_typed_node(*g, Ntype_op::Get_mask);
  gu::connect_mask_operands(lane_read, q, constant(2));
  gu::set_ubits(lane_read.create_driver_pin(0), 1);
  auto lane = mux(rst, mux(upd, lane_read.create_driver_pin(0), next), constant(1));
  auto din  = gu::create_set_mask(*g, q, constant(2), lane);
  gu::set_ubits(din.create_driver_pin(0), 4);
  din.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(flop, "din"));
  optimize_state(g);
  optimize_state(g);
  size_t muxes = 0;
  for (auto n : g->body().nodes()) {
    muxes += gu::type_op_of(n) == Ntype_op::Mux;
  }
  EXPECT_EQ(muxes, 0);
  // din's lane is now rst | next: check it against every rst/g with the
  // enable held (upd forced when rst is low).
  auto lane_now = gu::get_driver_of_sink_name(flop, "din");
  ASSERT_FALSE(lane_now.is_invalid());
  for (int64_t r : {0, 1}) {
    for (int64_t gv : {0, 1}) {
      Test_values values;
      values.emplace(g->get_input_pin("rst").get_class_index().value, r);
      values.emplace(g->get_input_pin("upd").get_class_index().value, 1);
      values.emplace(g->get_input_pin("g").get_class_index().value, gv);
      values.emplace(q.get_class_index().value, 0);
      EXPECT_EQ((mux_eval(lane_now, values) >> 1) & 1, r ? 1 : gv) << r << gv;
    }
  }
}

// bedrock's pairwise arbiter row, can[i] = AND_{j != i} (!req[j] | prio[i][j]),
// arrives as one And of per-bit Or terms. Every term is the same expression
// over single-bit slices at a shifted position, so the And is one word test
// under a mask; likewise an Or of shifted per-bit Ands.
TEST(CpropBool, BitSliceReductionsBecomeWordTests) {
  Mux_graph f("bool_reduce", 1, 16);
  auto      bit = [&](Test_pin src, int pos) {
    auto n   = gu::create_get_mask(*f.graph, src, pos, pos + 1);
    auto out = n.create_driver_pin(0);
    gu::set_ubits(out, 1);
    return out;
  };
  auto can = f.node(Ntype_op::And, 1);
  for (int j = 0; j < 6; ++j) {
    if (j != 2) {
      auto term = f.logic(Ntype_op::Or, {f.eq(bit(f.a, j), 0), bit(f.b, 8 + j)});
      gu::setup_sink_pid(can, 0).connect_driver(term);
    }
  }
  auto any = f.node(Ntype_op::Or, 1);
  for (int j = 0; j < 5; ++j) {
    gu::setup_sink_pid(any, 0).connect_driver(f.logic(Ntype_op::And, {bit(f.a, j), bit(f.b, j + 3)}));
  }
  can.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
  any.create_driver_pin(0).connect_sink(f.graph->get_output_pin("observe"));
  optimize_state(f.graph);
  // One compare per family instead of a gate per bit.
  EXPECT_LE(f.count(Ntype_op::EQ), 1);
  EXPECT_LE(f.count(Ntype_op::Get_mask), 4);
  uint64_t rng = 0x9E3779B97F4A7C15ULL;
  for (int sample = 0; sample < 3000; ++sample) {
    rng                        ^= rng << 13;
    rng                        ^= rng >> 7;
    rng                        ^= rng << 17;
    const int64_t av            = static_cast<int64_t>(rng & 0xFFFF);
    const int64_t bv            = static_cast<int64_t>((rng >> 16) & 0xFFFF);
    int64_t       expected_can  = 1;
    for (int j = 0; j < 6; ++j) {
      if (j != 2) {
        expected_can &= (((av >> j) & 1) == 0 || ((bv >> (8 + j)) & 1) != 0) ? 1 : 0;
      }
    }
    int64_t expected_any = 0;
    for (int j = 0; j < 5; ++j) {
      expected_any |= ((av >> j) & (bv >> (j + 3)) & 1);
    }
    auto values = f.inputs(0, av, bv);
    EXPECT_EQ(mux_eval(f.output(), values), expected_can) << av << " " << bv;
    auto values2 = f.inputs(0, av, bv);
    EXPECT_EQ(mux_eval(f.observed(), values2), expected_any) << av << " " << bv;
  }
}

// mask(a op b, W) pushes the window into a private bitwise op when at most one
// operand needs a new window: `mask((a << 1) ^ k, 16)` keeps every step at 16
// bits instead of computing a 17-bit value only to cut it back.
TEST(CpropMasks, LowWindowNarrowsPrivateFold) {
  Mux_graph f("mask_narrow_fold", 1, 16);
  auto      shl = f.node(Ntype_op::SHL, 17);
  gu::setup_sink_by_name(shl, "a").connect_driver(f.a);
  gu::setup_sink_by_name(shl, "b").connect_driver(f.constant(1));
  auto fold = f.logic(Ntype_op::Xor, {shl.create_driver_pin(0), f.constant(0x1A5A5)});
  gu::set_ubits(fold, 17);
  auto cut = gu::create_get_mask(*f.graph, fold, 0, 16);
  gu::set_ubits(cut.create_driver_pin(0), 16);
  cut.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
  optimize_state(f.graph);
  for (auto n : f.graph->body().nodes()) {
    for (const auto& pin : n.out_sorted_pins()) {
      EXPECT_LE(gu::bits_of(pin), 16) << gu::debug_name(n);
    }
  }
  for (int64_t av : {0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x1234}) {
    auto values = f.inputs(0, av, 0);
    EXPECT_EQ(mux_eval(f.output(), values), ((av << 1) ^ 0x1A5A5) & 0xFFFF) << av;
  }
}

// ...but windowing EVERY operand splits word-level logic into bit extracts:
// br_enc_gray2bin's `bit0((y >>> 1) ^ y)` became y[1] ^ y[0] per output bit
// (67 -> 167 Get_masks, a 1.25x slower sim). It must stay one word-level Xor.
TEST(CpropMasks, LowWindowDoesNotSplitWordLogicIntoBits) {
  Mux_graph f("mask_no_bit_split", 1, 16);
  auto      shift = [&](Ntype_op op, Test_pin x, int amount) {
    auto n = f.node(op, 16);
    gu::setup_sink_by_name(n, "a").connect_driver(x);
    gu::setup_sink_by_name(n, "b").connect_driver(f.constant(amount));
    return n.create_driver_pin(0);
  };
  auto y = f.logic(Ntype_op::Xor, {f.a, shift(Ntype_op::SRA, f.a, 2)});
  gu::set_ubits(y, 16);
  auto top = f.logic(Ntype_op::Xor, {shift(Ntype_op::SRA, y, 1), y});
  gu::set_ubits(top, 16);
  auto cut = gu::create_get_mask(*f.graph, top, 0, 1);
  gu::set_ubits(cut.create_driver_pin(0), 1);
  cut.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
  optimize_state(f.graph);
  EXPECT_LE(f.count(Ntype_op::Get_mask), 1);
  for (int64_t av : {0, 1, 2, 3, 5, 0x7FFF, 0x8000, 0xFFFF, 0x1234}) {
    const int64_t yv     = av ^ (av >> 2);
    auto          values = f.inputs(0, av, 0);
    EXPECT_EQ(mux_eval(f.output(), values), ((yv >> 1) ^ yv) & 1) << av;
  }
}

// A window that drops a fold to ONE machine word narrows even when both
// operands need a window: Pyrope's `wrap acc = ((acc << 1) | acc#[63]) ^ r`
// otherwise computes a 65-bit value per step only to cut it back to 64.
TEST(CpropMasks, LowWindowKeepsWideFoldInOneWord) {
  Mux_graph f("mask_one_word_fold", 1, 64);
  auto      shl = f.node(Ntype_op::SHL, 65);
  gu::setup_sink_by_name(shl, "a").connect_driver(f.a);
  gu::setup_sink_by_name(shl, "b").connect_driver(f.constant(1));
  auto top_bit = gu::create_get_mask(*f.graph, f.a, 63, 64);
  gu::set_ubits(top_bit.create_driver_pin(0), 1);
  auto rotated = f.logic(Ntype_op::Or, {shl.create_driver_pin(0), top_bit.create_driver_pin(0)});
  gu::set_ubits(rotated, 65);
  auto fold = f.logic(Ntype_op::Xor, {rotated, f.b});
  gu::set_ubits(fold, 65);
  auto cut = gu::create_get_mask(*f.graph, fold, 0, 64);
  gu::set_ubits(cut.create_driver_pin(0), 64);
  cut.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
  optimize_state(f.graph);
  for (auto n : f.graph->body().nodes()) {
    for (const auto& pin : n.out_sorted_pins()) {
      EXPECT_LE(gu::bits_of(pin), 64) << gu::debug_name(n);
    }
  }
}

// Sharing a nest of PROVEN 0/1 values buys 1-bit selects with 1-bit predicate
// gates (one conjunction per path); the nest must stay Muxes, value-exact.
TEST(CpropMuxSharing, ZeroOneSelectionsStayMuxes) {
  Mux_graph f("share_bool01", 5, 1);
  auto      x  = f.not_zero(f.controls[2]);
  auto      y  = f.not_zero(f.controls[3]);
  auto      z  = f.not_zero(f.controls[4]);
  auto      s0 = f.not_zero(f.controls[0]);
  auto      s1 = f.not_zero(f.controls[1]);
  f.mux(s0, f.mux(s1, x, y), f.mux(s1, z, y)).connect_sink(f.graph->get_output_pin("out"));
  Cprop{}.do_trans(f.graph);
  EXPECT_EQ(f.count(Ntype_op::Hotmux), 0);
  for (uint64_t mask = 0; mask < 32; ++mask) {
    const bool b0 = mask & 1, b1 = mask & 2, bx = mask & 4, by = mask & 8, bz = mask & 16;
    const bool expected = b0 ? (b1 ? by : bz) : (b1 ? by : bx);
    auto       values   = f.inputs(mask, 0, 0);
    EXPECT_EQ(mux_eval(f.output(), values), expected ? 1 : 0) << mask;
  }
}

TEST(CpropMuxSharing, WidthHintsDoNotGateSharingButColorsDo) {
  for (bool colored : {false, true}) {
    Mux_graph f(colored ? "colored_mux" : "narrow_mux", 6, colored ? 32 : 1);
    auto      root = f.b;
    for (int i = 5; i >= 0; --i) {
      root = f.mux(f.controls[i], root, i % 2 ? f.b : f.a);
      if (colored) {
        gu::set_color(root.get_master_node(), 7);
      }
    }
    root.connect_sink(f.graph->get_output_pin("out"));
    Cprop{}.do_trans(f.graph);
    EXPECT_EQ(f.count(Ntype_op::Hotmux), colored ? 0 : 1);
  }
}
}  // namespace

// A read of bit zero cannot be resolved through a cycle whose writes affect
// only bit one. Truncating the walk at 64 links used to rotate this read around
// a longer ring forever in the packed-read fixed-point loop.
TEST(CpropMasks, CyclicPackedReadDoesNotRotateAtWalkBudget) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_long_cycle");
  auto  io     = lib.create_io("long_cycle");
  io->add_output("q", 0);
  io->set_bits("q", 1);
  auto                          graph = io->create_graph();
  std::vector<hhds::Node_class> writers;
  for (int i = 0; i < 65; ++i) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Set_mask, 2);
    gu::set_ubits(node.create_driver_pin(0), 2);
    gu::setup_sink_by_name(node, "mask").connect_driver(gu::create_const(*graph, *Dlop::create_integer(2)));
    gu::setup_sink_by_name(node, "value").connect_driver(gu::create_const(*graph, *Dlop::create_integer(1)));
    writers.push_back(node);
  }
  for (size_t i = 0; i < writers.size(); ++i) {
    gu::setup_sink_by_name(writers[i], "a").connect_driver(writers[(i + 1) % writers.size()].get_driver_pin(0));
  }
  auto       read   = gu::create_typed_node(*graph, Ntype_op::Get_mask, 1);
  const auto source = writers.front().get_driver_pin(0);
  gu::setup_sink_by_name(read, "a").connect_driver(source);
  gu::setup_sink_by_name(read, "mask").connect_driver(gu::create_const(*graph, *Dlop::create_integer(1)));
  read.create_driver_pin(0).connect_sink(graph->get_output_pin("q"));
  Cprop cp;
  cp.do_trans(graph);
  EXPECT_FALSE(read.is_invalid());
  EXPECT_EQ(gu::get_driver_of_sink_name(read, "a"), source);
}

// A generate loop that fills a wide packed array one bit per Set_mask and reads
// back only its last row (bedrock's br_mux_bin_structured_gates). The chain is
// longer than canonicalize_set_mask_pack's limit, so the multi-bit read used to
// stay pinned to every link. It must become a Concat of just the lanes it reads.
TEST(CpropMasks, StraddlingSliceOfLongWriteChainGathersItsLanes) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_gather_chain");
  auto  io     = lib.create_io("gather_chain");
  io->add_input("a", 1);
  io->set_bits("a", 8);
  io->add_input("b", 2);
  io->set_bits("b", 8);
  io->add_input("s", 3);
  io->set_bits("s", 1);
  io->add_output("q", 4);
  io->set_bits("q", 4);
  auto g = io->create_graph();

  constexpr int kLinks = 300;  // > kPackChainLimit
  auto          value  = gu::create_const(*g, *Dlop::from_pyrope("0sb?"));
  for (int i = 0; i < kLinks; ++i) {
    auto bit = [&](const char* in) {
      auto get = gu::create_get_mask(*g, g->get_input_pin(in), i % 8, i % 8 + 1);
      gu::set_ubits(get.create_driver_pin(0), 1);
      return get.create_driver_pin(0);
    };
    auto mux = gu::create_typed_node(*g, Ntype_op::Mux);
    gu::setup_sink_pid(mux, 0).connect_driver(g->get_input_pin("s"));
    gu::setup_sink_pid(mux, 1).connect_driver(bit("a"));
    gu::setup_sink_pid(mux, 2).connect_driver(bit("b"));
    gu::set_ubits(mux.create_driver_pin(0), 1);

    auto write = gu::create_typed_node(*g, Ntype_op::Set_mask);
    gu::setup_sink_by_name(write, "a").connect_driver(value);
    gu::setup_sink_by_name(write, "mask").connect_driver(gu::create_const(*g, gu::mask_window_const(i, i + 1)));
    gu::setup_sink_by_name(write, "value").connect_driver(mux.create_driver_pin(0));
    gu::set_ubits(write.create_driver_pin(0), kLinks);
    value = write.create_driver_pin(0);
  }
  auto read = gu::create_get_mask(*g, value, kLinks - 4, kLinks);
  gu::set_ubits(read.create_driver_pin(0), 4);
  read.create_driver_pin(0).connect_sink(g->get_output_pin("q"));

  optimize_state(g);

  for (auto n : g->body().nodes()) {
    EXPECT_NE(gu::type_op_of(n), Ntype_op::Set_mask) << "the bypassed write chain must be swept";
  }
  auto out = g->get_output_pin("q").get_driver_pin();
  ASSERT_FALSE(out.is_invalid());
  auto head = out.get_master_node();
  // The four lane muxes vectorize too: {m299..m296} is one 4-bit Mux over
  // a[3:0] / b[3:0] (300 % 8 == 4, so the lanes read bits 4..7).
  ASSERT_EQ(gu::type_op_of(head), Ntype_op::Mux);
  EXPECT_EQ(gu::bits_of(out), 4);
  size_t muxes = 0;
  for (auto n : g->body().nodes()) {
    muxes += gu::type_op_of(n) == Ntype_op::Mux ? 1 : 0;
  }
  EXPECT_EQ(muxes, 1u);
}

// Indexed reads may resolve a shared writer, but must preserve both the
// narrow observation and the complete word, including lane truncation.
TEST(CpropMasks, StraddlingSlicePreservesSharedWord) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_gather_shared");
  auto  io     = lib.create_io("gather_shared");
  io->add_input("d", 1);
  io->set_bits("d", 1);
  io->add_output("q", 2);
  io->set_bits("q", 2);
  io->add_output("w", 3);
  io->set_bits("w", 8);
  auto g     = io->create_graph();
  auto value = gu::create_const(*g, *Dlop::create_integer(0));
  for (int i = 0; i < 8; ++i) {
    auto write = gu::create_typed_node(*g, Ntype_op::Set_mask);
    gu::setup_sink_by_name(write, "a").connect_driver(value);
    gu::setup_sink_by_name(write, "mask").connect_driver(gu::create_const(*g, gu::mask_window_const(i, i + 1)));
    gu::setup_sink_by_name(write, "value").connect_driver(g->get_input_pin("d"));
    gu::set_ubits(write.create_driver_pin(0), 8);
    value = write.create_driver_pin(0);
  }
  auto read = gu::create_get_mask(*g, value, 3, 5);
  gu::set_ubits(read.create_driver_pin(0), 2);
  read.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
  value.connect_sink(g->get_output_pin("w"));

  Cprop{}.do_trans(g);

  auto q = g->get_output_pin("q").get_driver_pin();
  ASSERT_FALSE(q.is_invalid());
  const auto word = g->get_output_pin("w").get_driver_pin();
  ASSERT_FALSE(word.is_invalid());
  for (int64_t data : {-2, -1, 0, 1, 2, 3, 255, 256}) {
    Test_values values{
        {static_cast<uint64_t>(g->get_input_pin("d").get_class_index().value), data}
    };
    EXPECT_EQ(mux_eval(q, values), (data & 1) ? 3 : 0);
    EXPECT_EQ(mux_eval(word, values), (data & 1) ? 255 : 0);
  }
}

// W one-bit muxes sharing a select over consecutive bits of the same two words
// are one W-bit mux, and a tree of them vectorizes level by level: a 4:1 x 8
// structured-gates mux (24 one-bit mux2 cells) becomes three 8-bit muxes.
TEST(CpropMux, BitSlicedMuxTreeVectorizes) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_vectorize");
  auto  io     = lib.create_io("vectorize_tree");
  io->add_input("in", 1);
  io->set_bits("in", 32);
  io->add_input("s0", 2);
  io->set_bits("s0", 1);
  io->add_input("s1", 3);
  io->set_bits("s1", 1);
  io->add_output("q", 4);
  io->set_bits("q", 8);
  auto g   = io->create_graph();
  auto in  = g->get_input_pin("in");
  auto mux = [&](const hhds::Pin_class& sel, const hhds::Pin_class& x, const hhds::Pin_class& y) {
    auto m = gu::create_typed_node(*g, Ntype_op::Mux);
    gu::setup_sink_pid(m, 0).connect_driver(sel);
    gu::setup_sink_pid(m, 1).connect_driver(x);
    gu::setup_sink_pid(m, 2).connect_driver(y);
    gu::set_ubits(m.create_driver_pin(0), 1);
    return m.create_driver_pin(0);
  };
  auto bit = [&](const hhds::Pin_class& src, int pos) {
    auto get = gu::create_get_mask(*g, src, pos, pos + 1);
    gu::set_ubits(get.create_driver_pin(0), 1);
    return get.create_driver_pin(0);
  };
  std::vector<hhds::Pin_class> level1;
  for (int i = 0; i < 8; ++i) {
    auto lo = mux(g->get_input_pin("s0"), bit(in, i), bit(in, 8 + i));
    auto hi = mux(g->get_input_pin("s0"), bit(in, 16 + i), bit(in, 24 + i));
    level1.push_back(mux(g->get_input_pin("s1"), lo, hi));
  }
  auto cat = gu::create_typed_node(*g, Ntype_op::Concat);
  for (int i = 0; i < 8; ++i) {  // MSB-first lanes
    gu::setup_sink_pid(cat, 2 * i).connect_driver(level1[7 - i]);
    gu::setup_sink_pid(cat, 2 * i + 1).connect_driver(gu::create_const(*g, *Dlop::create_integer(1)));
  }
  gu::set_ubits(cat.create_driver_pin(0), 8);
  cat.create_driver_pin(0).connect_sink(g->get_output_pin("q"));

  optimize_state(g);

  size_t muxes = 0;
  for (auto n : g->body().nodes()) {
    if (gu::type_op_of(n) == Ntype_op::Mux) {
      ++muxes;
      EXPECT_EQ(gu::bits_of(n.get_driver_pin(0)), 8) << "no one-bit lane mux may survive";
    }
  }
  EXPECT_EQ(muxes, 3u);
  auto q = g->get_output_pin("q").get_driver_pin();
  ASSERT_FALSE(q.is_invalid());
  auto top = q.get_master_node();
  ASSERT_EQ(gu::type_op_of(top), Ntype_op::Mux) << "the re-packing Concat must fold into the word mux";
  auto sel = top.get_sink_pin(0).get_driver_pins();
  ASSERT_EQ(sel.size(), 1u);
  EXPECT_EQ(sel.front(), g->get_input_pin("s1"));
}

// Every intermediate word has a whole-word observer as well as a slice read.
// Expanding all prefix layouts would manufacture 1+2+...+N lane operands.
TEST(CpropMasks, SharedWriteVersionsHaveBoundedExpansion) {
  for (int size : {128, 256, 512, 1024}) {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_cprop_shared_scaling");
    auto  io  = lib.create_io("versions_" + std::to_string(size));
    io->add_input("a", 1);
    io->set_bits("a", size);
    for (int i = 0; i < size; ++i) {
      io->add_output("word" + std::to_string(i), 2 * i + 2);
      io->set_bits("word" + std::to_string(i), size);
      io->add_output("bit" + std::to_string(i), 2 * i + 3);
      io->set_bits("bit" + std::to_string(i), 1);
    }
    auto graph  = io->create_graph();
    auto source = graph->get_input_pin("a");
    auto word   = gu::create_const(*graph, *Dlop::create_integer(0));
    for (int i = 0; i < size; ++i) {
      auto value = gu::create_get_mask(*graph, source, i, i + 1).create_driver_pin(0);
      auto mask  = gu::create_const(*graph, gu::mask_window_const(i, i + 1));
      word       = gu::create_set_mask(*graph, word, mask, value).create_driver_pin(0);
      word.connect_sink(graph->get_output_pin("word" + std::to_string(i)));
      auto read = gu::create_get_mask(*graph, word, i, i + 1).create_driver_pin(0);
      read.connect_sink(graph->get_output_pin("bit" + std::to_string(i)));
    }
    const auto start = std::chrono::steady_clock::now();
    Cprop{}.do_trans(graph);
    const auto ms    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    size_t     nodes = 0, edges = 0;
    for (auto node : graph->body().nodes()) {
      ++nodes;
      for ([[maybe_unused]] auto input : node.inp_sorted_pins()) {
        ++edges;
      }
    }
    EXPECT_LT(nodes, static_cast<size_t>(12 * size));
    EXPECT_LT(edges, static_cast<size_t>(30 * size));
    for (int i = 0; i < size; ++i) {
      const auto read = graph->get_output_pin("bit" + std::to_string(i)).get_driver_pin();
      ASSERT_EQ(gu::type_op_of(read.get_master_node()), Ntype_op::Get_mask);
      EXPECT_EQ(gu::get_driver_of_sink_name(read.get_master_node(), "a"), source);
      EXPECT_EQ(gu::mask_window_of(gu::const_of(gu::get_driver_of_sink_name(read.get_master_node(), "mask"))),
                (std::optional<std::pair<int, int>>{
                    {i, i + 1}
      }));
    }
    std::cout << "shared-versions n=" << size << " nodes=" << nodes << " edges=" << edges << " ms=" << ms << '\n';
  }
}

// Each old version must retain its own overwritten value after later writes.
TEST(CpropMasks, SharedOverwriteVersionsPreserveSignedSlices) {
  Mux_graph f("shared_overwrites", 0, 8, true);
  auto      write = [&](Test_pin base, Test_pin value) {
    return gu::create_set_mask(*f.graph, base, f.constant(6), value).create_driver_pin(0);
  };
  auto first = write(f.a, f.b);
  auto last  = write(first, f.constant(1));
  first.connect_sink(f.graph->get_output_pin("observe"));
  last.connect_sink(f.graph->get_output_pin("out"));
  Cprop{}.do_trans(f.graph);
  for (int64_t a : {-128, -3, -1, 0, 5, 127}) {
    for (int64_t b : {-9, -1, 0, 1, 2, 3, 8}) {
      auto v1 = f.inputs(0, a, b), v2 = v1;
      EXPECT_EQ(mux_eval(f.observed(), v1), (a & ~6LL) | ((b & 3) << 1));
      EXPECT_EQ(mux_eval(f.output(), v2), (a & ~6LL) | 2);
    }
  }
}

// A private conjunction specialized under its Or parent's condition is no
// longer the expression entered into CSE at its earlier forward visit.
TEST(CpropBool, SpecializedProducerInvalidatesItsCseEntry) {
  Mux_graph f("specialized_cse", 2, 1);
  auto      x     = f.not_zero(f.controls[0]);
  auto      nx    = f.eq(f.controls[0], 0);
  auto      y     = f.not_zero(f.controls[1]);
  auto      first = f.logic(Ntype_op::And, {nx, y});
  f.logic(Ntype_op::Or, {x, first}).connect_sink(f.graph->get_output_pin("out"));
  f.logic(Ntype_op::And, {nx, y}).connect_sink(f.graph->get_output_pin("observe"));
  optimize_state(f.graph);
  for (uint64_t mask = 0; mask < 4; ++mask) {
    auto a = f.inputs(mask, 0, 0), b = a;
    EXPECT_EQ(mux_eval(f.output(), a), bool(mask & 1) || bool(mask & 2));
    EXPECT_EQ(mux_eval(f.observed(), b), !bool(mask & 1) && bool(mask & 2));
  }
}
