//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cprop.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "absl/container/flat_hash_set.h"
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

// The compile schedule as pass.cprop runs it: bitwidth follows every sweep,
// so the rewrites that mint unstamped cells are on. `guard_scope` is what
// pass.cprop passes for the graph (whole_graph() on a lifted loop body).
void optimize_lanes(const std::shared_ptr<hhds::Graph>& graph, const Cprop::Node_scope& guard_scope = {}) {
  Cprop cprop{true};
  cprop.set_guard_scope(guard_scope);
  cprop.do_trans(graph);
  Bitwidth{10}.do_trans(graph);
  Enableopt{}.do_trans(graph);
  cprop.do_trans(graph);
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
    livehd::graph_util::connect_bit_range(mask, 0, 8);
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
    gu::connect_bit_range(write, 4 * (i / 3), 4 * (i / 3) + 4);
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
    gu::connect_bit_range(write, 4 * lane, 4 * lane + 4);
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

// A yosys bmuxmap SRAM read tree over a non-power-of-two memory: the pair of
// missing entries (30, 31 of a 30-entry array) reads two UNDRIVEN wires, which
// yosys2lg imports as input-less placeholder Or nodes. The mux over them shares
// its select (addr[0]) with every sibling pair mux. Whatever cprop does with
// the undefined pair, the defined pair must keep selecting on addr[0].
TEST(CpropMux, UndrivenArmsDoNotDropASharedSelect) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_undriven_mux_arms");
  auto  io     = lib.create_io("undriven_arms");
  io->add_input("ra", 1);
  io->set_bits("ra", 5);
  io->set_unsign("ra", true);
  io->add_input("d0", 2);
  io->set_bits("d0", 32);
  io->set_unsign("d0", true);
  io->add_input("d1", 3);
  io->set_bits("d1", 32);
  io->set_unsign("d1", true);
  io->add_output("y", 4);
  io->set_bits("y", 32);
  auto g    = io->create_graph();
  auto ra   = g->get_input_pin("ra");
  auto sel0 = gu::create_get_mask(*g, ra, 0, 1).create_driver_pin(0);
  auto sel1 = gu::create_get_mask(*g, ra, 1, 2).create_driver_pin(0);
  gu::set_ubits(sel0, 1);
  gu::set_ubits(sel1, 1);
  auto mux2 = [&](const hhds::Pin_class& s, const hhds::Pin_class& a, const hhds::Pin_class& b) {
    auto m = gu::create_typed_node(*g, Ntype_op::Mux, 32);
    m.create_sink_pin(0).connect_driver(s);
    m.create_sink_pin(1).connect_driver(a);
    m.create_sink_pin(2).connect_driver(b);
    return m.create_driver_pin(0);
  };
  auto u0 = gu::create_typed_node(*g, Ntype_op::Or, 32).create_driver_pin(0);  // undriven placeholders
  auto u1 = gu::create_typed_node(*g, Ntype_op::Or, 32).create_driver_pin(0);
  auto m0 = mux2(sel0, g->get_input_pin("d0"), g->get_input_pin("d1"));
  auto m1 = mux2(sel0, u0, u1);
  mux2(sel1, m0, m1).connect_sink(g->get_output_pin("y"));
  optimize_state(g);

  // y must still depend on ra[0] (through the d0/d1 pair).
  bool                          reaches_d1 = false;
  std::vector<hhds::Node_class> stack{g->get_output_pin("y").get_driver_pin().get_master_node()};
  std::unordered_set<uint64_t>  seen;
  while (!stack.empty()) {
    auto n = stack.back();
    stack.pop_back();
    if (!seen.insert(static_cast<uint64_t>(n.get_class_index().value)).second) {
      continue;
    }
    for (const auto& sink : n.inp_sorted_pins()) {
      auto d = sink.get_driver_pin();
      if (d.is_invalid()) {
        continue;
      }
      if (d == g->get_input_pin("d1")) {
        reaches_d1 = true;
      }
      if (!d.is_const() && !gu::is_graph_input_pin(d)) {
        stack.push_back(d.get_master_node());
      }
    }
  }
  EXPECT_TRUE(reaches_d1) << "the defined pair lost its addr[0] select (d1 unreachable)";
}

// ---- Q1: an out-of-range or disconnected Mux selection is '?' ---------------
//
// todo/livehd/2a-aegraph.html, ruling Q1: for Mux(s, p1..pn) a selection that
// is out of range or names a disconnected arm yields a '?' the optimizer may
// resolve to any value (an arm, 0, ...). An in-range selection of a CONNECTED
// arm must keep that arm. The tests below pin both halves.

using Q1_inputs = std::vector<std::pair<hhds::Pin_class, int64_t>>;

// Port-id-aware evaluator. A Mux reads arm port sel+1; a two-arm Mux (ports
// 0,1,2) is a condition, so any nonzero selector picks port 2 (a refinement of
// the out-of-range '?'). '?' is std::nullopt and poisons whatever reads it.
std::optional<int64_t> q1_eval(const hhds::Pin_class& pin, const Q1_inputs& inputs) {
  namespace gu = livehd::graph_util;
  if (pin.is_invalid()) {
    return std::nullopt;
  }
  for (const auto& [p, v] : inputs) {
    if (p == pin) {
      return v;
    }
  }
  if (pin.is_const()) {
    const auto& c = gu::const_of(pin);
    if (c.has_unknowns() || !c.is_just_i64()) {
      return std::nullopt;
    }
    return c.to_just_i64();
  }
  if (gu::is_graph_input_pin(pin)) {
    ADD_FAILURE() << "unbound graph input";
    return std::nullopt;
  }
  const auto node = pin.get_master_node();
  const auto op   = gu::type_op_of(node);
  const auto at   = [&](hhds::Port_id pid) -> std::optional<int64_t> {
    for (const auto& sink : node.inp_pins_snapshot()) {
      if (sink.get_port_id() == pid) {
        return q1_eval(sink.get_driver_pin(), inputs);
      }
    }
    return std::nullopt;
  };
  const auto low_mask = [](int bits) { return bits >= 63 ? int64_t{-1} : (int64_t{1} << bits) - 1; };
  if (op == Ntype_op::Mux) {
    const auto pins    = node.inp_pins_snapshot();
    const bool two_arm = pins.size() == 3 && pins[0].get_port_id() == 0 && pins[1].get_port_id() == 1 && pins[2].get_port_id() == 2;
    const auto sel     = at(0);
    if (!sel || (!two_arm && *sel < 0)) {
      return std::nullopt;
    }
    const auto port = two_arm ? hhds::Port_id(*sel != 0 ? 2 : 1) : static_cast<hhds::Port_id>(*sel + 1);
    return at(port);  // a disconnected arm is '?'
  }
  if (op == Ntype_op::Get_mask || op == Ntype_op::Set_mask) {
    const auto [lo, hi] = *gu::bit_range(node);
    const auto a        = q1_eval(gu::get_driver_of_sink_name(node, "a"), inputs);
    if (!a) {
      return std::nullopt;
    }
    if (op == Ntype_op::Get_mask) {
      return (*a >> lo) & low_mask(hi - lo);
    }
    const auto value = q1_eval(gu::get_driver_of_sink_name(node, "value"), inputs);
    if (!value) {
      return std::nullopt;
    }
    const auto mask = low_mask(hi - lo) << lo;
    return (*a & ~mask) | ((*value << lo) & mask);
  }
  if (op == Ntype_op::Sext) {
    const auto value = at(0), bits = at(1);
    if (!value || !bits) {
      return std::nullopt;
    }
    const auto sign = int64_t{1} << (*bits - 1);
    return (*value & sign) ? (*value | ~(sign - 1)) : (*value & (sign - 1));
  }
  if (op == Ntype_op::Concat) {
    int64_t result = 0;
    for (const auto& lane : gu::concat_lanes(node)) {
      const auto value = q1_eval(lane.value, inputs);
      if (!value) {
        return std::nullopt;
      }
      result |= (*value & low_mask(lane.width)) << lane.offset;
    }
    return result;
  }
  ADD_FAILURE() << "unsupported q1_eval op " << gu::debug_name(node);
  return std::nullopt;
}

// P2: an n-ary Mux with arms on ports 0,2,3 (port 1 unconnected) has three
// edges, but it is NOT a two-arm Mux. Selector 1 is in range and connected, so
// it must keep p2; every two-arm rewrite that reads edges [1],[2] as the
// false/true arms would make it pick p3.
TEST(CpropMux, GappedMuxKeepsArmPositions) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_gapped_mux");
  enum Shape { ZeroAndSelector, ConstArms01, SetMaskPair, SetMaskArm };
  for (const auto shape : {ZeroAndSelector, ConstArms01, SetMaskPair, SetMaskArm}) {
    auto io = lib.create_io("gapped_mux_" + std::to_string(shape));
    io->add_input("s", 1);
    io->set_bits("s", 2);
    io->set_unsign("s", true);
    io->add_input("base", 2);
    io->set_bits("base", 8);
    io->set_unsign("base", true);
    io->add_input("v2", 3);
    io->set_bits("v2", 4);
    io->set_unsign("v2", true);
    io->add_input("v3", 4);
    io->set_bits("v3", 4);
    io->set_unsign("v3", true);
    io->add_output("q", 5);
    io->set_bits("q", 8);
    auto       g    = io->create_graph();
    const auto s    = g->get_input_pin("s");
    const auto base = g->get_input_pin("base");
    const auto set_lane
        = [&](const hhds::Pin_class& value) { return gu::create_set_mask(*g, base, value, 0, 4).create_driver_pin(0); };
    hhds::Pin_class sel = s, p2, p3;
    switch (shape) {
      case ZeroAndSelector:  // s ? s : 0 shape on edges [1],[2]
        p2 = gu::create_const(*g, *Dlop::create_integer(0));
        p3 = s;
        break;
      case ConstArms01:  // Mux(x, 0, 1) == x shape on edges [1],[2]
        sel = gu::create_get_mask(*g, s, 0, 1).create_driver_pin(0);
        gu::set_ubits(sel, 1);
        p2 = gu::create_const(*g, *Dlop::create_integer(0));
        p3 = gu::create_const(*g, *Dlop::create_integer(1));
        break;
      case SetMaskPair:  // factor_set_mask_pair shape
        p2 = set_lane(g->get_input_pin("v2"));
        p3 = set_lane(g->get_input_pin("v3"));
        break;
      case SetMaskArm:  // factor_set_mask_arm shape
        p2 = base;
        p3 = set_lane(g->get_input_pin("v2"));
        break;
    }
    auto mux = gu::create_typed_node(*g, Ntype_op::Mux, 8);
    mux.create_sink_pin(0).connect_driver(sel);
    mux.create_sink_pin(2).connect_driver(p2);
    mux.create_sink_pin(3).connect_driver(p3);
    mux.create_driver_pin(0).connect_sink(g->get_output_pin("q"));

    std::vector<Q1_inputs> cases;
    for (int64_t sv = 0; sv < 4; ++sv) {
      cases.push_back({
          {                     s,   sv},
          {                  base, 0xA5},
          {g->get_input_pin("v2"),  0x3},
          {g->get_input_pin("v3"),  0xC}
      });
    }
    std::vector<std::optional<int64_t>> golden;
    for (const auto& in : cases) {
      golden.push_back(q1_eval(g->get_output_pin("q").get_driver_pin(), in));
    }
    Cprop{}.do_trans(g);
    for (size_t i = 0; i < cases.size(); ++i) {
      if (!golden[i]) {
        continue;  // '?' (Q1): any result is a legal refinement
      }
      EXPECT_EQ(q1_eval(g->get_output_pin("q").get_driver_pin(), cases[i]), golden[i]) << "shape " << shape << " s=" << i;
    }
  }
}

// P1 (legal under Q1): the equal-arms collapse compares only the CONNECTED
// arms, so Mux(s, a, a, a) and Mux(s, a, <unconnected>, a) both become `a`:
// every out-of-range or disconnected selection is a '?' that may be `a`.
TEST(CpropMux, Q1EqualArmsCollapseIgnoresUnselectableArms) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_q1_equal_arms");
  for (bool gapped : {false, true}) {
    auto io = lib.create_io(gapped ? "q1_equal_arms_gapped" : "q1_equal_arms");
    io->add_input("s", 1);
    io->set_bits("s", 2);
    io->add_input("a", 2);
    io->set_bits("a", 8);
    io->add_output("q", 3);
    io->set_bits("q", 8);
    auto g   = io->create_graph();
    auto a   = g->get_input_pin("a");
    auto mux = gu::create_typed_node(*g, Ntype_op::Mux, 8);
    mux.create_sink_pin(0).connect_driver(g->get_input_pin("s"));
    mux.create_sink_pin(1).connect_driver(a);
    if (!gapped) {
      mux.create_sink_pin(2).connect_driver(a);
    }
    mux.create_sink_pin(3).connect_driver(a);
    mux.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
    Cprop{}.do_trans(g);
    EXPECT_TRUE(g->get_output_pin("q").get_driver_pin() == a) << (gapped ? "gapped" : "dense");
  }
}

// P1 (legal under Q1): a constant selector keeps an in-range connected arm and
// resolves an out-of-range or disconnected selection to SOME value (today a
// fresh 0; any connected arm would be just as legal).
TEST(CpropMux, Q1ConstantSelectorFold) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_q1_const_sel");
  int   id     = 0;
  for (bool const_arms : {false, true}) {
    for (bool gapped : {false, true}) {
      for (int64_t sel : {0, 1, 2, 5}) {
        auto io = lib.create_io("q1_const_sel_" + std::to_string(id++));
        io->add_input("a", 1);
        io->set_bits("a", 8);
        io->add_input("b", 2);
        io->set_bits("b", 8);
        io->add_input("c", 3);
        io->set_bits("c", 8);
        io->add_output("q", 4);
        io->set_bits("q", 8);
        auto g   = io->create_graph();
        auto arm = [&](const char* name, int value) {
          return const_arms ? gu::create_const(*g, *Dlop::create_integer(value)) : g->get_input_pin(name);
        };
        // Port 1 is unconnected in the gapped form; ports 2,3 carry b,c.
        std::vector<std::pair<hhds::Port_id, hhds::Pin_class>> arms;
        if (!gapped) {
          arms.emplace_back(1, arm("a", 10));
        }
        arms.emplace_back(2, arm("b", 20));
        arms.emplace_back(3, arm("c", 30));
        auto mux = gu::create_typed_node(*g, Ntype_op::Mux, 8);
        mux.create_sink_pin(0).connect_driver(gu::create_const(*g, *Dlop::create_integer(sel)));
        for (const auto& [pid, drv] : arms) {
          mux.create_sink_pin(pid).connect_driver(drv);
        }
        mux.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
        Cprop{}.do_trans(g);

        const auto      out = g->get_output_pin("q").get_driver_pin();
        hhds::Pin_class selected;
        for (const auto& [pid, drv] : arms) {
          if (static_cast<int64_t>(pid) == sel + 1) {
            selected = drv;
          }
        }
        if (!selected.is_invalid()) {
          if (const_arms) {
            ASSERT_TRUE(out.is_const());
            EXPECT_EQ(gu::const_of(out).to_just_i64(), gu::const_of(selected).to_just_i64()) << "sel=" << sel;
          } else {
            EXPECT_TRUE(out == selected) << "sel=" << sel << " gapped=" << gapped;
          }
          continue;
        }
        // '?' (out of range or disconnected): a constant or any connected arm.
        bool is_arm = false;
        for (const auto& [pid, drv] : arms) {
          is_arm = is_arm || out == drv;
        }
        EXPECT_TRUE(out.is_const() || is_arm) << "sel=" << sel << " gapped=" << gapped;
      }
    }
  }
}

// P3 (legal under Q1): scalar_sext replaces a Sext(x,1) selector by the 0/1
// `x` on EVERY Mux, n-ary included. x=0 keeps arm 1; x=1 was Sext = -1, out
// of range, a '?' that the bypass resolves to arm 2.
TEST(CpropMux, Q1SextSelectorBypassOnIndexedMux) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_q1_sext_sel");
  auto  io     = lib.create_io("q1_sext_sel");
  io->add_input("in", 1);
  io->set_bits("in", 4);
  io->set_unsign("in", true);
  io->add_input("a", 2);
  io->set_bits("a", 8);
  io->add_input("b", 3);
  io->set_bits("b", 8);
  io->add_input("c", 4);
  io->set_bits("c", 8);
  io->add_output("q", 5);
  io->set_bits("q", 8);
  auto g = io->create_graph();
  auto x = gu::create_get_mask(*g, g->get_input_pin("in"), 0, 1).create_driver_pin(0);
  gu::set_ubits(x, 1);
  auto sext = gu::create_typed_node(*g, Ntype_op::Sext);
  sext.create_sink_pin(0).connect_driver(x);
  sext.create_sink_pin(1).connect_driver(gu::create_const(*g, *Dlop::create_integer(1)));
  auto mux = gu::create_typed_node(*g, Ntype_op::Mux, 8);
  mux.create_sink_pin(0).connect_driver(sext.create_driver_pin(0));
  mux.create_sink_pin(1).connect_driver(g->get_input_pin("a"));
  mux.create_sink_pin(2).connect_driver(g->get_input_pin("b"));
  mux.create_sink_pin(3).connect_driver(g->get_input_pin("c"));
  mux.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
  Cprop{}.do_trans(g);

  const auto out = g->get_output_pin("q").get_driver_pin();
  ASSERT_FALSE(out.is_const());
  ASSERT_EQ(gu::type_op_of(out.get_master_node()), Ntype_op::Mux);
  const auto sels = out.get_master_node().get_sink_pin(0).get_driver_pins();
  ASSERT_EQ(sels.size(), 1);
  EXPECT_NE(gu::type_op_of(sels.front().get_master_node()), Ntype_op::Sext) << "the Sext(x,1) selector bypass did not fire";
  const Q1_inputs in0{
      {g->get_input_pin("in"), 0},
      { g->get_input_pin("a"), 7},
      { g->get_input_pin("b"), 8},
      { g->get_input_pin("c"), 9}
  };
  EXPECT_EQ(q1_eval(out, in0), std::optional<int64_t>{7});
}

// P10 / Q3: LGraph has no types; a string arm is a Dlop constant, NOT zero.
// Mux(s, "str", s) is `s ? s : "str"`, so it must not collapse to `s` (which
// is what Mux(s, 0, s) does, kept here as the control).
TEST(CpropMux, StringArmIsNotZero) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_string_arm");
  for (bool string_arm : {false, true}) {
    auto io = lib.create_io(string_arm ? "string_arm" : "zero_arm");
    io->add_input("s", 1);
    io->set_bits("s", 8);
    io->add_output("q", 2);
    io->set_bits("q", 8);
    auto g   = io->create_graph();
    auto s   = g->get_input_pin("s");
    auto mux = gu::create_typed_node(*g, Ntype_op::Mux, 8);
    mux.create_sink_pin(0).connect_driver(s);
    mux.create_sink_pin(1).connect_driver(
        gu::create_const(*g, string_arm ? *Dlop::create_string("str") : *Dlop::create_integer(0)));
    mux.create_sink_pin(2).connect_driver(s);
    mux.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
    Cprop{}.do_trans(g);
    EXPECT_EQ(g->get_output_pin("q").get_driver_pin() == s, !string_arm) << (string_arm ? "string" : "zero");
  }
}

// P4: a Div/Rem with ONE operand disconnected. A missing operand is an invalid
// value, which LGraph reads as all-unknown '?' (Q2): `a / ?` and `a % ?` may be
// `a`, and `? / b` and `? % b` may be 0. The old rule forwarded the lone
// driver of a Div whatever its pin, so a b-only Div became b. A lone CONSTANT
// operand used to reach the all-constant fold, which read a second edge that
// does not exist.
TEST(CpropDivRem, SingleInputForwardsOnlyTheDividend) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_single_input_div");
  int   index  = 0;
  for (const auto op : {Ntype_op::Div, Ntype_op::Rem}) {
    SCOPED_TRACE(std::string{Ntype::get_name(op)});
    for (const bool on_b : {false, true}) {
      for (const bool constant : {false, true}) {
        auto io = lib.create_io("single_divrem_" + std::to_string(index++));
        io->add_input("x", 1);
        io->set_bits("x", 8);
        io->add_output("q", 2);
        io->set_bits("q", 8);
        auto       g   = io->create_graph();
        const auto x   = constant ? gu::create_const(*g, *Dlop::create_integer(6)) : g->get_input_pin("x");
        auto       div = gu::create_typed_node(*g, op, 8);
        gu::setup_sink_by_name(div, on_b ? "b" : "a").connect_driver(x);
        div.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
        Cprop{}.do_trans(g);
        const auto out = g->get_output_pin("q").get_driver_pin();
        ASSERT_FALSE(out.is_invalid());
        if (on_b) {
          ASSERT_TRUE(out.is_const()) << "a b-only Div/Rem is 0, never b (constant=" << constant << ")";
          EXPECT_TRUE(gu::const_of(out).is_known_zero());
        } else {
          EXPECT_TRUE(out == x) << "an a-only Div/Rem forwards a (constant=" << constant << ")";
        }
      }
    }
  }
}

// Q2: x/0 and x%0 are all-unknown '?' (hlop Dlop::div_op/rem_op), whether or
// not x is a constant, and that '?' is a fresh literal owned by the node. A
// divisor that only MAY be zero (an operand '?' literal) is different: a fresh
// all-unknown result would split that literal, so the Div/Rem stays.
TEST(CpropDivRem, DivideOrRemByZeroIsAllUnknown) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_div_by_zero");
  int   index  = 0;
  for (const auto op : {Ntype_op::Div, Ntype_op::Rem}) {
    SCOPED_TRACE(std::string{Ntype::get_name(op)});
    for (const bool constant_a : {false, true}) {
      for (const bool maybe_zero : {false, true}) {
        auto io = lib.create_io("divrem_zero_" + std::to_string(index++));
        io->add_input("x", 1);
        io->set_bits("x", 8);
        io->add_output("q", 2);
        io->set_bits("q", 8);
        auto       g   = io->create_graph();
        const auto a   = constant_a ? gu::create_const(*g, *Dlop::create_integer(7)) : g->get_input_pin("x");
        const auto b   = gu::create_const(*g, maybe_zero ? *Dlop::from_pyrope("0ub0?") : *Dlop::create_integer(0));
        auto       div = gu::create_typed_node(*g, op, 8);
        gu::setup_sink_by_name(div, "a").connect_driver(a);
        gu::setup_sink_by_name(div, "b").connect_driver(b);
        div.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
        Cprop{}.do_trans(g);
        const auto out = g->get_output_pin("q").get_driver_pin();
        ASSERT_FALSE(out.is_invalid());
        if (maybe_zero) {
          EXPECT_EQ(gu::type_op_of(out.get_master_node()), op) << "constant_a=" << constant_a;
        } else {
          ASSERT_TRUE(out.is_const()) << "constant_a=" << constant_a;
          EXPECT_TRUE(gu::const_of(out).has_unknowns());
          EXPECT_TRUE(gu::const_of(out).may_be_zero());
        }
      }
    }
  }
}

// P8: Not(Not(x)) == x on unlimited integers. The inner Not survives when it
// has another reader.
TEST(CpropNot, DoubleNotForwardsTheOperand) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_double_not");
  for (const bool shared_inner : {false, true}) {
    auto io = lib.create_io(shared_inner ? "double_not_shared" : "double_not");
    io->add_input("x", 1);
    io->set_bits("x", 8);
    io->add_output("q", 2);
    io->set_bits("q", 8);
    io->add_output("r", 3);
    io->set_bits("r", 8);
    auto       g     = io->create_graph();
    const auto x     = g->get_input_pin("x");
    auto       inner = gu::create_typed_node(*g, Ntype_op::Not, 8);
    gu::setup_sink_by_name(inner, "a").connect_driver(x);
    auto outer = gu::create_typed_node(*g, Ntype_op::Not, 8);
    gu::setup_sink_by_name(outer, "a").connect_driver(inner.create_driver_pin(0));
    outer.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
    if (shared_inner) {
      inner.create_driver_pin(0).connect_sink(g->get_output_pin("r"));
    }
    Cprop{}.do_trans(g);
    EXPECT_TRUE(g->get_output_pin("q").get_driver_pin() == x) << "shared_inner=" << shared_inner;
    if (shared_inner) {
      const auto r = g->get_output_pin("r").get_driver_pin();
      ASSERT_FALSE(r.is_invalid());
      EXPECT_EQ(gu::type_op_of(r.get_master_node()), Ntype_op::Not);
    }
  }
}

// P8: a compare whose operands are ONE driver pin. One driver is one value --
// a '?' literal included, since one literal has one value at all its uses
// (Q5) -- so EQ(x,x) is 1 and LT(x,x) / GT(x,x) are 0 for every x. EQ is
// "all operands equal", so EQ(x,x,y) stays. LT/GT fold only on the exact
// {x} vs {x} banks, and an LT whose two operands sit in ONE bank stays.
TEST(CpropCompare, SameDriverOperandsFold) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_same_operand_compare");
  enum Shape { EqXX, EqQQ, EqXXY, LtXX, GtXX, GtQQ, LtSameBank };
  for (const auto shape : {EqXX, EqQQ, EqXXY, LtXX, GtXX, GtQQ, LtSameBank}) {
    auto io = lib.create_io("same_compare_" + std::to_string(shape));
    io->add_input("x", 1);
    io->set_bits("x", 8);
    io->add_input("y", 2);
    io->set_bits("y", 8);
    io->add_output("q", 3);
    io->set_bits("q", 1);
    auto       g   = io->create_graph();
    const bool qq  = shape == EqQQ || shape == GtQQ;
    const auto x   = qq ? gu::create_const(*g, *Dlop::from_pyrope("0ub??")) : g->get_input_pin("x");
    const auto op  = shape == LtXX || shape == LtSameBank ? Ntype_op::LT
                     : shape == GtXX || shape == GtQQ     ? Ntype_op::GT
                                                          : Ntype_op::EQ;
    auto       cmp = gu::create_typed_node(*g, op, 1);
    if (op == Ntype_op::EQ) {
      gu::setup_sink_by_name(cmp, "as").connect_driver(x);
      gu::setup_sink_by_name(cmp, "as").connect_driver(x);
      if (shape == EqXXY) {
        gu::setup_sink_by_name(cmp, "as").connect_driver(g->get_input_pin("y"));
      }
    } else if (shape == LtSameBank) {
      gu::setup_sink_by_name(cmp, "as").connect_driver(x);
      gu::setup_sink_by_name(cmp, "as").connect_driver(x);
      gu::setup_sink_by_name(cmp, "bs").connect_driver(g->get_input_pin("y"));
    } else {
      gu::setup_sink_by_name(cmp, "as").connect_driver(x);
      gu::setup_sink_by_name(cmp, "bs").connect_driver(x);
    }
    cmp.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
    Cprop{}.do_trans(g);
    const auto out = g->get_output_pin("q").get_driver_pin();
    ASSERT_FALSE(out.is_invalid()) << "shape " << shape;
    if (shape == EqXXY || shape == LtSameBank) {
      EXPECT_EQ(gu::type_op_of(out.get_master_node()), op) << "shape " << shape;
      continue;
    }
    ASSERT_TRUE(out.is_const()) << "shape " << shape;
    const auto& v = gu::const_of(out);
    ASSERT_FALSE(v.has_unknowns()) << "shape " << shape;
    EXPECT_EQ(v.to_just_i64(), op == Ntype_op::EQ ? 1 : 0) << "shape " << shape;
  }
}

// P8: Sext(a, b) with constant inputs folds. The cell's `b` is the kept bit
// COUNT (sign at bit b-1, the cgen/sim/LEC reading). A result that keeps one
// of a's '?' bits does not fold (a new constant would split a's literal); a
// '?' above the kept window is dropped and the fold is exact. A count < 1 has
// no agreed reading and stays. (A window that keeps ALL of a '?' literal
// forwards that literal: see FoldsWithUnknownOperandNeverMintUnknownConstants.)
TEST(CpropSext, ConstantInputsFold) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_sext_fold");
  struct Case {
    const char*            a;
    int64_t                keep;
    std::optional<int64_t> expected;  // nullopt: the Sext must stay
  };
  const std::vector<Case> cases = {
      {      "11",  4,           -5},
      {      "11",  5,           11},
      {       "5",  3,           -3},
      {       "5",  2,            1},
      {      "-6",  3,            2},
      {      "-1",  1,           -1},
      {     "300", 64,          300},
      {"0ub?0101",  4,            5},
      { "0ub0?01",  3, std::nullopt},
      // A '?' sign bit at keep-1 with '?' bits above it: the result planes
      // spell exactly like a, but Sext(a,3) is only {1,-3} while a may be 5
      // or -7. Forwarding a would widen the value set, so the Sext stays.
      { "0sb??01",  3, std::nullopt},
      {      "11",  0, std::nullopt},
  };
  int index = 0;
  for (const auto& c : cases) {
    auto io = lib.create_io("sext_fold_" + std::to_string(index++));
    io->add_output("q", 1);
    io->set_bits("q", 16);
    auto g    = io->create_graph();
    auto sext = gu::create_typed_node(*g, Ntype_op::Sext, 16);
    gu::setup_sink_by_name(sext, "a").connect_driver(gu::create_const(*g, *Dlop::from_pyrope(c.a)));
    gu::setup_sink_by_name(sext, "b").connect_driver(gu::create_const(*g, *Dlop::create_integer(c.keep)));
    sext.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
    Cprop{}.do_trans(g);
    const auto out = g->get_output_pin("q").get_driver_pin();
    ASSERT_FALSE(out.is_invalid()) << c.a << " keep " << c.keep;
    if (!c.expected) {
      EXPECT_EQ(gu::type_op_of(out.get_master_node()), Ntype_op::Sext) << c.a << " keep " << c.keep;
      continue;
    }
    ASSERT_TRUE(out.is_const()) << c.a << " keep " << c.keep;
    const auto& v = gu::const_of(out);
    ASSERT_FALSE(v.has_unknowns()) << c.a << " keep " << c.keep;
    EXPECT_EQ(v.to_just_i64(), *c.expected) << c.a << " keep " << c.keep;
  }
}

// Every '?'-bearing constant pin that drives something in g (body sinks and
// the named outputs). P7: a fold never mints one, so it stays the source set.
std::vector<hhds::Pin_class> unknown_const_drivers(hhds::Graph& g, const std::vector<std::string>& outputs) {
  namespace gu = livehd::graph_util;
  std::vector<hhds::Pin_class> found;
  const auto                   note = [&](const hhds::Pin_class& d) {
    if (!d.is_invalid() && d.is_const() && gu::const_of(d).has_unknowns() && std::ranges::find(found, d) == found.end()) {
      found.push_back(d);
    }
  };
  for (auto n : g.body().nodes()) {
    for (auto sink : n.inp_sorted_pins()) {
      note(sink.get_driver_pin());
    }
  }
  for (const auto& name : outputs) {
    note(g.get_output_pin(name).get_driver_pin());
  }
  return found;
}

// P7: one '?' literal `c` read whole by one output and through Get_mask[0,2)
// by another. Folding the slice minted `0ub??`, a second literal that sim
// draws independently, so `lo` no longer equaled `all`'s low bits. The
// literal must stay the ONLY '?' constant and the slice must read it.
TEST(CpropLiteral, LowSliceOfUnknownLiteralReadsTheSameLiteral) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_literal_slice");
  auto  io     = lib.create_io("literal_slice");
  io->add_output("all", 1);
  io->set_bits("all", 4);
  io->add_output("lo", 2);
  io->set_bits("lo", 2);
  auto       g = io->create_graph();
  const auto c = gu::create_const(*g, *Dlop::from_pyrope("0ub????"));
  c.connect_sink(g->get_output_pin("all"));
  gu::create_get_mask(*g, c, 0, 2).create_driver_pin(0).connect_sink(g->get_output_pin("lo"));
  Cprop{}.do_trans(g);
  Bitwidth{10}.do_trans(g);
  EXPECT_TRUE(g->get_output_pin("all").get_driver_pin() == c);
  const auto lo = g->get_output_pin("lo").get_driver_pin();
  ASSERT_FALSE(lo.is_invalid());
  ASSERT_FALSE(lo.is_const()) << "the slice was minted as " << gu::const_of(lo).to_pyrope();
  ASSERT_EQ(gu::type_op_of(lo.get_master_node()), Ntype_op::Get_mask);
  EXPECT_TRUE(gu::get_driver_of_sink_name(lo.get_master_node(), "a") == c);
  const auto unknowns = unknown_const_drivers(*g, {"all", "lo"});
  ASSERT_EQ(unknowns.size(), 1u);
  EXPECT_TRUE(unknowns.front() == c);
}

// P7 over every all-constant fold: with one '?' operand literal c = 0ub????,
// a fold result that is FULLY KNOWN still folds (And(c,0), Or(c,-1), ...), a
// result that is exactly c forwards c's own pin (identity operands, a covering
// Get_mask, a constant Mux selection), and any other '?' result keeps its op.
// No case may leave a '?' constant other than c.
//
// hhds interns constants by same_repr ('?' included), so for the Forward rows
// a re-mint of c's value would also return c's pin: those rows pin the RESULT
// (out is c), not the forward_to_const_pin mechanism. They still fail when an
// identity fold is refused and the op is kept (SHL/SRA by 0, a covering Sext,
// a one-lane Concat). The single-'?' check guards the Keep and partial rows.
TEST(CpropLiteral, FoldsWithUnknownOperandNeverMintUnknownConstants) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_literal_folds");
  enum class Want { Keep, Known, Forward };
  struct Case {
    const char*                                  name;
    Ntype_op                                     op;
    std::vector<std::pair<const char*, int64_t>> operands;  // sink name, constant (INT64_MIN = c)
    Want                                         want;
    int64_t                                      value = 0;         // Known
    int                                          lo = -1, hi = -1;  // Get_mask / Set_mask window
  };
  constexpr int64_t       C     = std::numeric_limits<int64_t>::min();
  const std::vector<Case> cases = {
      {"shl", Ntype_op::SHL, {{"a", C}, {"b", 1}}, Want::Keep},
      {"shl_0", Ntype_op::SHL, {{"a", C}, {"b", 0}}, Want::Forward},
      {"sra", Ntype_op::SRA, {{"a", C}, {"b", 1}}, Want::Keep},
      {"sra_0", Ntype_op::SRA, {{"a", C}, {"b", 0}}, Want::Forward},
      {"sext_8", Ntype_op::Sext, {{"a", C}, {"b", 8}}, Want::Forward},
      {"sext_4", Ntype_op::Sext, {{"a", C}, {"b", 4}}, Want::Keep}, // '?' sign bit: a new spelling
      {"concat_1", Ntype_op::Concat, {{"p0", C}, {"p1", 4}}, Want::Forward},
      {"concat_1_wide", Ntype_op::Concat, {{"p0", C}, {"p1", 6}}, Want::Forward},
      {"concat_2", Ntype_op::Concat, {{"p0", 1}, {"p1", 4}, {"p2", C}, {"p3", 4}}, Want::Keep},
      {"ror", Ntype_op::Ror, {{"as", C}}, Want::Keep},
      {"not", Ntype_op::Not, {{"a", C}}, Want::Keep},
      {"sum_k", Ntype_op::Sum, {{"as", C}, {"as", 1}}, Want::Keep},
      {"sum_kk", Ntype_op::Sum, {{"as", C}, {"as", 1}, {"as", 2}}, Want::Keep},
      {"sum_0", Ntype_op::Sum, {{"as", C}, {"as", 0}}, Want::Forward},
      {"or_k", Ntype_op::Or, {{"as", C}, {"as", 1}}, Want::Keep},
      {"or_0", Ntype_op::Or, {{"as", C}, {"as", 0}}, Want::Forward},
      {"or_m1", Ntype_op::Or, {{"as", C}, {"as", -1}}, Want::Known, -1},
      {"and_k", Ntype_op::And, {{"as", C}, {"as", 3}}, Want::Keep},
      {"and_0", Ntype_op::And, {{"as", C}, {"as", 0}}, Want::Known, 0},
      {"and_high", Ntype_op::And, {{"as", C}, {"as", 48}}, Want::Known, 0},
      {"and_m1", Ntype_op::And, {{"as", C}, {"as", -1}}, Want::Forward},
      {"xor_k", Ntype_op::Xor, {{"as", C}, {"as", 1}}, Want::Keep},
      {"xor_0", Ntype_op::Xor, {{"as", C}, {"as", 0}}, Want::Forward},
      {"mult_k", Ntype_op::Mult, {{"as", C}, {"as", 3}}, Want::Keep},
      {"mult_0", Ntype_op::Mult, {{"as", C}, {"as", 0}}, Want::Known, 0},
      {"mult_1", Ntype_op::Mult, {{"as", C}, {"as", 1}}, Want::Forward},
      {"get_low2", Ntype_op::Get_mask, {{"a", C}}, Want::Keep, 0, 0, 2},
      {"get_all4", Ntype_op::Get_mask, {{"a", C}}, Want::Forward, 0, 0, 4},
      {"get_high", Ntype_op::Get_mask, {{"a", C}}, Want::Known, 0, 4, 8},
      {"set_low1", Ntype_op::Set_mask, {{"a", C}, {"value", 1}}, Want::Keep, 0, 0, 1},
  };
  int index = 0;
  for (const auto& tc : cases) {
    auto io = lib.create_io(std::string("literal_fold_") + tc.name + std::to_string(index++));
    io->add_output("q", 1);
    io->set_bits("q", 8);
    io->add_output("all", 2);
    io->set_bits("all", 4);
    auto       g = io->create_graph();
    const auto c = gu::create_const(*g, *Dlop::from_pyrope("0ub????"));
    c.connect_sink(g->get_output_pin("all"));
    auto node = gu::create_typed_node(*g, tc.op, 8);
    for (const auto& [sink, v] : tc.operands) {
      gu::setup_sink_by_name(node, sink).connect_driver(v == C ? c : gu::create_const(*g, *Dlop::create_integer(v)));
    }
    if (tc.lo >= 0) {
      gu::connect_bit_range(node, tc.lo, tc.hi);
    }
    node.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
    Cprop{}.do_trans(g);
    const auto out = g->get_output_pin("q").get_driver_pin();
    ASSERT_FALSE(out.is_invalid()) << tc.name;
    const auto unknowns = unknown_const_drivers(*g, {"q", "all"});
    ASSERT_EQ(unknowns.size(), 1u) << tc.name << ": a '?' constant was minted";
    EXPECT_TRUE(unknowns.front() == c) << tc.name;
    switch (tc.want) {
      case Want::Keep:
        ASSERT_FALSE(out.is_const()) << tc.name << " folded to " << gu::const_of(out).to_pyrope();
        EXPECT_EQ(gu::type_op_of(out.get_master_node()), tc.op) << tc.name;
        break;
      case Want::Known:
        ASSERT_TRUE(out.is_const()) << tc.name;
        ASSERT_FALSE(gu::const_of(out).has_unknowns()) << tc.name;
        EXPECT_EQ(gu::const_of(out).to_just_i64(), tc.value) << tc.name;
        break;
      case Want::Forward: EXPECT_TRUE(out == c) << tc.name; break;
    }
  }
}

// P7: a constant selector resolves to the selected arm's literal. hhds interns
// constants by same_repr ('?' included), so forwarding the arm pin and
// re-minting its value give the same pin: this pins the RESULT (q is c), not
// the forward_to_const_pin mechanism. It still fails if the fold is refused
// (replace_node keeps every other '?' result) and the Mux stays.
TEST(CpropLiteral, ConstantSelectorForwardsUnknownArmPin) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_literal_mux");
  for (const int sel : {0, 1}) {
    auto io = lib.create_io("literal_mux_" + std::to_string(sel));
    io->add_output("q", 1);
    io->set_bits("q", 4);
    io->add_output("all", 2);
    io->set_bits("all", 4);
    auto       g = io->create_graph();
    const auto c = gu::create_const(*g, *Dlop::from_pyrope("0ub????"));
    const auto k = gu::create_const(*g, *Dlop::create_integer(5));
    c.connect_sink(g->get_output_pin("all"));
    auto mux = gu::create_typed_node(*g, Ntype_op::Mux, 4);
    gu::setup_sink_pid(mux, 0).connect_driver(gu::create_const(*g, *Dlop::create_integer(sel)));
    gu::setup_sink_pid(mux, 1).connect_driver(sel == 0 ? c : k);
    gu::setup_sink_pid(mux, 2).connect_driver(sel == 0 ? k : c);
    mux.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
    Cprop{}.do_trans(g);
    EXPECT_TRUE(g->get_output_pin("q").get_driver_pin() == c) << "sel=" << sel;
  }
}

// P7 follow-up (lhd_constant_table_test): an array built lane by lane from a
// '?' initial value. Each intermediate word still holds '?' lanes, so it may
// not be minted as a constant and stays a node -- but a read of a WRITTEN lane
// is fully known under every resolution of the literal and must still fold.
// No '?' constant other than the source literal may appear.
TEST(CpropLiteral, KnownLaneOfUnknownConeFolds) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_literal_cone");
  auto  io     = lib.create_io("literal_cone");
  io->add_output("all", 1);
  io->set_bits("all", 8);
  io->add_output("lo", 2);
  io->set_bits("lo", 4);
  io->add_output("hi", 3);
  io->set_bits("hi", 4);
  io->add_output("q", 4);
  io->set_bits("q", 8);
  io->add_output("cat_hi", 5);
  io->set_bits("cat_hi", 4);
  auto       g = io->create_graph();
  const auto c = gu::create_const(*g, *Dlop::from_pyrope("0ub????????"));
  c.connect_sink(g->get_output_pin("all"));
  const auto k   = [&](int64_t v) { return gu::create_const(*g, *Dlop::create_integer(v)); };
  const auto bin = [&](Ntype_op op, hhds::Pin_class a, hhds::Pin_class b) {
    auto n = gu::create_typed_node(*g, op, 8);
    gu::setup_sink_by_name(n, "as").connect_driver(a);
    gu::setup_sink_pid(n, 1).connect_driver(b);
    return n.create_driver_pin(0);
  };
  // step1 = {c[7:4], 4'd5}; q = {4'hA, step1[3:0]} = 0xA5
  const auto step1 = bin(Ntype_op::Or, bin(Ntype_op::And, c, k(0xF0)), k(5));
  const auto q     = bin(Ntype_op::Or, bin(Ntype_op::And, step1, k(0x0F)), k(0xA0));
  q.connect_sink(g->get_output_pin("q"));
  gu::create_get_mask(*g, step1, 0, 4).create_driver_pin(0).connect_sink(g->get_output_pin("lo"));
  gu::create_get_mask(*g, step1, 4, 8).create_driver_pin(0).connect_sink(g->get_output_pin("hi"));
  // cat = {4'd3, c[3:0]}; cat[7:4] = 3
  auto cat = gu::create_typed_node(*g, Ntype_op::Concat);
  gu::setup_sink_pid(cat, 0).connect_driver(k(3));
  gu::setup_sink_pid(cat, 1).connect_driver(k(4));
  gu::setup_sink_pid(cat, 2).connect_driver(gu::create_get_mask(*g, c, 0, 4).create_driver_pin(0));
  gu::setup_sink_pid(cat, 3).connect_driver(k(4));
  gu::create_get_mask(*g, cat.create_driver_pin(0), 4, 8).create_driver_pin(0).connect_sink(g->get_output_pin("cat_hi"));

  Cprop{}.do_trans(g);

  const auto known = [&](const char* name, int64_t v) {
    const auto d = g->get_output_pin(name).get_driver_pin();
    ASSERT_FALSE(d.is_invalid()) << name;
    ASSERT_TRUE(d.is_const()) << name << " was not folded";
    ASSERT_FALSE(gu::const_of(d).has_unknowns()) << name;
    EXPECT_EQ(gu::const_of(d).to_just_i64(), v) << name;
  };
  known("lo", 5);
  known("q", 0xA5);
  known("cat_hi", 3);
  const auto hi = g->get_output_pin("hi").get_driver_pin();
  ASSERT_FALSE(hi.is_invalid());
  EXPECT_FALSE(hi.is_const()) << "hi holds c's '?' lane and was minted as " << gu::const_of(hi).to_pyrope();
  EXPECT_TRUE(g->get_output_pin("all").get_driver_pin() == c);
  const auto unknowns = unknown_const_drivers(*g, {"all", "lo", "hi", "q", "cat_hi"});
  ASSERT_EQ(unknowns.size(), 1u) << "a '?' constant was minted";
  EXPECT_TRUE(unknowns.front() == c);
}

// P7 follow-up: before P7 every all-constant fold minted a constant ('?' ones
// included), so a later op that DROPS the '?' bits still folded. Now those
// '?' intermediates stay nodes; the known-cone fold must see through the
// lane ops that move or overwrite bits (Set_mask, SHL/SRA by a constant,
// Sext, a constant-selector Mux) and fold the roots that discard them
// (Get_mask, SRA, Sext, Ror, Rxor, Popcount, Mux) when the result is fully
// known. A result that still holds a '?' bit keeps its node.
TEST(CpropLiteral, KnownResultThroughLaneOpsFolds) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_literal_lane_cone");
  struct Case {
    const char*            name;
    std::optional<int64_t> expected;  // nullopt: q keeps a node
  };
  const std::vector<Case> cases = {
      {     "set_get",            5},
      {"set_get_keep", std::nullopt},
      {     "sra_cat",            3},
      {    "sext_cat",            5},
      {     "shl_get",            0},
      {      "ror_or",            1},
      {    "popcount",            2},
      {        "rxor",            0},
      {     "mux_get",            5},
  };
  int index = 0;
  for (const auto& tc : cases) {
    auto io = lib.create_io(std::string("lane_cone_") + tc.name + std::to_string(index++));
    io->add_output("q", 1);
    io->set_bits("q", 8);
    io->add_output("all", 2);
    io->set_bits("all", 8);
    auto       g = io->create_graph();
    const auto c = gu::create_const(*g, *Dlop::from_pyrope("0ub????????"));
    c.connect_sink(g->get_output_pin("all"));
    const auto k   = [&](int64_t v) { return gu::create_const(*g, *Dlop::create_integer(v)); };
    // {hi:hw, lo:lw}, MSB lane first
    const auto cat = [&](hhds::Pin_class hi, int hw, hhds::Pin_class lo, int lw) {
      auto n = gu::create_typed_node(*g, Ntype_op::Concat);
      gu::setup_sink_pid(n, 0).connect_driver(hi);
      gu::setup_sink_pid(n, 1).connect_driver(k(hw));
      gu::setup_sink_pid(n, 2).connect_driver(lo);
      gu::setup_sink_pid(n, 3).connect_driver(k(lw));
      return n.create_driver_pin(0);
    };
    const auto ab = [&](Ntype_op op, hhds::Pin_class a, hhds::Pin_class b) {
      auto n = gu::create_typed_node(*g, op, 16);
      gu::setup_sink_by_name(n, "a").connect_driver(a);
      gu::setup_sink_by_name(n, "b").connect_driver(b);
      return n.create_driver_pin(0);
    };
    const auto set_mask = [&](hhds::Pin_class a, hhds::Pin_class v, int lo, int hi) {
      auto n = gu::create_typed_node(*g, Ntype_op::Set_mask, 16);
      gu::setup_sink_by_name(n, "a").connect_driver(a);
      gu::setup_sink_by_name(n, "value").connect_driver(v);
      gu::connect_bit_range(n, lo, hi);
      return n.create_driver_pin(0);
    };
    const auto get_mask
        = [&](hhds::Pin_class a, int lo, int hi) { return gu::create_get_mask(*g, a, lo, hi).create_driver_pin(0); };
    hhds::Pin_class   q;
    const std::string name = tc.name;
    if (name == "set_get") {
      q = get_mask(set_mask(c, k(5), 0, 4), 0, 4);
    } else if (name == "set_get_keep") {
      q = get_mask(set_mask(c, k(5), 0, 4), 2, 6);  // bits 4,5 are c's '?'
    } else if (name == "sra_cat") {
      q = ab(Ntype_op::SRA, cat(k(3), 4, c, 8), k(8));
    } else if (name == "sext_cat") {
      q = ab(Ntype_op::Sext, cat(c, 8, k(5), 4), k(4));
    } else if (name == "shl_get") {
      q = get_mask(ab(Ntype_op::SHL, c, k(4)), 0, 4);
    } else if (name == "ror_or") {
      auto orn = gu::create_typed_node(*g, Ntype_op::Or, 8);
      gu::setup_sink_by_name(orn, "as").connect_driver(c);
      gu::setup_sink_pid(orn, 1).connect_driver(k(1));
      auto ror = gu::create_typed_node(*g, Ntype_op::Ror, 1);
      gu::setup_sink_by_name(ror, "as").connect_driver(orn.create_driver_pin(0));
      q = ror.create_driver_pin(0);
    } else if (name == "popcount") {
      q = ab(Ntype_op::Popcount, cat(c, 8, k(5), 4), k(4));
    } else if (name == "rxor") {
      q = ab(Ntype_op::Rxor, cat(c, 8, k(5), 4), k(4));
    } else if (name == "mux_get") {
      auto orn = gu::create_typed_node(*g, Ntype_op::Or, 8);
      gu::setup_sink_by_name(orn, "as").connect_driver(c);
      gu::setup_sink_pid(orn, 1).connect_driver(k(0xF0));
      auto mux = gu::create_typed_node(*g, Ntype_op::Mux, 16);
      gu::setup_sink_pid(mux, 0).connect_driver(k(1));
      gu::setup_sink_pid(mux, 1).connect_driver(orn.create_driver_pin(0));
      gu::setup_sink_pid(mux, 2).connect_driver(cat(k(5), 4, c, 8));
      q = get_mask(mux.create_driver_pin(0), 8, 12);
    }
    ASSERT_FALSE(q.is_invalid()) << name;
    q.connect_sink(g->get_output_pin("q"));

    Cprop{}.do_trans(g);

    const auto out = g->get_output_pin("q").get_driver_pin();
    ASSERT_FALSE(out.is_invalid()) << name;
    if (tc.expected) {
      if (!out.is_const()) {
        ADD_FAILURE() << name << " was not folded (q is a " << Ntype::get_name(gu::type_op_of(out.get_master_node())) << ")";
        continue;
      }
      ASSERT_FALSE(gu::const_of(out).has_unknowns()) << name;
      EXPECT_EQ(gu::const_of(out).to_just_i64(), *tc.expected) << name;
    } else {
      EXPECT_FALSE(out.is_const()) << name << " minted " << gu::const_of(out).to_pyrope();
    }
    EXPECT_TRUE(g->get_output_pin("all").get_driver_pin() == c) << name;
    const auto unknowns = unknown_const_drivers(*g, {"q", "all"});
    ASSERT_EQ(unknowns.size(), 1u) << name << ": a '?' constant was minted";
    EXPECT_TRUE(unknowns.front() == c) << name;
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

// Fuzz: a write guard that folds to 0 (`if (x#[8] == 1)` on a U8) leaves a
// constant-false enable. The emitters read a constant enable as "no enable",
// so cprop must turn it into the hold it means (din = q) and drop a
// constant-true one (the unconditional write).
TEST(CpropCleanup, ConstantFlopEnableBecomesHoldOrUnconditionalWrite) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_const_enable_test");
  for (int en : {0, 1}) {
    auto io = lib.create_io(en ? "const_enable_true" : "const_enable_false");
    io->add_input("d", 1);
    io->set_bits("d", 8);
    io->add_input("clock", 2);
    io->set_bits("clock", 1);
    io->add_output("q", 3);
    io->set_bits("q", 8);
    auto g    = io->create_graph();
    auto flop = gu::create_typed_node(*g, Ntype_op::Flop, 8);
    auto q    = flop.create_driver_pin(0);
    gu::set_ubits(q, 8);
    q.connect_sink(g->get_output_pin("q"));
    g->get_input_pin("clock").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
    g->get_input_pin("d").connect_sink(gu::setup_sink_by_name(flop, "din"));
    gu::create_const(*g, *Dlop::create_integer(en)).connect_sink(gu::setup_sink_by_name(flop, "enable"));
    Cprop cp;
    cp.do_trans(g);
    EXPECT_TRUE(gu::get_driver_of_sink_name(flop, "enable").is_invalid());
    EXPECT_EQ(gu::get_driver_of_sink_name(flop, "din"), en ? g->get_input_pin("d") : q);
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

// Equal values can collapse only after exclusivity is established. An
// implicit zero default must also agree with the common value.
TEST(CpropHotmux, IdenticalArmsRequireExclusivity) {
  namespace gu = livehd::graph_util;
  auto& lib    = livehd::Hhds_graph_library::instance("lgdb_cprop_hotmux_identical");
  // shared: 0 = the arms share a runtime value, 1 = they share the constant 0.
  for (bool proven : {false, true}) {
    for (int shared : {0, 1}) {
      for (bool fallback : {false, true}) {
        auto io = lib.create_io(std::string{"identical_"} + (shared ? "zero" : "value") + (fallback ? "_default" : "_nodefault")
                                + (proven ? "_proven" : "_unproven"));
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

        if (proven) {
          gu::set_proven(hot, gu::kFormalOnehot);
        }
        Cprop cp;
        cp.do_trans(g);

        // An unproven overlap remains observable even with equal values.
        // No default port and a non-zero shared value: the zero-control case reads
        // 0, which the arms do not, so the cell must SURVIVE.
        const bool collapses = proven && (fallback || shared);
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
    const auto count  = node.inp_pins_snapshot().size() - 1;
    const auto select = at(0);
    const auto arm    = count == 2                                             ? size_t(select != 0)
                        : select < 0 || static_cast<uint64_t>(select) >= count ? count - 1
                                                                               : size_t(select);
    result            = at(arm + 1);
  } else if (op == Ntype_op::Not) {
    result = ~at(0);
  } else if (op == Ntype_op::Sum || op == Ntype_op::Mult || op == Ntype_op::LT || op == Ntype_op::GT) {
    int64_t a = op == Ntype_op::Mult ? 1 : 0, b = 0;
    for (auto sink : node.inp_sorted_pins()) {
      const auto value = mux_eval(sink.get_driver_pin(), values);
      if (Ntype::sink_bank(op, sink.get_port_id()) == 1) {
        b += value;
      } else if (op == Ntype_op::Mult) {
        a *= value;
      } else {
        a += value;
      }
    }
    result = op == Ntype_op::LT ? a < b : op == Ntype_op::GT ? a > b : a - b;
  } else if (op == Ntype_op::Div || op == Ntype_op::Rem) {
    const auto a = at(0), b = at(1);
    EXPECT_NE(b, 0);
    result = b == 0 ? 0 : op == Ntype_op::Div ? a / b : a % b;
  } else if (op == Ntype_op::Sext) {
    const auto bit   = at(1) - 1;
    const auto mask  = (int64_t{1} << bit) - 1;
    const auto value = at(0);
    result           = (value & (int64_t{1} << bit)) ? (value | ~mask) : (value & mask);
  } else if (op == Ntype_op::Rxor || op == Ntype_op::Popcount) {
    const auto value = static_cast<uint64_t>(at(0)) & ((uint64_t{1} << at(1)) - 1);
    const auto count = std::popcount(value);
    result           = op == Ntype_op::Rxor ? count % 2 : count;
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
    const auto [lo, hi] = *gu::bit_range(node);
    const auto mask     = ((uint64_t{1} << (hi - lo)) - 1) << lo;
    const auto base     = static_cast<uint64_t>(mux_eval(gu::get_driver_of_sink_name(node, "a"), values));
    const auto value    = static_cast<uint64_t>(mux_eval(gu::get_driver_of_sink_name(node, "value"), values));
    result              = static_cast<int64_t>((base & ~mask) | ((value << lo) & mask));
  } else if (op == Ntype_op::Get_mask) {
    // Constant contiguous window only: bits [lo,hi) of `a`, LSB-aligned.
    const auto [lo, hi] = *gu::bit_range(node);
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
    // The innermost identical-arm mux folds. Rebuilding the remaining chain
    // as a select tree is equal-cost, so A3 leaves it alone.
    EXPECT_EQ(f.count(Ntype_op::Mux), 5);
    EXPECT_EQ(f.count(Ntype_op::Hotmux), 0);
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
    EXPECT_EQ(f.count(Ntype_op::Hotmux), 0);
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
  EXPECT_EQ(f.count(Ntype_op::Mux), depth - 1);
  EXPECT_EQ(f.count(Ntype_op::Hotmux), 0);
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
  gu::connect_mask_operands(lane_read,
                            q,
                            livehd::graph_util::mask_window(livehd::graph_util::const_of(constant(2))).first,
                            livehd::graph_util::mask_window(livehd::graph_util::const_of(constant(2))).second);
  gu::set_ubits(lane_read.create_driver_pin(0), 1);
  auto lane = mux(rst, mux(upd, lane_read.create_driver_pin(0), next), constant(1));
  auto din  = gu::create_set_mask(*g,
                                  q,
                                  lane,
                                  livehd::graph_util::mask_window(livehd::graph_util::const_of(constant(2))).first,
                                  livehd::graph_util::mask_window(livehd::graph_util::const_of(constant(2))).second);
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

// Width-free Sext/Get_mask folds on the shape a Verilog reader emits for a
// signed 1-bit write (`sext(x, 12)[0,1)` re-signed by `sext(.., 1)`): a Sext
// keeping b bits reads no bit of a low [0,w) window above b <= w, and a [lo,hi)
// window with hi <= b reads no bit of the Sext above its kept bits.
TEST(CpropMasks, SextAndLowWindowChainsCollapse) {
  Mux_graph f("sext_window_chain", 1, 12, true);
  auto      s1 = f.node(Ntype_op::Sext);
  gu::setup_sink_by_name(s1, "a").connect_driver(f.a);
  gu::setup_sink_by_name(s1, "b").connect_driver(f.constant(12));
  auto m1 = gu::create_get_mask(*f.graph, s1.create_driver_pin(0), 0, 1);
  gu::set_ubits(m1.create_driver_pin(0), 1);
  auto s2 = f.node(Ntype_op::Sext, 1);
  gu::set_sbits(s2.create_driver_pin(0), 1);
  gu::setup_sink_by_name(s2, "a").connect_driver(m1.create_driver_pin(0));
  gu::setup_sink_by_name(s2, "b").connect_driver(f.constant(1));
  auto wide = gu::create_get_mask(*f.graph, s2.create_driver_pin(0), 0, 8);
  gu::set_ubits(wide.create_driver_pin(0), 8);
  wide.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
  auto bit = gu::create_get_mask(*f.graph, s2.create_driver_pin(0), 0, 1);
  gu::set_ubits(bit.create_driver_pin(0), 1);
  bit.create_driver_pin(0).connect_sink(f.graph->get_output_pin("observe"));
  Cprop{}.do_trans(f.graph);
  int sexts = 0;
  for (auto n : f.graph->body().nodes()) {
    if (gu::type_op_of(n) == Ntype_op::Sext) {
      ++sexts;
      EXPECT_TRUE(gu::is_graph_input_pin(gu::get_driver_of_sink_name(n, "a"))) << "the Sext reads the input directly";
    }
  }
  EXPECT_EQ(sexts, 1);
  EXPECT_TRUE(gu::is_graph_input_pin(gu::get_driver_of_sink_name(f.observed().get_master_node(), "a")))
      << "bit 0 of a 1-bit Sext is bit 0 of its input";
  for (int64_t av : {0, 1, 2, 3, -1, -2, 0x7FF, -0x800}) {
    auto values = f.inputs(0, av, 0);
    EXPECT_EQ(mux_eval(f.output(), values), (av & 1) ? 0xFF : 0) << av;
    EXPECT_EQ(mux_eval(f.observed(), values), av & 1) << av;
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
    auto      left  = f.mux(f.controls[1], f.a, f.b);
    auto      right = f.mux(f.controls[2], f.a, f.b);
    auto      root  = f.mux(f.controls[0], left, right);
    if (colored) {
      gu::set_color(root.get_master_node(), 7);
    }
    root.connect_sink(f.graph->get_output_pin("out"));
    Cprop{}.do_trans(f.graph);
    EXPECT_EQ(f.count(Ntype_op::Mux), colored ? 3 : 2);
    EXPECT_EQ(f.count(Ntype_op::Hotmux), 0);
  }
}

TEST(CpropMuxSharing, TwoGroupSelectTreePreservesWideSignedControlsAndBooleanData) {
  for (bool boolean_data : {false, true}) {
    Mux_graph f("two_group_" + std::to_string(boolean_data), 3, 8, true, true);
    auto      a = f.a, b = f.b;
    if (boolean_data) {
      a = gu::create_get_mask(*f.graph, a, 0, 1).create_driver_pin(0);
      b = gu::create_get_mask(*f.graph, b, 0, 1).create_driver_pin(0);
    }
    f.mux(f.controls[0], f.mux(f.controls[1], a, b), f.mux(f.controls[2], a, b)).connect_sink(f.graph->get_output_pin("out"));
    Cprop{}.do_trans(f.graph);
    EXPECT_EQ(f.count(Ntype_op::Mux), 2);
    EXPECT_EQ(f.count(Ntype_op::Hotmux), 0);
    EXPECT_EQ(f.count(Ntype_op::EQ), 0);
    for (int s : {-2, 0, 1, 4}) {
      for (int c : {-2, 0, 1, 4}) {
        for (int d : {-2, 0, 1, 4}) {
          auto values                                   = f.inputs(0, -7, 26);
          values[f.controls[0].get_class_index().value] = s;
          values[f.controls[1].get_class_index().value] = c;
          values[f.controls[2].get_class_index().value] = d;
          const auto expected                           = (s ? d : c) ? 26 : -7;
          EXPECT_EQ(mux_eval(f.output(), values), boolean_data ? expected & 1 : expected);
        }
      }
    }
  }
}

TEST(CpropMuxSharing, TwoGroupEqualCostRejectionLeavesNoResidue) {
  Mux_graph f("two_group_no_gain", 3);
  auto      left  = f.mux(f.controls[1], f.a, f.b);
  auto      right = f.mux(f.controls[2], f.b, f.a);
  auto      root  = f.mux(f.controls[0], left, right);
  root.connect_sink(f.graph->get_output_pin("out"));
  // Opposite leaf order costs an inversion, cancelling the saved mux.
  livehd::share_mux_regions(*f.graph, false);
  EXPECT_EQ(f.count(Ntype_op::Mux), 3);
  EXPECT_EQ(f.count(Ntype_op::EQ), 0);
  EXPECT_EQ(f.count(Ntype_op::Hotmux), 0);
  EXPECT_EQ(root.get_master_node().get_sink_pin(1).get_driver_pin(), left);
  EXPECT_EQ(root.get_master_node().get_sink_pin(2).get_driver_pin(), right);
}

TEST(CpropMuxSharing, NamedInteriorRemainsAddressable) {
  Mux_graph f("two_group_named", 3);
  auto      left  = f.mux(f.controls[1], f.a, f.b);
  auto      right = f.mux(f.controls[2], f.a, f.b);
  gu::set_pin_name(left, "visible_selection");
  f.mux(f.controls[0], left, right).connect_sink(f.graph->get_output_pin("out"));
  livehd::share_mux_regions(*f.graph, false);
  EXPECT_EQ(f.count(Ntype_op::Mux), 3);
  EXPECT_EQ(gu::pin_name_of(left), "visible_selection");
  EXPECT_EQ(gu::type_op_of(left.get_master_node()), Ntype_op::Mux);
}

TEST(CpropMuxSharing, TwoGroupHotmuxPreservesDefaultAndUnprovenObligation) {
  for (bool exclusive : {false, true}) {
    for (bool default_is_b : {false, true}) {
      Mux_graph f("two_group_hot_" + std::to_string(exclusive) + std::to_string(default_is_b), 3);
      auto      root = f.node(Ntype_op::Hotmux);
      root.create_sink_pin(0).connect_driver(exclusive ? f.eq(f.controls[0], 0) : f.controls[0]);
      root.create_sink_pin(1).connect_driver(f.mux(f.controls[1], f.a, f.b));
      root.create_sink_pin(2).connect_driver(exclusive ? f.eq(f.controls[0], 1) : f.controls[1]);
      root.create_sink_pin(3).connect_driver(f.mux(f.controls[2], f.a, f.b));
      root.create_sink_pin(4).connect_driver(default_is_b ? f.b : f.a);
      root.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
      livehd::share_mux_regions(*f.graph, false);
      EXPECT_EQ(f.count(Ntype_op::Mux), exclusive ? 1 : 2);
      EXPECT_EQ(f.count(Ntype_op::Hotmux), 1);
      if (!exclusive) {
        EXPECT_EQ(gu::type_op_of(root), Ntype_op::Hotmux);
        EXPECT_FALSE(gu::has_proven(root));
        continue;
      }
      for (unsigned mask = 0; mask < 8; ++mask) {
        for (int select : {0, 1, 2}) {
          auto values                                   = f.inputs(mask, 37, 91);
          values[f.controls[0].get_class_index().value] = select;
          const bool choose_b = select == 0 ? bool(mask & 2) : select == 1 ? bool(mask & 4) : default_is_b;
          EXPECT_EQ(mux_eval(f.output(), values), choose_b ? 91 : 37);
        }
      }
    }
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
    livehd::graph_util::connect_bit_range(node, 1, 2);
    gu::setup_sink_by_name(node, "value").connect_driver(gu::create_const(*graph, *Dlop::create_integer(1)));
    writers.push_back(node);
  }
  for (size_t i = 0; i < writers.size(); ++i) {
    gu::setup_sink_by_name(writers[i], "a").connect_driver(writers[(i + 1) % writers.size()].get_driver_pin(0));
  }
  auto       read   = gu::create_typed_node(*graph, Ntype_op::Get_mask, 1);
  const auto source = writers.front().get_driver_pin(0);
  gu::setup_sink_by_name(read, "a").connect_driver(source);
  livehd::graph_util::connect_bit_range(read, 0, 1);
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
    livehd::graph_util::connect_bit_range(write, i, i + 1);
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
  // The four lane muxes vectorize, then operator sharing factors their equal
  // masks: Get_mask(Mux(s,a,b), [0,4)). Only one word mux and one read remain.
  ASSERT_EQ(gu::type_op_of(head), Ntype_op::Get_mask);
  EXPECT_EQ(*gu::bit_range(head), std::make_pair(0, 4));
  EXPECT_EQ(gu::type_op_of(gu::get_driver_of_sink_name(head, "a").get_master_node()), Ntype_op::Mux);
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
    livehd::graph_util::connect_bit_range(write, i, i + 1);
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
      word       = gu::create_set_mask(*graph,
                                       word,
                                       value,
                                       livehd::graph_util::mask_window(livehd::graph_util::const_of(mask)).first,
                                       livehd::graph_util::mask_window(livehd::graph_util::const_of(mask)).second)
                       .create_driver_pin(0);
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
      EXPECT_EQ(gu::bit_range(read.get_master_node()),
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
    return gu::create_set_mask(*f.graph,
                               base,
                               value,
                               livehd::graph_util::mask_window(livehd::graph_util::const_of(f.constant(6))).first,
                               livehd::graph_util::mask_window(livehd::graph_util::const_of(f.constant(6))).second)
        .create_driver_pin(0);
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

TEST(CpropBool, XorInversionRequiresStructuralBooleanValue) {
  for (bool boolean : {false, true}) {
    for (bool bitwise_not : {false, true}) {
      Mux_graph f("inversion_domain_" + std::to_string(boolean) + std::to_string(bitwise_not), 1, 8, true, true);
      auto      source = f.controls[0];
      if (boolean) {
        source = gu::create_get_mask(*f.graph, source, 0, 1).create_driver_pin(0);
      } else {
        gu::set_ubits(source, 1);  // A hint is not a bool01 proof.
      }
      auto inversion = f.node(bitwise_not ? Ntype_op::Not : Ntype_op::Xor);
      gu::setup_sink_pid(inversion, 0).connect_driver(source);
      if (!bitwise_not) {
        gu::setup_sink_pid(inversion, 0).connect_driver(f.constant(1));
      }
      f.mux(inversion.create_driver_pin(0), f.a, f.b).connect_sink(f.graph->get_output_pin("out"));
      Cprop{}.do_trans(f.graph);
      if (boolean && !bitwise_not) {
        EXPECT_EQ(f.count(Ntype_op::Xor), 0);
      } else {
        EXPECT_EQ(f.count(bitwise_not ? Ntype_op::Not : Ntype_op::Xor), 1);
      }
      for (int64_t input : {-2, -1, 0, 1, 2, 127, 128}) {
        auto values                                   = f.inputs(0, -7, 26);
        values[f.controls[0].get_class_index().value] = input;
        const auto value                              = boolean ? input & 1 : input;
        const auto select                             = bitwise_not ? ~value : value ^ 1;
        EXPECT_EQ(mux_eval(f.output(), values), select ? 26 : -7);
      }
    }
  }
}

namespace {
TEST(CpropOpSharing, PositionalAndBankedOperatorsPreserveValues) {
  for (auto op :
       {Ntype_op::Sum,  Ntype_op::Mult, Ntype_op::And,      Ntype_op::Or,       Ntype_op::Xor,      Ntype_op::EQ,    Ntype_op::LT,
        Ntype_op::GT,   Ntype_op::Ror,  Ntype_op::Not,      Ntype_op::SHL,      Ntype_op::SRA,      Ntype_op::Div,   Ntype_op::Rem,
        Ntype_op::Sext, Ntype_op::Rxor, Ntype_op::Popcount, Ntype_op::Get_mask, Ntype_op::Set_mask, Ntype_op::Concat}) {
    SCOPED_TRACE(std::string{Ntype::get_name(op)});
    Mux_graph  f("opshare_" + std::string{Ntype::get_name(op)}, 2, 8, false, true);
    const auto expr = [&](Test_pin value) {
      auto n = f.node(op);
      if (op == Ntype_op::Get_mask || op == Ntype_op::Set_mask) {
        gu::connect_mask_operands(n,
                                  value,
                                  livehd::graph_util::mask_window(livehd::graph_util::const_of(f.constant(15))).first,
                                  livehd::graph_util::mask_window(livehd::graph_util::const_of(f.constant(15))).second,
                                  op == Ntype_op::Set_mask ? f.controls[1] : Test_pin{});
      } else if (op == Ntype_op::Concat) {
        auto lane = gu::create_get_mask(*f.graph, value, 0, 8);
        n.create_sink_pin(0).connect_driver(lane.create_driver_pin(0));
        n.create_sink_pin(1).connect_driver(f.constant(8));
        n.create_sink_pin(2).connect_driver(f.controls[1]);
        n.create_sink_pin(3).connect_driver(f.constant(8));
        gu::set_ubits(n.create_driver_pin(0), 16);
      } else {
        gu::setup_sink_pid(n, 0).connect_driver(value);
        if (Ntype::sink_bank_count(op)) {
          gu::setup_sink_pid(n, Ntype::sink_bank_count(op) == 2 && op != Ntype_op::Sum ? 1 : 0).connect_driver(f.controls[1]);
        } else if (op != Ntype_op::Not) {
          n.create_sink_pin(1).connect_driver(f.constant(op == Ntype_op::SHL || op == Ntype_op::SRA ? 2 : 3));
        }
      }
      return n.create_driver_pin(0);
    };
    auto root = f.mux(f.controls[0], expr(f.a), expr(f.b));
    root.connect_sink(f.graph->get_output_pin("out"));
    std::vector<int64_t> expected;
    for (unsigned mask = 0; mask < 4; ++mask) {
      for (int a : {1, 7, 15, 31}) {
        auto inputs = f.inputs(mask, a, 5, true, true);
        expected.push_back(mux_eval(f.output(), inputs));
      }
    }
    Cprop{}.do_trans(f.graph);
    EXPECT_EQ(f.count(op), 1);
    size_t i = 0;
    for (unsigned mask = 0; mask < 4; ++mask) {
      for (int a : {1, 7, 15, 31}) {
        auto inputs = f.inputs(mask, a, 5, true, true);
        EXPECT_EQ(mux_eval(f.output(), inputs), expected[i++]);
      }
    }
  }
}

// A constant shift is wiring: `mux(s, a << 1, b << 2)` must not become
// `mux(s, a, b) << mux(s, 1, 2)`, a barrel shifter. A shared variable amount
// still shares (it saves a whole shifter).
TEST(CpropOpSharing, ConstantShiftAmountsAreNotShared) {
  for (auto op : {Ntype_op::SHL, Ntype_op::SRA}) {
    for (const bool variable : {false, true}) {
      SCOPED_TRACE(std::string{Ntype::get_name(op)} + (variable ? " variable" : " constant"));
      Mux_graph  f(std::string{"opshare_amount_"} + std::string{Ntype::get_name(op)} + (variable ? "_v" : "_c"), 3, 8, false, true);
      const auto expr = [&](Test_pin value, int amount) {
        auto n = f.node(op);
        gu::setup_sink_pid(n, 0).connect_driver(value);
        n.create_sink_pin(1).connect_driver(variable ? f.controls[amount] : f.constant(amount));
        return n.create_driver_pin(0);
      };
      f.mux(f.controls[0], expr(f.a, 1), expr(f.b, 2)).connect_sink(f.graph->get_output_pin("out"));
      std::vector<int64_t> expected;
      for (unsigned mask = 0; mask < 8; ++mask) {
        auto values = f.inputs(mask, 23, 5);
        expected.push_back(mux_eval(f.output(), values));
      }
      Cprop{}.do_trans(f.graph);
      EXPECT_EQ(f.count(op), variable ? 1u : 2u);
      for (unsigned mask = 0; mask < 8; ++mask) {
        auto values = f.inputs(mask, 23, 5);
        EXPECT_EQ(mux_eval(f.output(), values), expected[mask]);
      }
    }
  }
}

TEST(CpropOpSharing, MultisetsRetainDuplicatesAndBothBanks) {
  Mux_graph  f("opshare_banks", 2);
  const auto expr = [&](bool second) {
    auto n = f.node(Ntype_op::Sum);
    for (auto p : {second ? f.b : f.a, second ? f.b : f.a, f.controls[1]}) {
      gu::setup_sink_pid(n, 0).connect_driver(p);
    }
    gu::setup_sink_pid(n, 1).connect_driver(second ? f.a : f.b);
    gu::setup_sink_pid(n, 1).connect_driver(second ? f.clock : f.en);
    return n.create_driver_pin(0);
  };
  f.mux(f.controls[0], expr(false), expr(true)).connect_sink(f.graph->get_output_pin("out"));
  Cprop{}.do_trans(f.graph);
  ASSERT_EQ(f.count(Ntype_op::Sum), 1);
  for (unsigned mask = 0; mask < 4; ++mask) {
    auto values = f.inputs(mask, 17, 5);
    EXPECT_EQ(mux_eval(f.output(), values), (mask & 1 ? -7 : 28) + int64_t((mask >> 1) & 1));
  }
}

TEST(CpropOpSharing, ConcatDoesNotCreateOverwideMixedSignLanes) {
  Mux_graph f("opshare_concat_signed", 1, 8, true, true);
  f.graph->get_io()->set_unsign("b", true);
  gu::set_ubits(f.b, 8);
  const auto concat = [&](Test_pin value) {
    auto n = f.node(Ntype_op::Concat);
    n.create_sink_pin(0).connect_driver(value);
    n.create_sink_pin(1).connect_driver(f.constant(8));
    n.create_sink_pin(2).connect_driver(f.controls[0]);
    n.create_sink_pin(3).connect_driver(f.constant(8));
    gu::set_ubits(n.create_driver_pin(0), 16);
    return n.create_driver_pin(0);
  };
  f.mux(f.controls[0], concat(f.a), concat(f.b)).connect_sink(f.graph->get_output_pin("out"));
  Cprop{}.do_trans(f.graph);
  EXPECT_EQ(f.count(Ntype_op::Concat), 2);
  Bitwidth{10}.do_trans(f.graph);
  for (unsigned select : {0, 1}) {
    auto values = f.inputs(select, -1, 255);
    EXPECT_EQ(mux_eval(f.output(), values), (255 << 8) + select);
  }
}

TEST(CpropOpSharing, IndexedSelectionPreservesAllInRangeOperandSlots) {
  Mux_graph f("opshare_index", 1, 8, false, true);
  auto      mux           = f.node(Ntype_op::Mux);
  auto      selected_bits = gu::create_get_mask(*f.graph, f.controls[0], 0, 2);
  mux.create_sink_pin(0).connect_driver(selected_bits.create_driver_pin(0));
  for (int i = 0; i < 4; ++i) {
    auto n = f.node(Ntype_op::Sum);
    gu::setup_sink_pid(n, 0).connect_driver(f.a);
    gu::setup_sink_pid(n, 0).connect_driver(f.constant(10 + i));
    mux.create_sink_pin(i + 1).connect_driver(n.create_driver_pin(0));
  }
  mux.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
  Cprop{}.do_trans(f.graph);
  EXPECT_EQ(f.count(Ntype_op::Sum), 1);
  for (int select : {-1, 0, 1, 2, 3, 4, 128}) {
    auto values                                   = f.inputs(0, 7, 0);
    values[f.controls[0].get_class_index().value] = select;
    EXPECT_EQ(mux_eval(f.output(), values), 17 + (select & 3));
  }
}

TEST(CpropOpSharing, SharedNamedAndColoredArmsAreNotRetired) {
  for (int mode = 0; mode < 3; ++mode) {
    Mux_graph f("opshare_private_" + std::to_string(mode), 1);
    auto      a = f.node(Ntype_op::Sum), b = f.node(Ntype_op::Sum);
    gu::setup_sink_pid(a, 0).connect_driver(f.a);
    gu::setup_sink_pid(a, 0).connect_driver(f.constant(3));
    gu::setup_sink_pid(b, 0).connect_driver(f.b);
    gu::setup_sink_pid(b, 0).connect_driver(f.constant(5));
    const auto ap = a.create_driver_pin(0);
    if (mode == 0) {
      ap.connect_sink(f.graph->get_output_pin("observe"));
    } else if (mode == 1) {
      gu::set_pin_name(ap, "visible_arm");
    } else {
      gu::set_color(a, 7);
    }
    f.mux(f.controls[0], ap, b.create_driver_pin(0)).connect_sink(f.graph->get_output_pin("out"));
    Cprop{}.do_trans(f.graph);
    EXPECT_EQ(f.count(Ntype_op::Sum), 2);
    EXPECT_EQ(gu::type_op_of(f.output().get_master_node()), Ntype_op::Mux);
  }
}

TEST(CpropOpSharing, OutputNarrowingDoesNotNarrowSelectedOperands) {
  for (int hint : {0, 1, 8, 32}) {
    Mux_graph f("opshare_width_" + std::to_string(hint), 1, 8, true, true);
    f.graph->get_io()->set_bits("out", 3);
    f.graph->get_io()->set_unsign("b", true);
    gu::set_ubits(f.b, 8);
    auto a = f.node(Ntype_op::SRA), b = f.node(Ntype_op::SRA);
    a.create_sink_pin(0).connect_driver(f.a);
    b.create_sink_pin(0).connect_driver(f.b);
    for (auto n : {a, b}) {
      n.create_sink_pin(1).connect_driver(f.constant(4));
      gu::set_sbits(n.create_driver_pin(0), hint);
    }
    auto root = f.mux(f.controls[0], a.create_driver_pin(0), b.create_driver_pin(0));
    gu::set_sbits(root, hint);
    root.connect_sink(f.graph->get_output_pin("out"));
    Cprop{}.do_trans(f.graph);
    ASSERT_EQ(f.count(Ntype_op::SRA), 1);
    const auto shared  = f.output().get_master_node();
    const auto operand = gu::get_driver_of_sink_name(shared, "a");
    ASSERT_EQ(gu::type_op_of(operand.get_master_node()), Ntype_op::Mux);
    EXPECT_EQ(gu::bits_of(operand), 9) << "fresh operand mux takes the lossless s8/u8 union, never the narrow result hint";
    EXPECT_FALSE(gu::is_unsign(operand));
    Bitwidth{10}.do_trans(f.graph);
    EXPECT_EQ(gu::bits_of(operand), 9) << "signed u8/s8 union needs an extra sign bit";
    EXPECT_FALSE(gu::is_unsign(operand));
    EXPECT_EQ(gu::bits_of(f.output()), 3) << "only the final observation may be capped";
  }
}
}  // namespace

namespace {
// An unstamped Mux result is as wide as its selected arm, but an unstamped
// Get_mask (or any other op) reads as ONE bit to the LEC encoder and cgen,
// and LEC's post-inline cprop has no bitwidth pass after it. Re-typing such a
// root must carry the arms' common stamp; differing arm stamps block the
// rewrite (br_mux_bin_structured_gates was REFUTED through this).
TEST(CpropOpSharing, UnstampedRootKeepsArmRealization) {
  for (bool same_stamp : {true, false}) {
    Mux_graph  f(same_stamp ? "opshare_unstamped_root" : "opshare_unstamped_mixed", 1, 8, true);
    const auto slice = [&](Test_pin value, int bits) {
      auto n = f.node(Ntype_op::Get_mask, bits);
      gu::connect_mask_operands(n,
                                value,
                                livehd::graph_util::mask_window(livehd::graph_util::const_of(f.constant(15))).first,
                                livehd::graph_util::mask_window(livehd::graph_util::const_of(f.constant(15))).second,
                                Test_pin{});
      return n.create_driver_pin(0);
    };
    auto root = gu::create_typed_node(*f.graph, Ntype_op::Mux, 0);
    root.create_sink_pin(0).connect_driver(f.controls[0]);
    root.create_sink_pin(1).connect_driver(slice(f.a, 4));
    root.create_sink_pin(2).connect_driver(slice(f.b, same_stamp ? 4 : 5));
    auto out = root.create_driver_pin(0);
    ASSERT_EQ(gu::bits_of(out), 0);
    out.connect_sink(f.graph->get_output_pin("out"));
    Cprop{}.do_trans(f.graph);
    const auto result = f.output();
    if (!same_stamp) {
      EXPECT_EQ(f.count(Ntype_op::Get_mask), 2);
      continue;
    }
    ASSERT_EQ(gu::type_op_of(result.get_master_node()), Ntype_op::Get_mask);
    EXPECT_EQ(gu::bits_of(result), 4);
    EXPECT_TRUE(gu::is_unsign(result));
    const auto operand = gu::get_driver_of_sink_name(result.get_master_node(), "a");
    ASSERT_EQ(gu::type_op_of(operand.get_master_node()), Ntype_op::Mux);
    EXPECT_EQ(gu::bits_of(operand), 8);
    EXPECT_FALSE(gu::is_unsign(operand));
  }
}

TEST(CpropOpSharing, IndexRangeMustBeStructural) {
  Mux_graph f("opshare_index_range", 1, 8, false, true);
  gu::set_ubits(f.controls[0], 2);  // a hint is not a selector-range proof
  auto mux = f.node(Ntype_op::Mux);
  mux.create_sink_pin(0).connect_driver(f.controls[0]);
  for (int i = 0; i < 4; ++i) {
    auto n = f.node(Ntype_op::Sum);
    gu::setup_sink_pid(n, 0).connect_driver(f.a);
    gu::setup_sink_pid(n, 0).connect_driver(f.constant(i + 5));
    mux.create_sink_pin(i + 1).connect_driver(n.create_driver_pin(0));
  }
  mux.create_driver_pin(0).connect_sink(f.graph->get_output_pin("out"));
  Cprop{}.do_trans(f.graph);
  EXPECT_EQ(f.count(Ntype_op::Sum), 4);
}

TEST(CpropOpSharing, DeepCascadeHasLinearGeneratedSize) {
  constexpr int depth = 2048;
  Mux_graph     f("opshare_cascade", 1, 16, true);
  auto          a = f.a, b = f.b;
  for (int i = 0; i < depth; ++i) {
    const auto wrap = [&](Test_pin value) {
      auto n = f.node(i % 2 ? Ntype_op::Not : Ntype_op::Sum);
      gu::setup_sink_pid(n, 0).connect_driver(value);
      if (i % 2 == 0) {
        gu::setup_sink_pid(n, 0).connect_driver(f.constant(3));
      }
      return n.create_driver_pin(0);
    };
    a = wrap(a);
    b = wrap(b);
  }
  f.mux(f.controls[0], a, b).connect_sink(f.graph->get_output_pin("out"));
  Cprop{}.do_trans(f.graph);
  EXPECT_EQ(f.count(Ntype_op::Sum) + f.count(Ntype_op::Not), depth);
  EXPECT_EQ(f.count(Ntype_op::Mux), 1);
  size_t edges = 0;
  for (auto n : f.graph->body().nodes()) {
    for ([[maybe_unused]] auto p : n.inp_sorted_pins()) {
      ++edges;
    }
  }
  EXPECT_LE(edges, 2 * depth + 3);
}

// `if c_i { acc OP= b + k_i }` unrolled: mux(c_i, acc, acc OP x_i) per link,
// or the update on the false arm. `links` is the chain's update cells
// (link i: its Mux and its update cell).
struct Guard_chain {
  std::vector<hhds::Node_class> muxes, updates;
};
Guard_chain build_guard_chain(Mux_graph& f, Ntype_op op, bool update_true) {
  Guard_chain chain;
  auto        acc = f.a;
  for (int i = 0; i < 4; ++i) {
    auto term = f.node(Ntype_op::Sum);
    gu::setup_sink_pid(term, 0).connect_driver(f.b);
    gu::setup_sink_pid(term, 0).connect_driver(f.constant(3 * i + 1));
    auto upd = f.node(op);
    gu::setup_sink_pid(upd, 0).connect_driver(acc);
    gu::setup_sink_pid(upd, 0).connect_driver(term.create_driver_pin(0));
    acc = update_true ? f.mux(f.controls[i], acc, upd.create_driver_pin(0)) : f.mux(f.controls[i], upd.create_driver_pin(0), acc);
    chain.updates.push_back(upd);
    chain.muxes.push_back(acc.get_master_node());
  }
  acc.connect_sink(f.graph->get_output_pin("out"));
  return chain;
}

// Inside a guard scope (a lifted loop body) the guard moves onto the term
// (acc OP mux(c_i, id, x_i)) and the compile schedule flattens the plain
// chain into one n-ary operator: AND-OR logic, not a serial MUX chain.
TEST(CpropMux, GuardedAccumulationBecomesOneOperator) {
  for (const auto op : {Ntype_op::Or, Ntype_op::And, Ntype_op::Xor}) {
    for (const bool update_true : {true, false}) {
      SCOPED_TRACE(std::string{Ntype::get_name(op)} + (update_true ? " true-arm" : " false-arm"));
      Mux_graph f(std::string{"guard_"} + std::string{Ntype::get_name(op)} + (update_true ? "_t" : "_f"), 4, 8);
      build_guard_chain(f, op, update_true);
      std::vector<int64_t> expected;
      for (uint64_t mask = 0; mask < 16; ++mask) {
        for (const auto& [av, bv] : {std::pair{0x5a, 0x21}, std::pair{0xff, 0x0f}, std::pair{0x00, 0x7f}}) {
          auto values = f.inputs(mask, av, bv);
          expected.push_back(mux_eval(f.output(), values));
        }
      }
      optimize_lanes(f.graph, Cprop::whole_graph());
      EXPECT_EQ(f.count(Ntype_op::Mux), 4U);  // the guards, one per term
      EXPECT_EQ(f.count(op), 1U);
      const auto root = f.output().get_master_node();
      EXPECT_EQ(gu::type_op_of(root), op);
      EXPECT_EQ(root.inp_pins_snapshot().size(), 5U);
      size_t i = 0;
      for (uint64_t mask = 0; mask < 16; ++mask) {
        for (const auto& [av, bv] : {std::pair{0x5a, 0x21}, std::pair{0xff, 0x0f}, std::pair{0x00, 0x7f}}) {
          auto values = f.inputs(mask, av, bv);
          EXPECT_EQ(mux_eval(f.output(), values), expected[i++]) << mask;
        }
      }
    }
  }
}

// Outside a guard scope -- straight-line code, a hand-unrolled run like
// br_enc_onehot2bin -- the chain keeps every Mux on the accumulator: the
// rolled-loop rewrite must not change what loop-free code compiles to. A
// partial scope (synthesis' loop cleanup admits only the cells an expansion
// minted) rewrites exactly the links whose Mux and update cell it admits.
TEST(CpropMux, GuardedAccumulationOnlyInsideTheGuardScope) {
  for (const auto op : {Ntype_op::Or, Ntype_op::And, Ntype_op::Xor}) {
    SCOPED_TRACE(std::string{Ntype::get_name(op)});
    {
      Mux_graph f(std::string{"guard_off_"} + std::string{Ntype::get_name(op)}, 4, 8);
      build_guard_chain(f, op, true);
      optimize_lanes(f.graph);
      EXPECT_EQ(gu::type_op_of(f.output().get_master_node()), Ntype_op::Mux);
      EXPECT_EQ(f.count(Ntype_op::Mux), 4U);
      EXPECT_EQ(f.count(op), 4U);  // one update per link, still behind its Mux
    }
    {
      Mux_graph f(std::string{"guard_partial_"} + std::string{Ntype::get_name(op)}, 4, 8);
      auto      chain = build_guard_chain(f, op, true);
      // Admit the last link only: its Mux and its update cell. A link whose
      // update cell is outside the scope stays a Mux even if its Mux is in.
      absl::flat_hash_set<hhds::Node_class> admitted{chain.muxes[3], chain.updates[3], chain.muxes[1]};
      std::vector<int64_t>                  expected;
      for (uint64_t mask = 0; mask < 16; ++mask) {
        auto values = f.inputs(mask, 0x5a, 0x21);
        expected.push_back(mux_eval(f.output(), values));
      }
      Cprop cprop{true};
      cprop.set_guard_scope([&](const hhds::Node_class& n) { return admitted.contains(n); });
      cprop.do_trans(f.graph);
      Bitwidth{10}.do_trans(f.graph);
      const auto root = f.output().get_master_node();
      EXPECT_EQ(gu::type_op_of(root), op);  // the admitted link moved its guard
      size_t guards = 0;
      for (auto n : f.graph->body().nodes()) {
        if (gu::type_op_of(n) != Ntype_op::Mux) {
          continue;
        }
        for (const auto& sink : n.inp_sorted_pins()) {
          if (sink.get_port_id() != 0 && sink.get_driver_pin().is_const()) {
            ++guards;  // an identity arm: a moved guard, off the accumulator
          }
        }
      }
      EXPECT_EQ(guards, 1U);
      EXPECT_EQ(f.count(Ntype_op::Mux), 4U);  // three links still hold the accumulator
      for (uint64_t mask = 0; mask < 16; ++mask) {
        auto values = f.inputs(mask, 0x5a, 0x21);
        EXPECT_EQ(mux_eval(f.output(), values), expected[mask]) << mask;
      }
    }
  }
}

// `w = s ? a : a ^ 9` twice (sim_loop_mux_arm's two copies of the low
// nibble): the update cell has two consumers until CSE merges the muxes, so
// the guard can only appear in a later sweep. Bitwidth_rewrite runs scalar_mux
// after inference converged; a guard minted THERE kept no width and the
// emitted Verilog declared the constant-arm mux(s, 9, 0) one bit wide. Every
// value cell must end stamped, and both outputs keep their values.
TEST(CpropMux, GuardedAccumulationNeverLeavesAnUnstampedCell) {
  Mux_graph f("guard_const_term", 1, 4);
  auto      upd = f.node(Ntype_op::Xor);
  gu::setup_sink_pid(upd, 0).connect_driver(f.a);
  gu::setup_sink_pid(upd, 0).connect_driver(f.constant(9));
  f.mux(f.controls[0], upd.create_driver_pin(0), f.a).connect_sink(f.graph->get_output_pin("out"));
  f.mux(f.controls[0], upd.create_driver_pin(0), f.a).connect_sink(f.graph->get_output_pin("observe"));
  // One round, as the compile schedule's last: CSE merges the muxes in this
  // sweep, so the next scalar_mux to see them is Bitwidth_rewrite's.
  Cprop cprop{true};
  cprop.set_guard_scope(Cprop::whole_graph());
  cprop.do_trans(f.graph);
  Bitwidth{10}.do_trans(f.graph);
  for (auto node : f.graph->body().nodes()) {
    if (!gu::is_builtin_node(node)) {
      EXPECT_GT(gu::bits_of(node.get_driver_pin(0)), 0) << gu::debug_name(node);
    }
  }
  for (uint64_t mask = 0; mask < 2; ++mask) {
    for (int64_t av = 0; av < 16; ++av) {
      auto values = f.inputs(mask, av, 0);
      EXPECT_EQ(mux_eval(f.output(), values), mask ? av : (av ^ 9));
      auto values2 = f.inputs(mask, av, 0);
      EXPECT_EQ(mux_eval(f.observed(), values2), mask ? av : (av ^ 9));
    }
  }
}

// The guard rewrite needs a private update cell that reads the held value: a
// shared or named update, a colored Mux, and an update without the held
// value keep the Mux.
TEST(CpropMux, GuardedAccumulationNeedsAPrivateUpdate) {
  const auto build = [](const std::string& name, int variant) {
    auto f   = std::make_unique<Mux_graph>(name, 1, 8);
    auto upd = f->node(Ntype_op::Or);
    gu::setup_sink_pid(upd, 0).connect_driver(variant == 3 ? f->constant(3) : f->a);
    gu::setup_sink_pid(upd, 0).connect_driver(f->b);
    auto root = f->mux(f->controls[0], f->a, upd.create_driver_pin(0));
    root.connect_sink(f->graph->get_output_pin("out"));
    if (variant == 0) {
      upd.create_driver_pin(0).connect_sink(f->graph->get_output_pin("observe"));  // shared
    } else if (variant == 1) {
      gu::set_pin_name(upd.create_driver_pin(0), "named_update");
    } else if (variant == 2) {
      gu::set_color(root.get_master_node(), 3);
    }
    return f;
  };
  for (int variant = 0; variant < 4; ++variant) {
    SCOPED_TRACE(variant);
    auto  f = build("guard_keep_" + std::to_string(variant), variant);
    Cprop cprop{true};
    cprop.set_guard_scope(Cprop::whole_graph());
    cprop.do_trans(f->graph);
    EXPECT_EQ(gu::type_op_of(f->output().get_master_node()), Ntype_op::Mux);
  }
}
}  // namespace

TEST(CpropMuxContextSchedule, CompileScheduleDoesNotRegrowPrunedTrees) {
  Mux_graph  f("context_schedule", 1, 8, false, true);
  const auto e3 = f.eq(f.controls[0], 3), e5 = f.eq(f.controls[0], 5);
  auto       root = f.mux(e3, f.mux(e3, f.a, f.b), f.mux(e5, f.b, f.a));
  root.connect_sink(f.graph->get_output_pin("out"));
  const auto size = [&]() {
    size_t nodes = 0, edges = 0;
    for (auto n : f.graph->body().nodes()) {
      ++nodes;
      for ([[maybe_unused]] auto pin : n.inp_sorted_pins()) {
        ++edges;
      }
    }
    return std::pair{nodes, edges};
  };
  optimize_state(f.graph);
  const auto first = size();
  optimize_state(f.graph);
  EXPECT_EQ(size(), first);
}
