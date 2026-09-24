// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "satopt_sweep.hpp"

#include "gtest/gtest.h"
#include "node_util.hpp"
#include "satopt.hpp"

namespace gu = livehd::graph_util;
using livehd::satopt::Sweep;

namespace {
struct Fixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> g;
  hhds::Pin_class              x, y;
  Fixture(std::string_view name, bool sign = false) {
    auto io = lib.create_io(name);
    io->add_input("x", 1);
    io->set_bits("x", 8);
    io->set_unsign("x", !sign);
    io->add_input("y", 2);
    io->set_bits("y", 8);
    io->set_unsign("y", !sign);
    io->add_output("o", 3);
    io->add_output("p", 4);
    g = io->create_graph();
    x = g->get_input_pin("x");
    y = g->get_input_pin("y");
    bits(x, 8, sign);
    bits(y, 8, sign);
  }
  static void bits(const hhds::Pin_class& p, int w, bool sign) {
    if (sign) {
      gu::set_sbits(p, w);
    } else {
      gu::set_ubits(p, w);
    }
  }
  hhds::Pin_class konst(int64_t v) { return gu::create_const(*g, *Dlop::create_integer(v)); }
  hhds::Pin_class op(Ntype_op kind, std::initializer_list<hhds::Pin_class> ins, int w, bool sign = false) {
    auto n = gu::create_typed_node(*g, kind);
    for (const auto& p : ins) {
      p.connect_sink(gu::setup_sink_pid(n, 0));
    }
    auto out = n.create_driver_pin(0);
    bits(out, w, sign);
    return out;
  }
  void out(std::string_view name, const hhds::Pin_class& p) { p.connect_sink(g->get_output_pin(name)); }
  hhds::Pin_class driver(std::string_view name) { return g->get_output_pin(name).get_driver_pin(); }
  int             count(Ntype_op kind) const {
    int n = 0;
    for (const auto node : g->body().nodes()) {
      n += gu::type_op_of(node) == kind;
    }
    return n;
  }
  livehd::satopt::Stage_report run(Sweep kind, livehd::satopt::Profile profile = livehd::satopt::Profile::shared) {
    livehd::satopt::Stage_report report;
    livehd::satopt::Meter        meter(livehd::satopt::Budget::unlimited());
    meter.begin_stage(0);
    livehd::satopt::sweep_values({g}, kind, profile, {}, report, meter);
    return report;
  }
  // Both outputs, one simulated column each, to compare before and after.
  std::vector<Dlop> samples() {
    std::vector<Dlop> all;
    for (auto name : {"o", "p"}) {
      auto v = livehd::satopt::Satopt_seeds{}.sample(driver(name));
      EXPECT_TRUE(v.has_value()) << name;
      if (v) {
        all.insert(all.end(), v->begin(), v->end());
      }
    }
    return all;
  }
};
void expect_same(const std::vector<Dlop>& a, const std::vector<Dlop>& b) {
  ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_TRUE(a[i].is_known_eq(b[i])) << i;
  }
}
}  // namespace

// (x | 1) & 1 is always 1: no operand is constant, so only the sweep sees it.
TEST(SatoptSweep, HiddenConstantOutputBecomesTheConstant) {
  Fixture f("sweep_constant");
  f.out("o", f.op(Ntype_op::And, {f.op(Ntype_op::Or, {f.x, f.konst(1)}, 8), f.konst(1)}, 8));
  f.out("p", f.op(Ntype_op::Sum, {f.x, f.y}, 9));
  const auto before = f.samples();
  const auto report = f.run(Sweep::constants);
  // The output constant, and the Or's low bit (always 1: a low-lane fact
  // that is never applied -- replacing the output sweeps the Or's cone).
  EXPECT_EQ(report.proven, 2u);
  EXPECT_EQ(report.applied, 1u);
  const auto o = f.driver("o");
  ASSERT_TRUE(o.is_const());
  EXPECT_TRUE(gu::const_of(o).is_known_eq(*Dlop::create_integer(1)));
  EXPECT_EQ(f.count(Ntype_op::Or), 0);  // the dead cone is swept
  expect_same(before, f.samples());
}

// A signed one-bit result with bit 0 set is -1, including for wider readers.
TEST(SatoptSweep, SignedConstantPreservesExtension) {
  Fixture    f("sweep_signed_constant");
  const auto one = f.op(Ntype_op::And, {f.op(Ntype_op::Or, {f.x, f.konst(1)}, 8), f.konst(1)}, 1, true);
  f.out("o", f.op(Ntype_op::And, {one, f.konst(255)}, 8));
  f.out("p", one);
  const auto report = f.run(Sweep::constants);
  EXPECT_GE(report.applied, 1u);
  ASSERT_TRUE(f.driver("p").is_const());
  EXPECT_TRUE(gu::const_of(f.driver("p")).is_known_eq(*Dlop::create_integer(-1)));
  ASSERT_TRUE(f.driver("o").is_const());
  EXPECT_TRUE(gu::const_of(f.driver("o")).is_known_eq(*Dlop::create_integer(255)));
}

// y + x recomputes x + y: its consumer reads the earlier value.
TEST(SatoptSweep, DuplicateValueReadsTheEarlierOne) {
  Fixture f("sweep_equal");
  const auto a = f.op(Ntype_op::Sum, {f.x, f.y}, 9);
  f.out("o", a);
  f.out("p", f.op(Ntype_op::Sum, {f.y, f.x}, 9));
  const auto before = f.samples();
  const auto report = f.run(Sweep::equiv);
  EXPECT_EQ(report.proven, 1u);
  EXPECT_EQ(f.count(Ntype_op::Sum), 1);
  EXPECT_EQ(f.driver("o"), f.driver("p"));
  expect_same(before, f.samples());
  // Nothing left to find.
  EXPECT_EQ(f.run(Sweep::equiv).proven, 0u);
}

// A bucket limits alternate representatives, not how many duplicates can be
// eliminated. More than the old 64-member cap must still share one Sum.
TEST(SatoptSweep, LargeEquivalenceClassSharesOneRepresentative) {
  Fixture f("sweep_many_equal");
  auto    gather = gu::create_typed_node(*f.g, Ntype_op::Or);
  for (int i = 0; i < 80; ++i) {
    f.op(Ntype_op::Sum, {f.x, f.y}, 9).connect_sink(gu::setup_sink_pid(gather, 0));
  }
  gu::set_ubits(gather.create_driver_pin(0), 9);
  f.out("o", gather.create_driver_pin(0));
  const auto report = f.run(Sweep::equiv);
  EXPECT_GE(report.applied, 79u);
  EXPECT_EQ(f.count(Ntype_op::Sum), 1);
}

TEST(SatoptSweep, CounterexamplesKeepTheRepresentativeIndexUsable) {
  Fixture f("sweep_equal_refinement");
  auto    gather = gu::create_typed_node(*f.g, Ntype_op::Or);
  // Corner patterns assign the same word to x and y: these first two sums
  // collide until a solver counterexample distinguishes them.
  for (auto p : {f.op(Ntype_op::Sum, {f.x, f.y}, 9), f.op(Ntype_op::Sum, {f.x, f.x}, 9), f.op(Ntype_op::Sum, {f.y, f.x}, 9)}) {
    p.connect_sink(gu::setup_sink_pid(gather, 0));
  }
  gu::set_ubits(gather.create_driver_pin(0), 9);
  f.out("o", gather.create_driver_pin(0));
  livehd::satopt::Budget budget;
  budget.samples = 7;
  livehd::satopt::Meter        meter(budget);
  livehd::satopt::Stage_report report;
  livehd::satopt::sweep_values({f.g}, Sweep::equiv, livehd::satopt::Profile::shared, {}, report, meter);
  EXPECT_GT(report.refuted, 0u);
  EXPECT_GE(report.applied, 1u);
  EXPECT_EQ(f.count(Ntype_op::Sum), 2);
}

// (x ^ 255) ^ y is the u8 complement of x ^ y: two cells become one Xor with
// the width's mask.
TEST(SatoptSweep, UnsignedComplementBecomesAMaskXor) {
  Fixture f("sweep_complement");
  f.out("o", f.op(Ntype_op::Xor, {f.x, f.y}, 8));
  f.out("p", f.op(Ntype_op::Xor, {f.op(Ntype_op::Xor, {f.x, f.konst(255)}, 8), f.y}, 8));
  const auto before = f.samples();
  EXPECT_EQ(f.count(Ntype_op::Xor), 3);
  const auto report = f.run(Sweep::complement);
  EXPECT_EQ(report.proven, 1u);
  EXPECT_EQ(f.count(Ntype_op::Xor), 2);
  const auto p = f.driver("p");
  ASSERT_EQ(gu::type_op_of(p.get_master_node()), Ntype_op::Xor);
  expect_same(before, f.samples());
}

// A signed complement is a Not.
TEST(SatoptSweep, SignedComplementBecomesANot) {
  Fixture f("sweep_signed", true);
  f.out("o", f.op(Ntype_op::Sum, {f.x, f.y}, 9, true));
  // ~(x + y) == (-1 - x) - y, built without a Not.
  auto neg = gu::create_typed_node(*f.g, Ntype_op::Sum);
  f.konst(-1).connect_sink(gu::setup_sink_pid(neg, 0));
  f.x.connect_sink(gu::setup_sink_pid(neg, 1));
  f.y.connect_sink(gu::setup_sink_pid(neg, 1));
  gu::set_sbits(neg.create_driver_pin(0), 9);
  auto wrap = f.op(Ntype_op::And, {neg.create_driver_pin(0), f.konst(-1)}, 9, true);
  f.out("p", wrap);
  const auto before = f.samples();
  const auto report = f.run(Sweep::complement);
  EXPECT_EQ(report.proven, 1u);
  EXPECT_EQ(gu::type_op_of(f.driver("p").get_master_node()), Ntype_op::Not);
  expect_same(before, f.samples());
}

// A lone cell complementing an earlier value is not worth a Not.
TEST(SatoptSweep, LoneComplementCellStays) {
  Fixture f("sweep_lone");
  f.out("o", f.x);
  f.out("p", f.op(Ntype_op::Xor, {f.x, f.konst(255)}, 8));
  EXPECT_EQ(f.run(Sweep::complement).candidates, 0u);
  EXPECT_EQ(f.count(Ntype_op::Xor), 1);
}

// E: (x & 127) + (y & 127) never reaches 256, so its consumers read the low
// eight bits; a second run finds nothing new (the slice reads the value).
TEST(SatoptSweep, UpperZeroRunBecomesANarrowerSlice) {
  Fixture    f("sweep_narrow");
  const auto sum = f.op(Ntype_op::Sum, {f.op(Ntype_op::And, {f.x, f.konst(127)}, 7), f.op(Ntype_op::And, {f.y, f.konst(127)}, 7)}, 9);
  f.out("o", sum);
  f.out("p", f.op(Ntype_op::Xor, {sum, f.x}, 9));
  const auto before = f.samples();
  const auto report = f.run(Sweep::constants);
  // The Sum's top bit, and the top bit of its Xor with x.
  EXPECT_EQ(report.proven, 2u);
  EXPECT_EQ(report.bits, 2u);
  const auto o = f.driver("o");
  ASSERT_EQ(gu::type_op_of(o.get_master_node()), Ntype_op::Get_mask);
  EXPECT_EQ(gu::bits_of(o), 8);
  expect_same(before, f.samples());
  const auto again = f.run(Sweep::constants);
  EXPECT_EQ(again.proven, 0u);
  EXPECT_EQ(again.applied, 0u);
}

// E: a constant low run (x | 1 is always odd) reaches every reader as the
// low-lane form Or(And(t, -2), 1) cprop narrows through; the value stays one
// cell, and a second run finds nothing new.
TEST(SatoptSweep, LowConstantBitBecomesLowLaneForm) {
  for (const bool sign : {false, true}) {
    Fixture    f(sign ? "sweep_low_signed" : "sweep_low", sign);
    const auto odd = f.op(Ntype_op::Or, {f.x, f.konst(1)}, 8, sign);
    const auto sum = f.op(Ntype_op::Sum, {odd, f.y}, 9, sign);
    f.out("o", sum);
    f.out("p", f.op(Ntype_op::Xor, {odd, f.y}, 8, sign));
    const auto before = f.samples();
    EXPECT_EQ(f.run(Sweep::constants).applied, 1u) << sign;
    EXPECT_EQ(f.count(Ntype_op::Concat), 0) << sign;
    EXPECT_EQ(f.driver("o"), sum) << sign;
    for (const auto& e : odd.out_edges()) {
      EXPECT_EQ(gu::type_op_of(e.sink.get_master_node()), Ntype_op::And) << sign;
    }
    expect_same(before, f.samples());
    EXPECT_EQ(f.run(Sweep::constants).applied, 0u) << sign;
  }
}

// G: (x & 0xf0) & (y & 0x0f) is always 0, so neither operand is observed at
// the And: an operand becomes 0 (proven over the window, applied alone). The
// outputs keep their values and a second run finds nothing.
TEST(SatoptSweep, UnobservedOperandBecomesAConstant) {
  Fixture    f("sweep_odc");
  const auto hi  = f.op(Ntype_op::And, {f.x, f.konst(0xf0)}, 8);
  const auto lo  = f.op(Ntype_op::And, {f.y, f.konst(0x0f)}, 8);
  const auto off = f.op(Ntype_op::And, {hi, lo}, 8);
  f.out("o", f.op(Ntype_op::Or, {off, f.y}, 8));
  f.out("p", f.op(Ntype_op::Sum, {f.x, f.y}, 9));
  const auto before = f.samples();
  const auto report = f.run(Sweep::odc);
  EXPECT_GE(report.applied, 1u);
  EXPECT_EQ(report.proven, report.applied);
  expect_same(before, f.samples());
  EXPECT_EQ(f.run(Sweep::odc).applied, 0u);
}

// G: an operand still observed somewhere stays.
TEST(SatoptSweep, ObservedOperandStays) {
  Fixture    f("sweep_odc_observed");
  const auto hi = f.op(Ntype_op::And, {f.x, f.konst(0xf0)}, 8);
  f.out("o", f.op(Ntype_op::Or, {hi, f.y}, 8));
  f.out("p", f.op(Ntype_op::Sum, {f.x, f.y}, 9));
  EXPECT_EQ(f.run(Sweep::odc).applied, 0u);
}

// G, shared profile: a value that is a `unique if` control is observed by the
// obligation even when the Hotmux output does not depend on it.
TEST(SatoptSweep, HotmuxControlIsObserved) {
  Fixture f("sweep_odc_hotmux");
  const auto c0 = f.op(Ntype_op::EQ, {f.x, f.konst(3)}, 1);
  const auto c1 = f.op(Ntype_op::EQ, {f.x, f.konst(3)}, 1);
  auto       h  = gu::create_typed_node(*f.g, Ntype_op::Hotmux);
  c0.connect_sink(h.create_sink_pin(0));
  f.y.connect_sink(h.create_sink_pin(1));
  c1.connect_sink(h.create_sink_pin(2));
  f.y.connect_sink(h.create_sink_pin(3));
  f.y.connect_sink(h.create_sink_pin(4));
  gu::set_ubits(h.create_driver_pin(0), 8);
  f.out("o", h.create_driver_pin(0));  // every arm is y: the controls never matter to o
  f.out("p", f.op(Ntype_op::Sum, {f.x, f.y}, 9));
  for (const auto profile : {livehd::satopt::Profile::shared, livehd::satopt::Profile::synthesis}) {
    // Synthesis too: its AND-OR Hotmux cover needs the controls exclusive,
    // while the window proof reads priority.
    EXPECT_EQ(f.run(Sweep::odc, profile).applied, 0u);
    EXPECT_EQ(h.create_sink_pin(0).get_driver_pin(), c0);
    EXPECT_EQ(h.create_sink_pin(2).get_driver_pin(), c1);
  }
}

namespace {
// Three one-bit inputs a, b, c and a one-bit output o.
struct Bits {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> g;
  hhds::Pin_class              a, b, c;
  explicit Bits(std::string_view name) {
    auto io = lib.create_io(name);
    int  pid = 1;
    for (auto in : {"a", "b", "c"}) {
      io->add_input(in, pid++);
      io->set_bits(in, 1);
      io->set_unsign(in, true);
    }
    io->add_output("o", pid);
    io->set_bits("o", 1);
    g = io->create_graph();
    a = g->get_input_pin("a");
    b = g->get_input_pin("b");
    c = g->get_input_pin("c");
    for (auto p : {a, b, c}) {
      gu::set_ubits(p, 1);
    }
  }
  hhds::Pin_class gate(Ntype_op op, const hhds::Pin_class& x, const hhds::Pin_class& y) {
    auto n = gu::create_typed_node(*g, op);
    x.connect_sink(gu::setup_sink_pid(n, 0));
    y.connect_sink(gu::setup_sink_pid(n, 0));
    auto out = n.create_driver_pin(0);
    gu::set_ubits(out, 1);
    return out;
  }
  livehd::satopt::Stage_report run() {
    livehd::satopt::Stage_report report;
    livehd::satopt::Meter        meter;
    meter.begin_stage(0);
    livehd::satopt::sweep_values({g}, Sweep::resub, livehd::satopt::Profile::shared, {}, report, meter);
    return report;
  }
  int count(Ntype_op kind) const {
    int n = 0;
    for (const auto node : g->body().nodes()) {
      n += gu::type_op_of(node) == kind;
    }
    return n;
  }
};
}  // namespace

// J: (a ^ b) ^ (b ^ c) is a ^ c: one gate replaces the three-cell cone.
TEST(SatoptSweep, ResubstitutionReplacesAConeByOneGate) {
  Bits f("sweep_resub");
  f.gate(Ntype_op::Xor, f.gate(Ntype_op::Xor, f.a, f.b), f.gate(Ntype_op::Xor, f.b, f.c)).connect_sink(f.g->get_output_pin("o"));
  const auto out    = [&] { return f.g->get_output_pin("o").get_driver_pin(); };
  const auto before = livehd::satopt::Satopt_seeds{}.sample(out());
  ASSERT_TRUE(before.has_value());
  const auto report = f.run();
  EXPECT_EQ(report.applied, 1u);
  EXPECT_EQ(f.count(Ntype_op::Xor), 1);
  const auto after = livehd::satopt::Satopt_seeds{}.sample(out());
  ASSERT_TRUE(after.has_value());
  for (size_t i = 0; i < before->size(); ++i) {
    EXPECT_TRUE((*before)[i].is_known_eq((*after)[i])) << i;
  }
  EXPECT_EQ(f.run().applied, 0u);
}

// J: no single gate over two divisors matches a majority: nothing changes.
TEST(SatoptSweep, ResubstitutionLeavesAnUnmatchedConeAlone) {
  Bits       f("sweep_resub_majority");
  const auto ab = f.gate(Ntype_op::And, f.a, f.b);
  const auto bc = f.gate(Ntype_op::And, f.b, f.c);
  const auto ac = f.gate(Ntype_op::And, f.a, f.c);
  f.gate(Ntype_op::Or, f.gate(Ntype_op::Or, ab, bc), ac).connect_sink(f.g->get_output_pin("o"));
  EXPECT_EQ(f.run().applied, 0u);
  EXPECT_EQ(f.count(Ntype_op::And), 3);
}

TEST(SatoptSweep, ResubstitutionStopsFilteringWhenWorkRunsOut) {
  Bits       f("sweep_resub_budget");
  const auto ab = f.gate(Ntype_op::And, f.a, f.b);
  const auto bc = f.gate(Ntype_op::And, f.b, f.c);
  const auto ac = f.gate(Ntype_op::And, f.a, f.c);
  f.gate(Ntype_op::Or, f.gate(Ntype_op::Or, ab, bc), ac).connect_sink(f.g->get_output_pin("o"));
  livehd::satopt::Budget budget;
  budget.work = 600;
  livehd::satopt::Meter        meter(budget);
  livehd::satopt::Stage_report report;
  livehd::satopt::sweep_values({f.g}, Sweep::resub, livehd::satopt::Profile::shared, {}, report, meter);
  EXPECT_TRUE(meter.exhausted());
  EXPECT_GT(report.budget_skips, 0u);
  EXPECT_LE(meter.work_done(), budget.work + budget.samples);
  EXPECT_EQ(report.applied, 0u);
}
