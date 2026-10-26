// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_select.hpp"

#include <functional>

#include "gtest/gtest.h"

namespace livehd::domplace {
namespace {
using usyn::Gate_formula;
using usyn::Xsignal;
using Kind = Gate_formula::Kind;
using Node = Gate_formula::Node;

Node         lit(uint32_t variable, bool negative = false) { return {Kind::literal, 0, 0, variable, negative}; }
Node         series(uint32_t left, uint32_t right) { return {Kind::series, left, right}; }
Node         parallel(uint32_t left, uint32_t right) { return {Kind::parallel, left, right}; }
Gate_formula formula(std::vector<Node> nodes, bool output_inverted = false) {
  Gate_formula f;
  f.nodes           = std::move(nodes);
  f.output_inverted = output_inverted;
  return f;
}

// A frozen one-phase region built the way freeze_endpoint_netlist leaves one:
// every state is a clk-clocked register whose DominoLatch formula, expanded
// with land/lor over its inputs, is the canonical reference D.
struct Builder {
  usyn::Stateful_region    region;
  std::vector<std::string> names;
  Xsignal                  clk;

  Builder() {
    region.frozen.emplace();
    net().clock_phases = 1;
    clk                = input("clk");
    net().outputs.push_back({"clk_control", clk});
  }
  usyn::Endpoint_netlist& net() { return *region.frozen; }
  Xsignal                 input(const std::string& name) {
    const auto signal = net().native.input(name + "_b0");
    net().inputs.push_back(signal);
    names.push_back(name);
    return signal;
  }
  // A register row without its next state yet; returns its Q.
  Xsignal state(const std::string& name) {
    const auto index = static_cast<uint32_t>(net().state.size());
    const auto q     = net().native.input(name);
    net().state.push_back({name, 'x', q, q, usyn::unknown_clock_domain, {}});
    synth::Source_state s;
    s.name          = name;
    s.role          = synth::State_role::register_candidate;
    s.bits          = 1;
    s.stages        = 1;
    s.clock.present = true;
    s.clock.bits    = 1;
    region.source.sources.push_back(s);
    region.source.bits.push_back({index, 0, 0, index, name, {}, {}});
    region.source.controls.push_back({index, synth::State_control_kind::clock, {0}});
    region.state_bits.push_back(index);
    return q;
  }
  // D of state `index` = f(inputs), stored as its DominoLatch.
  Xsignal next(uint32_t index, const Gate_formula& f, const std::vector<Xsignal>& inputs) {
    std::vector<Xsignal> values;
    for (const auto& node : f.nodes) {
      switch (node.kind) {
        case Kind::constant: values.push_back(net().native.constant(node.inverted)); break;
        case Kind::literal : values.push_back(node.inverted ? ~inputs[node.variable] : inputs[node.variable]); break;
        case Kind::series  : values.push_back(net().native.land(values[node.left], values[node.right])); break;
        case Kind::parallel: values.push_back(net().native.lor(values[node.left], values[node.right])); break;
      }
    }
    const auto        d = f.output_inverted ? ~values.back() : values.back();
    usyn::Frozen_cell cell;
    cell.kind    = usyn::Endpoint_kind::DominoLatch;
    cell.state   = index;
    cell.name    = net().state[index].name;
    cell.formula = f;
    cell.metrics = *f.metrics();
    for (const auto input : inputs) {
      cell.inputs.push_back({usyn::Endpoint_ref::Space::native, input.id, input.inverted});
    }
    for (const auto rail : usyn::gate_rails(cell.metrics, static_cast<uint32_t>(inputs.size()))) {
      auto producer      = cell.inputs[rail.input];
      producer.inverted ^= rail.negative;
      cell.rails.push_back({rail.input, producer});
    }
    usyn::Budget work{1000000};
    cell.function                   = *f.evaluate_table(static_cast<uint32_t>(inputs.size()), work);
    net().state[index].reference_d  = d;
    net().state[index].domino_latch = static_cast<uint32_t>(net().cells.size());
    net().cells.push_back(std::move(cell));
    return d;
  }
};

usyn::Native_cost_model cmos_model() {
  usyn::Budget work{10000000};
  auto         model = usyn::read_native_cost_text_model(R"LIB(library(t) {
    cell(INV) {area:1; pin(A) {direction:input;} pin(Y) {direction:output; function:"!A";} }
    cell(NAND) {area:2; pin(A) {direction:input;} pin(B) {direction:input;} pin(Y) {direction:output; function:"!(A B)";} }
    cell(NOR) {area:2; pin(A) {direction:input;} pin(B) {direction:input;} pin(Y) {direction:output; function:"!(A+B)";} }
  })LIB",
                                                 work);
  EXPECT_TRUE(model);
  return *model;
}

Domino_reference reference() {
  Domino_reference r;
  r.inverter.name = "INV";
  r.inverter.area = 1;
  r.registers.push_back({.name = "DFF", .area = 10});
  r.aux_nonregister = 4;
  return r;
}

struct Run {
  std::optional<usyn::Physical_emission> emission;
  std::vector<std::string>               reasons() const {
    std::vector<std::string> result;
    for (const auto& s : emission->states) {
      result.push_back(s.reason);
    }
    return result;
  }
};
Run select(const Builder& b, Select_options options, double cmos_register_area = 10, uint64_t work = 100000000) {
  static const auto model = cmos_model();
  static const auto ref   = reference();
  options.clock           = "clk";
  usyn::Budget budget{work};
  return {select_physical(b.region, b.names, options, {&ref, &model, cmos_register_area}, budget)};
}
Select_options all() {
  Select_options options;
  options.policy = Select_options::Policy::all;
  return options;
}
}  // namespace

TEST(PhysicalSelect, AndOrEndpointConvertsWithRecheckedMetrics) {
  Builder    b;
  const auto a = b.input("a"), c = b.input("c"), d = b.input("d"), e = b.input("e");
  b.state("q");
  // (a & c) | (d & e): four devices, stack 2, two branches.
  b.next(0, formula({lit(0), lit(1), series(0, 1), lit(2), lit(3), series(3, 4), parallel(2, 5)}), {a, c, d, e});
  const auto run = select(b, all());
  ASSERT_TRUE(run.emission);
  ASSERT_EQ(run.emission->states.size(), 1U);
  const auto& s = run.emission->states[0];
  EXPECT_EQ(s.decision, usyn::Physical_decision::domino);
  EXPECT_EQ(s.reason, "domino-all");
  ASSERT_TRUE(s.formula);
  EXPECT_EQ(s.formula->nodes.size(), 7U);  // the frozen formula is reused as is
  EXPECT_EQ(s.metrics.transistors, 4U);
  EXPECT_EQ(s.metrics.stack, 2U);
  EXPECT_EQ(s.a_domino, (4 + 4) / 2.0 * 1 + 10);
  EXPECT_EQ(s.a_rail_not, 0.0);
  EXPECT_FALSE(s.a_cmos);  // not priced under select=all
  ASSERT_EQ(s.rails.size(), 4U);
  EXPECT_EQ(s.rails[2].producer, d);
  ASSERT_EQ(run.emission->cells.size(), 1U);
  EXPECT_TRUE(run.emission->cells[0].name.starts_with("__lhd_domino_"));
  EXPECT_TRUE(run.emission->cells[0].name.ends_with("_v0_t0"));
  EXPECT_EQ(run.emission->cells[0].rails.size(), 4U);

  // A corrupted frozen table is refused, never trusted.
  auto broken = b;
  broken.net().cells[0].function.set(0, true);
  EXPECT_FALSE(select(broken, all()).emission);
}

TEST(PhysicalSelect, OutputInvertedFormulaIsResynthesizedPositive) {
  Builder    b;
  const auto a = b.input("a"), c = b.input("c");
  b.state("q");
  // a & c through the logical !Q port: parallel(!a, !c) with output_inverted.
  b.next(0, formula({lit(0, true), lit(1, true), parallel(0, 1)}, true), {a, c});
  const auto run = select(b, all());
  ASSERT_TRUE(run.emission);
  const auto& s = run.emission->states[0];
  EXPECT_EQ(s.reason, "domino-all");
  ASSERT_TRUE(s.formula);
  EXPECT_FALSE(s.formula->output_inverted);
  usyn::Budget work{1000};
  EXPECT_EQ(*s.formula->evaluate_table(2, work), b.net().cells[0].function);
  EXPECT_EQ(s.metrics.stack, 2U);
  ASSERT_EQ(s.rails.size(), 2U);
  EXPECT_FALSE(s.rails[0].negative);
  EXPECT_FALSE(s.rails[1].negative);
}

TEST(PhysicalSelect, And5IsPolarityIllegal) {
  Builder              b;
  std::vector<Xsignal> inputs;
  for (const auto* name : {"a", "c", "d", "e", "f"}) {
    inputs.push_back(b.input(name));
  }
  b.state("q");
  b.next(0,
         formula({lit(0, true),
                  lit(1, true),
                  parallel(0, 1),
                  lit(2, true),
                  parallel(2, 3),
                  lit(3, true),
                  parallel(4, 5),
                  lit(4, true),
                  parallel(6, 7)},
                 true),
         inputs);
  const auto run = select(b, all());
  ASSERT_TRUE(run.emission);
  EXPECT_EQ(run.emission->states[0].reason, "polarity-illegal");
  EXPECT_EQ(run.emission->states[0].decision, usyn::Physical_decision::retained_cmos);
  EXPECT_TRUE(run.emission->cells.empty());
}

TEST(PhysicalSelect, IdentityAndConstantEndpointsAreRetained) {
  Builder    b;
  const auto a = b.input("a"), c = b.input("c");
  b.state("identity");
  b.state("constant");
  b.state("cofactored");
  b.state("merged");
  b.state("contradiction");
  b.next(0, formula({lit(0)}), {a});
  b.next(1,
         formula({
             {Kind::constant, 0, 0, 0, true}
  }),
         {});
  // 1 & c: a constant input cofactors away and leaves the identity of c.
  b.next(2, formula({lit(0), lit(1), series(0, 1)}), {b.net().native.constant(true), c});
  // (a & a) | c: the duplicate producer merges into one variable.
  b.next(3, formula({lit(0), lit(1), series(0, 1), lit(2), parallel(2, 3)}), {a, a, c});
  // a & !a: merging opposite references leaves no support.
  b.next(4, formula({lit(0), lit(1), series(0, 1)}), {a, ~a});
  const auto run = select(b, all());
  ASSERT_TRUE(run.emission);
  EXPECT_EQ(
      run.reasons(),
      (std::vector<std::string>{"identity-endpoint", "constant-endpoint", "identity-endpoint", "domino-all", "constant-endpoint"}));
  const auto& merged = run.emission->states[3];
  ASSERT_EQ(merged.rails.size(), 2U);  // a | c
  EXPECT_EQ(merged.rails[0].producer, a);
  EXPECT_EQ(merged.rails[1].producer, c);
  EXPECT_EQ(merged.metrics.transistors, 2U);

  auto identity     = all();
  identity.identity = true;
  const auto forced = select(b, identity);
  ASSERT_TRUE(forced.emission);
  EXPECT_EQ(forced.reasons()[0], "domino-all");
  EXPECT_EQ(forced.reasons()[1], "constant-endpoint");  // constants never convert
}

TEST(PhysicalSelect, BothPolarityPinsKeepTheirOrderAndComplementsAreCharged) {
  Builder    b;
  const auto x = b.input("x"), y = b.input("y"), en = b.input("en");
  b.state("parity");
  const auto q = b.state("hold");
  // x ^ y, both rails of both inputs.
  b.next(0, formula({lit(0), lit(1, true), series(0, 1), lit(0, true), lit(1), series(3, 4), parallel(2, 5)}), {x, y});
  // en ? x : q, the enable folded into D reads its own Q.
  b.next(1, formula({lit(0), lit(1), series(0, 1), lit(0, true), lit(2), series(3, 4), parallel(2, 5)}), {en, x, q});
  const auto run = select(b, all());
  ASSERT_TRUE(run.emission);
  const auto& parity = run.emission->states[0];
  ASSERT_EQ(parity.rails.size(), 4U);
  EXPECT_EQ(parity.rails[0].producer, x);
  EXPECT_EQ(parity.rails[1].producer, ~x);
  EXPECT_EQ(parity.rails[2].producer, y);
  EXPECT_EQ(parity.rails[3].producer, ~y);
  EXPECT_EQ(parity.a_rail_not, 2.0);
  const auto& hold = run.emission->states[1];
  ASSERT_EQ(hold.rails.size(), 4U);
  EXPECT_EQ(hold.rails[0].producer, en);
  EXPECT_EQ(hold.rails[1].producer, ~en);
  EXPECT_EQ(hold.rails[2].producer, x);
  EXPECT_EQ(hold.rails[3].producer, q);
  EXPECT_EQ(hold.a_rail_not, 1.0);
  EXPECT_EQ(run.emission->cells.size(), 2U);
}

TEST(PhysicalSelect, InvertedConvertedQIsNotFree) {
  Builder    b;
  const auto a = b.input("a"), c = b.input("c");
  const auto q0 = b.state("first");
  b.state("second");
  b.next(0, formula({lit(0), lit(1), series(0, 1)}), {a, c});
  b.next(1, formula({lit(0, true), lit(1), series(0, 1)}), {q0, c});  // !first & c
  const auto run = select(b, all());
  ASSERT_TRUE(run.emission);
  EXPECT_EQ(run.emission->states[0].decision, usyn::Physical_decision::domino);
  const auto& second = run.emission->states[1];
  EXPECT_EQ(second.decision, usyn::Physical_decision::domino);
  ASSERT_EQ(second.rails.size(), 2U);
  EXPECT_EQ(second.rails[0].producer, ~q0);
  EXPECT_EQ(second.a_rail_not, 1.0);  // one A_NOT, although the logical ledger calls !Q free
  // Equal formulas share one master; these two differ in rail polarity.
  EXPECT_EQ(run.emission->cells.size(), 2U);
}

TEST(PhysicalSelect, AreaPolicyComparesBothSidesAndATieRetainsCmos) {
  Builder    b;
  const auto a = b.input("a"), c = b.input("c"), d = b.input("d"), e = b.input("e");
  b.state("shared");
  b.state("cone");
  // a & c, also an output: its CMOS gate stays, so only the register is saved.
  const auto shared = b.next(0, formula({lit(0), lit(1), series(0, 1)}), {a, c});
  b.net().outputs.push_back({"y", shared});
  // (a & c) | (d & e): its OR and the d & e gate are exclusive, a & c is shared.
  b.next(1, formula({lit(0), lit(1), series(0, 1), lit(2), lit(3), series(3, 4), parallel(2, 5)}), {a, c, d, e});
  Select_options area;
  // a_domino(shared) = (2 + 4) / 2 * 1 + 10 = 13: equal to the CMOS register.
  const auto     tie = select(b, area, 13);
  ASSERT_TRUE(tie.emission);
  const auto& s = tie.emission->states[0];
  EXPECT_EQ(s.a_domino, 13.0);
  ASSERT_TRUE(s.a_cmos);
  EXPECT_EQ(*s.a_cmos, 13.0);
  EXPECT_EQ(s.reason, "area");
  EXPECT_EQ(s.decision, usyn::Physical_decision::retained_cmos);
  const auto& cone = tie.emission->states[1];
  ASSERT_TRUE(cone.a_cmos);
  EXPECT_GT(*cone.a_cmos, 13.0);  // the exclusive OR and d & e gates are priced
  EXPECT_LT(*cone.a_cmos, 13.0 + 3 * 2 + 3);
  const auto wins = select(b, area, 13.5);
  ASSERT_TRUE(wins.emission);
  EXPECT_EQ(wins.emission->states[0].reason, "domino-area");
  EXPECT_EQ(wins.emission->states[0].decision, usyn::Physical_decision::domino);
}

TEST(PhysicalSelect, BudgetExhaustionIsDeterministicAndSticky) {
  Builder    b;
  const auto a = b.input("a"), c = b.input("c"), d = b.input("d");
  for (uint32_t i = 0; i < 6; ++i) {
    b.state("q" + std::to_string(i));
  }
  for (uint32_t i = 0; i < 6; ++i) {
    b.next(i, formula({lit(0), lit(1), series(0, 1), lit(2), parallel(2, 3)}, false), {i % 2 ? a : c, d, i % 3 ? a : d});
  }
  Select_options area;
  bool           partial = false;
  for (uint64_t work = 0; work < 3000; work += 50) {
    const auto first = select(b, area, 1000, work), second = select(b, area, 1000, work);
    ASSERT_TRUE(first.emission && second.emission);
    EXPECT_EQ(first.reasons(), second.reasons()) << work;
    bool out = false;
    for (const auto& reason : first.reasons()) {
      if (out) {
        EXPECT_EQ(reason, "selection-budget") << work;
      }
      out |= reason == "selection-budget";
    }
    partial |= out && first.reasons().front() != "selection-budget";
  }
  EXPECT_TRUE(partial);  // some budget converts a prefix and retains the rest
  const auto none = select(b, area, 1000, 0);
  for (const auto& state : none.emission->states) {
    EXPECT_EQ(state.reason, "selection-budget");
    EXPECT_FALSE(state.formula);
  }
}

TEST(PhysicalSelect, TwoPhaseResultsAndPhaseOneCellsAreRefused) {
  Builder    b;
  const auto a = b.input("a"), c = b.input("c");
  b.state("q");
  b.next(0, formula({lit(0), lit(1), series(0, 1)}), {a, c});
  ASSERT_TRUE(select(b, all()).emission);
  auto two_phase               = b;
  two_phase.net().clock_phases = 2;
  EXPECT_FALSE(select(two_phase, all()).emission);
  auto domino                = b;
  domino.net().cells[0].kind = usyn::Endpoint_kind::Domino;
  EXPECT_FALSE(select(domino, all()).emission);
  auto unfrozen = b;
  unfrozen.region.frozen.reset();
  EXPECT_FALSE(select(unfrozen, all()).emission);
  // Eligibility comes first: another clock domain retains with its reason.
  auto other     = b;
  other.names[0] = "other_clk";
  const auto run = select(other, all());
  ASSERT_TRUE(run.emission);
  EXPECT_EQ(run.emission->states[0].reason, "clock-domain");
  EXPECT_FALSE(run.emission->states[0].eligible);
  EXPECT_TRUE(select(b, all()).emission->states[0].eligible);
}

}  // namespace livehd::domplace
