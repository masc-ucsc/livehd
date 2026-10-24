// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "clock_gate.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "latch_contract.hpp"
#include "node_util.hpp"
#include "query.hpp"

namespace livehd::clock_gate {
namespace {

namespace gu = livehd::graph_util;

// clk, a, b, c, rst: 1-bit inputs; d: 8-bit data; outputs q0..q3 (8 bits).
struct Fixture {
  hhds::GraphLibrary             library;
  std::shared_ptr<hhds::GraphIO> io;
  std::shared_ptr<hhds::Graph>   graph;

  explicit Fixture(const std::string& name = "top") {
    io                = library.create_io(name);
    hhds::Port_id pid = 1;
    for (const char* in : {"clk", "a", "b", "c", "rst", "d"}) {
      io->add_input(in, pid++);
      io->set_bits(in, std::string{in} == "d" ? 8 : 1);
      io->set_unsign(in, true);
    }
    for (const char* out : {"q0", "q1", "q2", "q3"}) {
      io->add_output(out, pid++);
      io->set_bits(out, 8);
      io->set_unsign(out, true);
    }
    graph = io->create_graph();
  }

  int regs_ = 0;

  hhds::Pin_class in(const char* name) { return graph->get_input_pin(name); }

  // Every output must be driven for the LEC to compare the design: tie the unused ones to 0.
  void tie_unused_outputs() {
    for (const char* port : {"q0", "q1", "q2", "q3"}) {
      if (graph->get_output_pin(port).get_driver_pin().is_invalid()) {
        gu::create_const(*graph, *Dlop::create_integer(0)).connect_sink(graph->get_output_pin(port));
      }
    }
  }

  hhds::Pin_class bit(Ntype_op op, std::vector<hhds::Pin_class> operands) {
    auto n = gu::create_typed_node(*graph, op);
    for (const auto& o : operands) {
      gu::setup_sink_pid(n, 0).connect_driver(o);
    }
    auto out = n.create_driver_pin(0);
    gu::set_bits(out, 1);
    gu::set_unsign(out);
    return out;
  }

  // A register on `clk` fed by d, with an optional enable and an optional synchronous reset to 0.
  hhds::Node_class reg(const hhds::Pin_class& enable, const hhds::Pin_class& clk, bool sync_reset = false, int bits = 8,
                       const hhds::Pin_class& din = hhds::Pin_class{}) {
    auto flop = gu::create_typed_node(*graph, Ntype_op::Flop, bits);
    flop.set_name("r" + std::to_string(regs_++));  // named, so the LEC pairs the state of the two sides exactly
    gu::set_unsign(flop.create_driver_pin(0));
    (din.is_invalid() ? in("d") : din).connect_sink(gu::setup_sink_by_name(flop, "din"));
    clk.connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
    if (!enable.is_invalid()) {
      enable.connect_sink(gu::setup_sink_by_name(flop, "enable"));
    }
    if (sync_reset) {
      in("rst").connect_sink(gu::setup_sink_by_name(flop, "reset_pin"));
      gu::create_const(*graph, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(flop, "initial"));
    }
    return flop;
  }

  void out(const char* port, const hhds::Node_class& flop) { flop.create_driver_pin(0).connect_sink(graph->get_output_pin(port)); }

  hhds::Pin_class gate(const hhds::Pin_class& clk, const hhds::Pin_class& enable) {
    auto cell = gu::create_typed_node(*graph, Ntype_op::Clock_cell);
    clk.connect_sink(gu::setup_sink_by_name(cell, "clk_ref"));
    enable.connect_sink(gu::setup_sink_by_name(cell, "en"));
    auto out = cell.create_driver_pin(0);
    gu::set_bits(out, 1);
    gu::set_unsign(out);
    return out;
  }

  size_t count(Ntype_op op) const {
    size_t n = 0;
    for (auto node : graph->body().nodes()) {
      n += gu::type_op_of(node) == op ? 1 : 0;
    }
    return n;
  }
};

std::optional<Clock_gate_stats> infer(Fixture& f, uint32_t min_bits = 8, uint32_t depth = 2) {
  const std::vector<std::shared_ptr<hhds::Graph>> defs{f.graph};
  return infer_clock_gates(defs, {.min_bits = min_bits, .depth = depth});
}

// The gated design is the source design, proven by the LEC engine (a Clock_cell is the edge-sampled enable it stands for). The
// shapes below are the ones the engine decides on the bare graph; the end-to-end proof of the mapped netlist, with the clock gates
// folded back into enables by pass.single_edge, is lhd/tests/lhd_clock_gate_test.sh.
void expect_equivalent(const std::function<void(Fixture&)>& build, uint32_t min_bits = 8, uint32_t depth = 2) {
  Fixture ref("ref"), impl("impl");
  build(ref);
  build(impl);
  ref.tie_unused_outputs();
  impl.tie_unused_outputs();
  ASSERT_TRUE(infer(impl, min_bits, depth));
  livehd::lec::Lec_options options;
  options.cones     = "false";
  options.timeout   = 20;
  const auto result = livehd::lec::prove_equal(ref.graph.get(), impl.graph.get(), options);
  EXPECT_EQ(result.verdict, livehd::lec::Verdict::Proven) << result.detail << " " << result.witness;
}

TEST(ClockGate, DeMorganMappedIcgPreservesClockAndLatchPolarity) {
  for (const bool idle_high : {false, true}) {
    for (const bool invert_clock : {false, true}) {
      Fixture f;
      auto    clock     = f.in("clk");
      auto    not_clock = f.bit(Ntype_op::Not, {clock});
      auto    latch     = gu::create_typed_node(*f.graph, Ntype_op::Latch, 1);
      (idle_high ? clock : not_clock).connect_sink(gu::setup_sink_by_name(latch, "enable"));
      f.in("a").connect_sink(gu::setup_sink_by_name(latch, "din"));
      auto sampled       = latch.create_driver_pin(0);
      auto latch_operand = idle_high ? sampled : f.bit(Ntype_op::Not, {sampled});
      auto gate          = f.bit(idle_high ? Ntype_op::And : Ntype_op::Or, {invert_clock ? clock : not_clock, latch_operand});
      f.bit(Ntype_op::Not, {gate}).connect_sink(f.graph->get_output_pin("q0"));
      const auto match = livehd::latch_contract::match_icg_def(f.graph.get());
      if (invert_clock) {
        EXPECT_FALSE(match);
      } else {
        ASSERT_TRUE(match);
        EXPECT_EQ(match->clk_in, clock);
        EXPECT_EQ(match->enable_cone, f.in("a"));
        EXPECT_EQ(match->invert, idle_high);
      }
    }
  }
}

TEST(ClockGate, RegistersSharingAnEnableConjunctMoveOntoOneGate) {
  Fixture f;
  auto    ab = f.bit(Ntype_op::And, {f.in("a"), f.in("b")});
  auto    ac = f.bit(Ntype_op::And, {f.in("a"), f.in("c")});
  auto    r0 = f.reg(ab, f.in("clk"));
  auto    r1 = f.reg(ac, f.in("clk"));
  auto    r2 = f.reg(f.in("a"), f.in("clk"));
  f.out("q0", r0);
  f.out("q1", r1);
  f.out("q2", r2);
  const auto stats = infer(f, 8, 1);
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->gates, 1U);
  EXPECT_EQ(stats->registers, 3U);
  EXPECT_EQ(stats->bits, 24U);
  EXPECT_EQ(stats->enables, 1U);  // r2's enable was exactly `a`
  EXPECT_EQ(f.count(Ntype_op::Clock_cell), 1U);
  // r0 keeps `b`, r1 keeps `c`, r2 none.
  EXPECT_TRUE(gu::get_driver_of_sink_name(r2, "enable").is_invalid());
  EXPECT_TRUE(gu::get_driver_of_sink_name(r0, "enable") == f.in("b"));
  EXPECT_TRUE(gu::get_driver_of_sink_name(r1, "enable") == f.in("c"));
  const auto gclk = gu::get_driver_of_sink_name(r0, "clock_pin");
  EXPECT_TRUE(gclk == gu::get_driver_of_sink_name(r1, "clock_pin"));
  EXPECT_TRUE(gclk == gu::get_driver_of_sink_name(r2, "clock_pin"));
  EXPECT_TRUE(gu::type_op_of(gclk.get_master_node()) == Ntype_op::Clock_cell);
  EXPECT_TRUE(gu::get_driver_of_sink_name(gclk.get_master_node(), "en") == f.in("a"));
}

TEST(ClockGate, SmallGroupsAndOtherClocksAreLeftAlone) {
  Fixture f;
  auto    r0 = f.reg(f.in("a"), f.in("clk"), false, 4);
  auto    r1 = f.reg(f.in("b"), f.in("clk"), false, 4);
  f.out("q0", r0);
  f.out("q1", r1);
  const auto stats = infer(f, 8, 2);
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->gates, 0U);  // 4 bits per enable, below the 8-bit minimum
  EXPECT_EQ(f.count(Ntype_op::Clock_cell), 0U);
}

// A closed gate skips only logic nothing else reads: a register fed from a port (the leaf of a pipeline) skips nothing.
TEST(ClockGate, MinConeLeavesRegistersWithoutAPrivateConeOnTheirPlainEnables) {
  Fixture f;
  auto    r0 = f.reg(f.in("a"), f.in("clk"));
  auto    r1 = f.reg(f.in("a"), f.in("clk"));
  f.out("q0", r0);
  f.out("q1", r1);
  const std::vector<std::shared_ptr<hhds::Graph>> defs{f.graph};
  const auto                                      stats = infer_clock_gates(defs, {.min_bits = 8, .min_cone = 2, .depth = 1});
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->gates, 0U);
  EXPECT_EQ(f.count(Ntype_op::Clock_cell), 0U);
}

TEST(ClockGate, MinConeGatesRegistersWhoseNextStateConeIsPrivate) {
  Fixture f;
  auto    x0 = f.bit(Ntype_op::Xor, {f.in("b"), f.in("c")});
  auto    x1 = f.bit(Ntype_op::And, {x0, f.in("b")});
  auto    y0 = f.bit(Ntype_op::Xor, {f.in("c"), f.in("rst")});
  auto    y1 = f.bit(Ntype_op::And, {y0, f.in("c")});
  auto    r0 = f.reg(f.in("a"), f.in("clk"), false, 8, x1);
  auto    r1 = f.reg(f.in("a"), f.in("clk"), false, 8, y1);
  f.out("q0", r0);
  f.out("q1", r1);
  const std::vector<std::shared_ptr<hhds::Graph>> defs{f.graph};
  const auto                                      stats = infer_clock_gates(defs, {.min_bits = 8, .min_cone = 4, .depth = 1});
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->gates, 1U);
}

TEST(ClockGate, MinConeDoesNotCountLogicSomethingElseAlsoReads) {
  Fixture f;
  auto    x0 = f.bit(Ntype_op::Xor, {f.in("b"), f.in("c")});
  auto    x1 = f.bit(Ntype_op::And, {x0, f.in("b")});
  auto    y0 = f.bit(Ntype_op::Xor, {f.in("c"), f.in("rst")});
  auto    y1 = f.bit(Ntype_op::And, {y0, f.in("c")});
  auto    r0 = f.reg(f.in("a"), f.in("clk"), false, 8, x1);
  auto    r1 = f.reg(f.in("a"), f.in("clk"), false, 8, y1);
  f.out("q0", r0);
  f.out("q1", r1);
  x1.connect_sink(f.graph->get_output_pin("q2"));  // x1 is also an output: it runs whether or not the gate is open
  y1.connect_sink(f.graph->get_output_pin("q3"));
  const std::vector<std::shared_ptr<hhds::Graph>> defs{f.graph};
  const auto                                      stats = infer_clock_gates(defs, {.min_bits = 8, .min_cone = 4, .depth = 1});
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->gates, 0U);
}

TEST(ClockGate, ASynchronousResetKeepsTheClockRunningForTheReset) {
  Fixture f;
  auto    r0 = f.reg(f.in("a"), f.in("clk"), true);
  f.out("q0", r0);
  const auto stats = infer(f, 8, 1);
  ASSERT_TRUE(stats);
  ASSERT_EQ(stats->gates, 1U);
  const auto gclk = gu::get_driver_of_sink_name(r0, "clock_pin");
  const auto en   = gu::get_driver_of_sink_name(gclk.get_master_node(), "en");
  // The gate's enable is `a | rst`, never plain `a`: a stopped clock would not load the reset value.
  ASSERT_FALSE(en.is_invalid());
  EXPECT_TRUE(gu::type_op_of(en.get_master_node()) == Ntype_op::Or);
}

TEST(ClockGate, NestedGatesFactorTheNextSharedConjunct) {
  Fixture f;
  auto    ab  = f.bit(Ntype_op::And, {f.in("a"), f.in("b")});
  auto    ac  = f.bit(Ntype_op::And, {f.in("a"), f.in("c")});
  auto    abc = f.bit(Ntype_op::And, {f.in("a"), f.in("b"), f.in("c")});
  auto    r0  = f.reg(ab, f.in("clk"));
  auto    r1  = f.reg(abc, f.in("clk"));
  auto    r2  = f.reg(ac, f.in("clk"));
  f.out("q0", r0);
  f.out("q1", r1);
  f.out("q2", r2);
  const auto stats = infer(f, 16, 2);
  ASSERT_TRUE(stats);
  // `a` is shared by all three, then `b` by r0 and r1 (16 bits): two gates, the second clocked by the first.
  EXPECT_EQ(stats->gates, 2U);
  EXPECT_EQ(f.count(Ntype_op::Clock_cell), 2U);
  const auto inner = gu::get_driver_of_sink_name(r0, "clock_pin");
  EXPECT_TRUE(inner == gu::get_driver_of_sink_name(r1, "clock_pin"));
  const auto outer = gu::get_driver_of_sink_name(inner.get_master_node(), "clk_ref");
  EXPECT_TRUE(gu::type_op_of(outer.get_master_node()) == Ntype_op::Clock_cell);
  EXPECT_TRUE(gu::get_driver_of_sink_name(r2, "clock_pin") == outer);
}

// Gates for different shared enables on one clock are SIBLINGS on that clock, not a chain.
TEST(ClockGate, IndependentGatesAreBothClockedByTheOriginalClock) {
  Fixture f;
  auto    r0 = f.reg(f.in("a"), f.in("clk"));
  auto    r1 = f.reg(f.in("b"), f.in("clk"));
  f.out("q0", r0);
  f.out("q1", r1);
  const auto stats = infer(f, 8, 2);
  ASSERT_TRUE(stats);
  ASSERT_EQ(stats->gates, 2U);
  for (const auto& r : {r0, r1}) {
    const auto gclk = gu::get_driver_of_sink_name(r, "clock_pin");
    ASSERT_TRUE(gu::type_op_of(gclk.get_master_node()) == Ntype_op::Clock_cell);
    EXPECT_TRUE(gu::get_driver_of_sink_name(gclk.get_master_node(), "clk_ref") == f.in("clk"));
  }
}

TEST(ClockGate, ExistingGatesInARowCollapse) {
  Fixture f;
  // outer checks a; the next one checks a & b (only b is new, and the cascade becomes one level); the third checks a again
  // (nothing new): it IS the outer gate.
  auto    outer = f.gate(f.in("clk"), f.in("a"));
  auto    inner = f.gate(outer, f.bit(Ntype_op::And, {f.in("a"), f.in("b")}));
  auto    same  = f.gate(outer, f.in("a"));
  auto    r0    = f.reg({}, inner);
  auto    r1    = f.reg({}, same);
  f.out("q0", r0);
  f.out("q1", r1);
  const std::vector<std::shared_ptr<hhds::Graph>> defs{f.graph};
  const auto                                      stats = collapse_clock_gates(defs);
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->collapsed, 2U);
  EXPECT_EQ(f.count(Ntype_op::Clock_cell), 2U);  // the outer gate and the inner one, now a sibling checking a & b
  EXPECT_TRUE(gu::get_driver_of_sink_name(r1, "clock_pin") == outer);
  const auto gclk = gu::get_driver_of_sink_name(r0, "clock_pin");
  EXPECT_TRUE(gu::get_driver_of_sink_name(gclk.get_master_node(), "clk_ref") == f.in("clk"));
  const auto en = gu::get_driver_of_sink_name(gclk.get_master_node(), "en");
  ASSERT_TRUE(gu::type_op_of(en.get_master_node()) == Ntype_op::And);
}

TEST(ClockGate, CollapsePreservesAStoppedOuterClock) {
  Fixture f;
  auto    outer = f.gate(f.in("clk"), gu::create_const(*f.graph, *Dlop::create_integer(0)));
  auto    inner = f.gate(outer, f.in("a"));
  auto    reg   = f.reg({}, inner);
  f.out("q0", reg);
  const std::vector<std::shared_ptr<hhds::Graph>> defs{f.graph};
  ASSERT_TRUE(collapse_clock_gates(defs));
  const auto clock = gu::get_driver_of_sink_name(reg, "clock_pin");
  ASSERT_FALSE(clock.is_invalid());
  EXPECT_TRUE(gu::get_driver_of_sink_name(clock.get_master_node(), "clk_ref") == outer);
  EXPECT_EQ(f.count(Ntype_op::Clock_cell), 2U);
}

TEST(ClockGate, IdenticalGatesMergeAndImpliedRegisterEnablesVanish) {
  Fixture f;
  auto    g0 = f.gate(f.in("clk"), f.in("a"));
  auto    g1 = f.gate(f.in("clk"), f.in("a"));
  auto    r0 = f.reg(f.in("a"), g0);  // the gate already checks a
  auto    r1 = f.reg(f.in("b"), g1);
  f.out("q0", r0);
  f.out("q1", r1);
  const std::vector<std::shared_ptr<hhds::Graph>> defs{f.graph};
  const auto                                      stats = collapse_clock_gates(defs);
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->duplicates, 1U);
  EXPECT_EQ(stats->redundant, 1U);
  EXPECT_EQ(f.count(Ntype_op::Clock_cell), 1U);
  EXPECT_TRUE(gu::get_driver_of_sink_name(r0, "enable").is_invalid());
  EXPECT_TRUE(gu::get_driver_of_sink_name(r1, "enable") == f.in("b"));
}

TEST(ClockGate, GatedDesignsAreEquivalentToTheEnableForm) {
  expect_equivalent(
      [](Fixture& f) {
        auto ab = f.bit(Ntype_op::And, {f.in("a"), f.in("b")});
        auto ac = f.bit(Ntype_op::And, {f.in("a"), f.in("c")});
        f.out("q0", f.reg(ab, f.in("clk")));
        f.out("q1", f.reg(ac, f.in("clk")));
        f.out("q2", f.reg(f.in("a"), f.in("clk")));
      },
      8,
      2);
}

TEST(ClockGate, OneSharedEnableIsEquivalent) {
  expect_equivalent(
      [](Fixture& f) {
        f.out("q0", f.reg(f.in("a"), f.in("clk")));
        f.out("q1", f.reg(f.in("a"), f.in("clk")));
      },
      8,
      1);
}

TEST(ClockGate, ANestedGateIsEquivalent) {
  expect_equivalent(
      [](Fixture& f) {
        auto ab = f.bit(Ntype_op::And, {f.in("a"), f.in("b")});
        f.out("q0", f.reg(ab, f.in("clk")));
        f.out("q1", f.reg(f.in("a"), f.in("clk")));
      },
      8,
      2);
}

TEST(ClockGate, AConjunctionIsEquivalent) {
  expect_equivalent(
      [](Fixture& f) {
        auto ab = f.bit(Ntype_op::And, {f.in("a"), f.in("b")});
        auto ac = f.bit(Ntype_op::And, {f.in("a"), f.in("c")});
        f.out("q0", f.reg(ab, f.in("clk")));
        f.out("q1", f.reg(ac, f.in("clk")));
      },
      8,
      1);
}

TEST(ClockGate, SynchronousResetDesignsAreEquivalentToo) {
  expect_equivalent(
      [](Fixture& f) {
        auto ab = f.bit(Ntype_op::And, {f.in("a"), f.in("b")});
        f.out("q0", f.reg(ab, f.in("clk"), true));
        f.out("q1", f.reg(f.in("a"), f.in("clk"), true));
      },
      8,
      2);
}

}  // namespace
}  // namespace livehd::clock_gate
