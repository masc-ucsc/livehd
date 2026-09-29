// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lut_cover.hpp"

#include <bit>
#include <functional>
#include <initializer_list>
#include <limits>
#include <span>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
using livehd::synth::Lid;
using livehd::synth::Lnet;

Truth_table table_of(uint32_t inputs, const std::function<bool(uint32_t)>& f) {
  Truth_table t(inputs);
  for (uint32_t x = 0; x < (1U << inputs); ++x) {
    t.set(x, f(x));
  }
  return t;
}
Truth_table and2() { return table_of(2, [](uint32_t x) { return x == 3; }); }
Truth_table xor2() { return table_of(2, [](uint32_t x) { return x == 1 || x == 2; }); }
// A LUT computing `t` over `fanins`.
Lid lut(Lnet& net, std::initializer_list<Lid> fanins, const Truth_table& t) {
  return net.add_lut(std::span<const Lid>(fanins.begin(), fanins.size()), std::span<const uint64_t>(t.words));
}

// Every output of `net` for the source assignment `inputs` (sources in id order).
std::vector<bool> evaluate(const Lnet& net, uint32_t inputs) {
  std::vector<bool> value(net.size());
  uint32_t          source = 0;
  for (Lid i = 0; i < net.size(); ++i) {
    if (net.kind(i) == Lnet::Kind::source) {
      value[i] = ((inputs >> source++) & 1) != 0;
      continue;
    }
    uint32_t x = 0;
    for (uint32_t k = 0; k < net.fanin_count(i); ++k) {
      x |= uint32_t{value[net.fanin(i, k)]} << k;
    }
    value[i] = net.eval(i, x);
  }
  std::vector<bool> out;
  for (auto o : livehd::synth::combinational_outputs(net)) {
    out.push_back(value[o]);
  }
  return out;
}

// The cover as its own network: each LUT reads its leaves (sources or other
// LUT roots) through its table. Returns the outputs for `inputs`.
std::vector<bool> evaluate_cover(const Lnet& source, const Cover_result& cover, uint32_t inputs) {
  std::vector<int> value(source.size(), -1);
  uint32_t         next = 0;
  for (Lid i = 0; i < source.size(); ++i) {
    if (source.kind(i) == Lnet::Kind::source) {
      value[i] = (inputs >> next++) & 1;
    }
  }
  for (const auto& lut : cover.luts) {
    uint32_t x = 0;
    for (size_t k = 0; k < lut.leaves.size(); ++k) {
      EXPECT_GE(value[lut.leaves[k]], 0) << "leaf " << lut.leaves[k] << " of LUT " << lut.root << " is not built yet";
      x |= static_cast<uint32_t>(value[lut.leaves[k]] == 1) << k;
    }
    value[lut.root] = lut.table.get(x) ? 1 : 0;
  }
  std::vector<bool> out;
  for (auto o : livehd::synth::combinational_outputs(source)) {
    EXPECT_GE(value[o], 0) << "output " << o << " is not built";
    out.push_back(value[o] == 1);
  }
  return out;
}

void expect_equivalent(const Lnet& source, const Cover_result& cover, uint32_t sources) {
  const auto coarse = cover_network(source, cover);
  ASSERT_TRUE(coarse.has_value());
  for (uint32_t x = 0; x < (1U << sources); ++x) {
    EXPECT_EQ(evaluate_cover(source, cover, x), evaluate(source, x)) << "inputs " << x;
    EXPECT_EQ(evaluate(*coarse, x), evaluate(source, x)) << "cover network, inputs " << x;
  }
}

// y = a&b&c and z = a&b&d through a shared s = a&b that is not an output.
Lnet shared_and() {
  Lnet       g;
  const auto a = g.add_input("a"), b = g.add_input("b"), c = g.add_input("c"), d = g.add_input("d");
  const auto s = lut(g, {a, b}, and2());
  g.add_output(lut(g, {s, c}, and2()), "y");
  g.add_output(lut(g, {s, d}, and2()), "z");
  return g;
}

Search_options options(uint32_t support) {
  Search_options o;
  o.recipe.support = support;
  return o;
}
}  // namespace

TEST(LutCoverCost, ClassifiesConstantsWiresDominoAndStaticFunctions) {
  const Recipe     recipe{};
  const Cover_cost cost{};
  Budget           budget{std::numeric_limits<uint64_t>::max() / 4};

  const auto constant = function_cost(Truth_table(0, true), recipe, cost, budget);
  EXPECT_TRUE(constant.constant);
  EXPECT_EQ(constant.cost, 0u);
  const auto wire = function_cost(table_of(1, [](uint32_t x) { return x == 0; }), recipe, cost, budget);
  EXPECT_TRUE(wire.alias);  // an inverter is free in dual rail
  EXPECT_EQ(wire.cost, 0u);

  // AND2: two pull-down literals plus the domino overhead; NAND2 costs the
  // same (its output is free in the other polarity).
  const auto a = function_cost(and2(), recipe, cost, budget);
  EXPECT_TRUE(a.domino);
  EXPECT_EQ(a.cost, cost.domino_overhead + 2);
  const auto nand = function_cost(and2().complement(), recipe, cost, budget);
  EXPECT_TRUE(nand.domino);
  EXPECT_EQ(nand.cost, a.cost);
  // Every input comes in both polarities, so XOR2 is one domino gate too.
  const auto x = function_cost(xor2(), recipe, cost, budget);
  EXPECT_TRUE(x.domino);
  EXPECT_EQ(x.cost, cost.domino_overhead + 4);

  // AND3 under a series limit of 2: only its complement (three one-literal
  // products) is a domino gate.
  const auto and3   = table_of(3, [](uint32_t v) { return v == 7; });
  const auto series = function_cost(and3, Recipe{6, 16, 2}, cost, budget);
  EXPECT_TRUE(series.domino);
  EXPECT_TRUE(series.complemented);
  // Wider than one gate's support: a static LUT.
  const auto wide = function_cost(and3, Recipe{2, 16, 4}, cost, budget);
  EXPECT_FALSE(wide.domino);
  EXPECT_EQ(wide.cost, cost.nonunate_penalty * (cost.cmos_factor * wide.form.factored + cost.static_overhead));
  // XOR3 has three-literal products in both polarities: static under series 2.
  const auto xor3 = function_cost(table_of(3, [](uint32_t v) { return std::popcount(v) % 2 == 1; }), Recipe{6, 16, 2}, cost, budget);
  EXPECT_FALSE(xor3.domino);
  EXPECT_GE(xor3.form.factored, 6u);
  EXPECT_EQ(xor3.cost, cost.nonunate_penalty * (cost.cmos_factor * xor3.form.factored + cost.static_overhead));
}

TEST(LutCover, MergesAndReplicatesSharedLogicWhenCheaper) {
  const auto g     = shared_and();
  auto       cover = lut_cover(g, options(3));
  ASSERT_EQ(cover.status, Status::feasible) << cover.reason;
  // Two 3-input gates (8 + 8) beat three 2-input ones (7 * 3): s is computed
  // inside both.
  EXPECT_EQ(cover.domino, 2u);
  EXPECT_EQ(cover.domino_inputs[3], 2u);
  EXPECT_EQ(cover.cost, 16u);
  EXPECT_EQ(cover.replicated_nodes, 1u);
  EXPECT_EQ(cover.outputs_shallow, 2u);
  expect_equivalent(g, cover, 4);

  // At support two every AND is its own gate.
  cover = lut_cover(g, options(2));
  ASSERT_EQ(cover.status, Status::feasible) << cover.reason;
  EXPECT_EQ(cover.domino, 3u);
  EXPECT_EQ(cover.replicated_nodes, 0u);
  expect_equivalent(g, cover, 4);
}

TEST(LutCover, WithoutDuplicationAMultiReaderNodeIsAGateOutput) {
  const auto g        = shared_and();
  auto       o        = options(3);
  o.duplicate         = false;
  const auto no_copy  = lut_cover(g, o);
  ASSERT_EQ(no_copy.status, Status::feasible) << no_copy.reason;
  EXPECT_EQ(no_copy.domino, 3u);
  EXPECT_EQ(no_copy.replicated_nodes, 0u);
  expect_equivalent(g, no_copy, 4);

  // A fanout boundary has the same effect for this node.
  o                   = options(3);
  o.fanout_boundary   = 2;
  const auto boundary = lut_cover(g, o);
  ASSERT_EQ(boundary.status, Status::feasible) << boundary.reason;
  EXPECT_EQ(boundary.domino, 3u);
  EXPECT_EQ(boundary.replicated_nodes, 0u);
}

TEST(LutCover, FunctionsNoDominoGateBuildsBecomeStaticLuts) {
  // One XOR3 node under a series limit of 2: its own cut is the only choice.
  Lnet       g;
  const auto a = g.add_input({}), b = g.add_input({}), c = g.add_input({});
  g.add_output(lut(g, {a, b, c}, table_of(3, [](uint32_t v) { return std::popcount(v) % 2 == 1; })), "y");
  auto o          = options(3);
  o.recipe.series = 2;
  auto cover      = lut_cover(g, o);
  ASSERT_EQ(cover.status, Status::feasible) << cover.reason;
  EXPECT_EQ(cover.domino, 0u);
  EXPECT_EQ(cover.nonunate, 1u);
  EXPECT_EQ(cover.outputs_deep, 1u);  // a static LUT ends every domino chain
  expect_equivalent(g, cover, 3);
  // The same parity through two XOR2 nodes: two domino gates are cheaper.
  Lnet       two;
  const auto x = two.add_input({}), y = two.add_input({}), z = two.add_input({});
  two.add_output(lut(two, {lut(two, {x, y}, xor2()), z}, xor2()), "y");
  cover = lut_cover(two, o);
  ASSERT_EQ(cover.status, Status::feasible) << cover.reason;
  EXPECT_EQ(cover.domino, 2u);
  EXPECT_EQ(cover.nonunate, 0u);
  expect_equivalent(two, cover, 3);
}

TEST(LutCover, ConstantsWiresAndSourcesNeedNoGate) {
  Lnet       g;
  const auto a   = g.add_input({});
  const auto one = g.add_constant(true);
  const auto inv = lut(g, {a}, table_of(1, [](uint32_t x) { return x == 0; }));
  g.add_output(a, "a");
  g.add_output(one, "one");
  g.add_output(inv, "inv");
  const auto cover = lut_cover(g, options(4));
  ASSERT_EQ(cover.status, Status::feasible) << cover.reason;
  EXPECT_EQ(cover.domino + cover.nonunate, 0u);
  EXPECT_EQ(cover.constants, 1u);
  EXPECT_EQ(cover.aliases, 1u);
  EXPECT_EQ(cover.outputs_wire, 3u);
  EXPECT_EQ(cover.cost, 0u);
  expect_equivalent(g, cover, 1);
}

TEST(LutCover, DominoLevelsRequireShallowOutputs) {
  // y = ((((a & b) & c) & d) & e) & f: a chain one 6-input gate collapses.
  Lnet             g;
  std::vector<Lid> in;
  for (int i = 0; i < 6; ++i) {
    in.push_back(g.add_input({}));
  }
  auto v = lut(g, {in[0], in[1]}, and2());
  for (int i = 2; i < 6; ++i) {
    v = lut(g, {v, in[static_cast<size_t>(i)]}, and2());
  }
  g.add_output(v, "y");
  auto cover = lut_cover(g, options(6));
  ASSERT_EQ(cover.status, Status::feasible) << cover.reason;
  EXPECT_EQ(cover.domino, 1u);
  EXPECT_EQ(cover.domino_inputs[6], 1u);
  EXPECT_EQ(cover.outputs_shallow, 1u);
  expect_equivalent(g, cover, 6);

  // At support two the output needs five chained gates: deeper than two levels.
  cover = lut_cover(g, options(2));
  ASSERT_EQ(cover.status, Status::feasible) << cover.reason;
  EXPECT_EQ(cover.domino, 5u);
  EXPECT_EQ(cover.outputs_deep, 1u);
  EXPECT_EQ(cover.luts.back().level, 5u);
  expect_equivalent(g, cover, 6);
}

TEST(LutCover, RefusesUnsupportedOptionsAndStopsOnAdmission) {
  const auto g = shared_and();
  EXPECT_EQ(lut_cover(g, options(9)).status, Status::unsupported);
  auto o      = options(3);
  o.max_nodes = 3;  // shared_and has 8 nodes
  EXPECT_EQ(lut_cover(g, o).status, Status::unsupported);
  o           = options(3);
  o.admission = [] { return false; };
  const auto stopped = lut_cover(g, o);
  EXPECT_EQ(stopped.status, Status::search_exhausted);
  EXPECT_TRUE(stopped.luts.empty());
  Lnet bad;  // a latch whose input was never set
  (void)bad.add_latch("q", 'x');
  EXPECT_EQ(lut_cover(bad, options(3)).status, Status::invalid);
}

// The cover network keeps the boundary (names, CI order, latch inputs) and
// gives every gate its SOP.
TEST(LutCover, CoverNetworkKeepsTheBoundaryAndCarriesSops) {
  Lnet       g;
  const auto latch = g.add_latch("q_%r0_0", '1');
  const auto q     = g.latch(latch).q;
  const auto a     = g.add_input("a_b0");
  const auto b     = g.add_input("b_b0");
  const auto s     = lut(g, {a, b}, and2());
  const auto x     = lut(g, {s, q}, xor2());
  g.add_output(x, "y");
  g.set_latch_input(latch, s);
  const auto cover = lut_cover(g, options(3));
  ASSERT_EQ(cover.status, Status::feasible) << cover.reason;
  const auto net = cover_network(g, cover);
  ASSERT_TRUE(net.has_value());
  ASSERT_EQ(net->inputs().size(), 2u);
  EXPECT_EQ(net->inputs()[0].name, "a_b0");
  ASSERT_EQ(net->latches().size(), 1u);
  EXPECT_EQ(net->latch(0).name, "q_%r0_0");
  EXPECT_EQ(net->latch(0).init, '1');
  ASSERT_EQ(net->outputs().size(), 1u);
  EXPECT_EQ(net->outputs()[0].name, "y");
  // CI order: the inputs, then the latch output, although the latch came first.
  EXPECT_LT(net->inputs()[1].node, net->latch(0).q);
  for (Lid id = 0; id < net->size(); ++id) {
    if (net->kind(id) == Lnet::Kind::lut && net->fanin_count(id) > 1) {
      EXPECT_NE(net->sop(id), nullptr) << id;
    }
  }
  for (uint32_t v = 0; v < 8; ++v) {
    const bool va = v & 1, vb = (v >> 1) & 1, vq = (v >> 2) & 1;
    // sources of `g` in id order: q, a, b; of the cover network: a, b, q.
    EXPECT_EQ(evaluate(*net, va | (vb << 1) | (vq << 2)), evaluate(g, vq | (va << 1) | (vb << 2))) << v;
  }
}
}  // namespace livehd::usyn
