// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "residual.hpp"

#include <algorithm>
#include <array>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
std::vector<bool> evaluate(const Xag& g, std::span<const Residual_output> outputs, uint32_t assignment) {
  std::vector<bool> values(g.size(), false);
  const auto        value = [&](Xsignal signal) { return values[signal.id] != signal.inverted; };
  for (Id id = 1; id < g.size(); ++id) {
    const auto& node = g.node(id);
    switch (node.kind) {
      case Xag::Kind::constant: break;
      case Xag::Kind::source  : values[id] = (assignment >> node.source_index) & 1; break;
      case Xag::Kind::and_gate: values[id] = value(node.inputs[0]) && value(node.inputs[1]); break;
      case Xag::Kind::xor_gate: values[id] = value(node.inputs[0]) != value(node.inputs[1]); break;
    }
  }
  std::vector<bool> result;
  for (const auto& output : outputs) {
    result.push_back(value(output.signal));
  }
  return result;
}

uint64_t live_cost(const Xag& g, std::span<const Residual_output> outputs, const Residual_options& o) {
  std::vector<bool> live(g.size(), false);
  for (const auto& output : outputs) {
    live[output.signal.id] = true;
  }
  uint64_t cost = 0;
  for (size_t i = g.size(); i > 0; --i) {
    const auto& node = g.node(static_cast<Id>(i - 1));
    if (!live[i - 1] || node.kind == Xag::Kind::constant || node.kind == Xag::Kind::source) {
      continue;
    }
    cost                    += node.kind == Xag::Kind::and_gate ? o.and_cost : o.xor_cost;
    live[node.inputs[0].id] = live[node.inputs[1].id] = true;
  }
  return cost;
}

void check(const Xag& before, std::span<const Residual_output> outputs, const Residual_result& result,
           const Residual_options& options) {
  ASSERT_EQ(result.status, Status::feasible) << result.reason;
  ASSERT_TRUE(result.network);
  const auto& after = *result.network;
  EXPECT_EQ(before.input_names(), after.graph.input_names());
  ASSERT_EQ(outputs.size(), after.outputs.size());
  ASSERT_EQ(outputs.size(), after.affected.size());
  EXPECT_EQ(result.report.cost_before, live_cost(before, outputs, options));
  EXPECT_EQ(result.report.cost_after, live_cost(after.graph, after.outputs, options));
  EXPECT_EQ(after.estimated_cost, result.report.cost_after);
  EXPECT_LE(result.report.cost_after, result.report.cost_before);
  for (Id id = 0; id < after.graph.size(); ++id) {
    const auto& node = after.graph.node(id);
    if (node.kind == Xag::Kind::and_gate || node.kind == Xag::Kind::xor_gate) {
      EXPECT_LT(node.inputs[0].id, id);
      EXPECT_LT(node.inputs[1].id, id);
    }
  }
  for (size_t i = 0; i < outputs.size(); ++i) {
    EXPECT_EQ(outputs[i].endpoint_input, after.outputs[i].endpoint_input);
    EXPECT_LE(after.graph.node(after.outputs[i].signal.id).level,
              uint64_t{before.node(outputs[i].signal.id).level} + options.depth_slack);
  }
  ASSERT_LE(before.input_names().size(), 8U);
  for (uint32_t x = 0; x < (1U << before.input_names().size()); ++x) {
    ASSERT_EQ(evaluate(before, outputs, x), evaluate(after.graph, after.outputs, x)) << x;
  }
}
}  // namespace

TEST(Residual, RewriteExposesSharedInputFactoring) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto       root = g.land(g.lor(a, b), g.lor(a, c));
  const std::array outputs{
      Residual_output{ root,  true},
      Residual_output{~root, false}
  };
  Residual_options o;
  o.resubstitute = false;
  Budget     work{10000000};
  const auto result = optimize_residual(g, outputs, o, work);
  check(g, outputs, result, o);
  EXPECT_EQ(result.report.cost_before, 6U);
  EXPECT_EQ(result.report.cost_after, 4U);
  EXPECT_GT(result.report.rewrite_wins, 0U);
  ASSERT_TRUE(result.network);
  EXPECT_TRUE(result.network->affected[0]);
  EXPECT_TRUE(result.network->affected[1]);
  EXPECT_EQ(result.network->outputs[0].signal, ~result.network->outputs[1].signal);
}

TEST(Residual, OutsideReadersPreventFalseDeletionCredit) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto       ab = g.lor(a, b), ac = g.lor(a, c), root = g.land(ab, ac);
  const std::array outputs{
      Residual_output{root,  true},
      Residual_output{  ab, false},
      Residual_output{  ac, false}
  };
  Residual_options o;
  o.resubstitute = false;
  Budget     work{10000000};
  const auto result = optimize_residual(g, outputs, o, work);
  check(g, outputs, result, o);
  EXPECT_EQ(result.report.cost_before, 6U);
  EXPECT_EQ(result.report.cost_after, 6U);
  EXPECT_EQ(result.report.rewrite_wins, 0U);
  EXPECT_GT(result.report.cost_rejections, 0U);
}

TEST(Residual, EqualAreaRewriteBalancesAndAndXorChains) {
  for (bool xor_gate : {false, true}) {
    Xag              g;
    const auto       a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
    const auto       operation = [&](Xsignal x, Xsignal y) { return xor_gate ? g.lxor(x, y) : g.land(x, y); };
    const auto       root      = operation(operation(operation(a, b), c), d);
    const std::array outputs{
        Residual_output{root, false}
    };
    Residual_options o;
    o.resubstitute = false;
    Budget     work{10000000};
    const auto result = optimize_residual(g, outputs, o, work);
    check(g, outputs, result, o);
    ASSERT_TRUE(result.network);
    EXPECT_EQ(result.report.cost_before, result.report.cost_after);
    EXPECT_EQ(result.network->graph.node(result.network->outputs[0].signal.id).level, 2U);
    EXPECT_GT(result.report.rewrite_wins, 0U);
  }
}

TEST(Residual, BalancingDoesNotDuplicateProtectedIntermediateLogic) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c"), d = g.input("d");
  const auto       intermediate = g.land(g.land(a, b), c);
  const auto       root         = g.land(intermediate, d);
  const std::array outputs{
      Residual_output{        root, false},
      Residual_output{intermediate,  true}
  };
  Residual_options o;
  o.resubstitute = false;
  Budget     work{10000000};
  const auto result = optimize_residual(g, outputs, o, work);
  check(g, outputs, result, o);
  ASSERT_TRUE(result.network);
  EXPECT_EQ(result.report.cost_before, result.report.cost_after);
  EXPECT_EQ(result.network->graph.node(result.network->outputs[0].signal.id).level, 3U);
}

TEST(Residual, XorHasRealConfigurableCost) {
  Xag              g;
  const auto       s = g.input("s"), a = g.input("a"), b = g.input("b");
  const std::array outputs{
      Residual_output{g.mux(s, a, b), true}
  };
  Residual_options o;
  o.resubstitute = false;
  Budget     work{10000000};
  const auto result = optimize_residual(g, outputs, o, work);
  check(g, outputs, result, o);
  EXPECT_EQ(result.report.cost_before, 10U);
  EXPECT_EQ(result.report.cost_after, 6U);
  o.xor_cost           = 1;
  const auto cheap_xor = optimize_residual(g, outputs, o, work);
  check(g, outputs, cheap_xor, o);
  EXPECT_EQ(cheap_xor.report.cost_before, 4U);
  EXPECT_EQ(cheap_xor.report.cost_after, 4U);
  o.xor_cost = 0;
  EXPECT_EQ(optimize_residual(g, outputs, o, work).status, Status::invalid);
}

TEST(Residual, DepthGuardRejectsCheaperButDeeperMux) {
  Xag              g;
  const auto       s = g.input("s"), a = g.input("a"), b = g.input("b");
  const auto       root = g.lor(g.land(s, a), g.land(~s, b));
  const std::array outputs{
      Residual_output{root, true}
  };
  Residual_options o;
  o.resubstitute = false;
  o.xor_cost     = 1;
  Budget     work{10000000};
  const auto guarded = optimize_residual(g, outputs, o, work);
  check(g, outputs, guarded, o);
  EXPECT_EQ(guarded.report.cost_after, 6U);
  EXPECT_GT(guarded.report.depth_rejections, 0U);
  o.depth_slack      = 1;
  const auto relaxed = optimize_residual(g, outputs, o, work);
  check(g, outputs, relaxed, o);
  EXPECT_EQ(relaxed.report.cost_after, 4U);
}

TEST(Residual, ResubstitutionReusesAnOutsideDivisorWithoutInsertion) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b");
  const auto       shared = g.lxor(a, b);
  const auto       root   = g.lor(g.land(a, ~b), g.land(~a, b));
  const std::array outputs{
      Residual_output{shared,  true},
      Residual_output{  root, false}
  };
  Residual_options o;
  o.rewrite  = false;
  o.inserted = 0;
  Budget     work{10000000};
  const auto result = optimize_residual(g, outputs, o, work);
  check(g, outputs, result, o);
  EXPECT_EQ(result.report.cost_before, 10U);
  EXPECT_EQ(result.report.cost_after, 4U);
  EXPECT_GT(result.report.resub_wins, 0U);
  ASSERT_TRUE(result.network);
  EXPECT_EQ(result.network->outputs[0].signal, result.network->outputs[1].signal);
}

TEST(Residual, TwoInsertedNodesEnableFactoringMissedByOneNodeSearch) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const std::array outputs{
      Residual_output{g.land(g.lor(a, b), g.lor(a, c)), true}
  };
  Residual_options o;
  o.rewrite  = false;
  o.inserted = 1;
  Budget     work{10000000};
  const auto one = optimize_residual(g, outputs, o, work);
  check(g, outputs, one, o);
  EXPECT_EQ(one.report.cost_after, 6U);
  o.inserted     = 2;
  const auto two = optimize_residual(g, outputs, o, work);
  check(g, outputs, two, o);
  EXPECT_EQ(two.report.cost_after, 4U);
}

TEST(Residual, ResubstitutionUsesACommonBasisBeyondFourInputs) {
  Xag  g;
  auto parity = g.input("0");
  for (uint32_t i = 1; i < 5; ++i) {
    parity = g.lxor(parity, g.input(std::to_string(i)));
  }
  const auto       extra = g.input("extra");
  const std::array outputs{
      Residual_output{g.lxor(g.lxor(parity, extra), extra), true}
  };
  Residual_options o;
  o.rewrite  = false;
  o.inserted = 0;
  Budget     work{10000000};
  const auto result = optimize_residual(g, outputs, o, work);
  check(g, outputs, result, o);
  EXPECT_EQ(result.report.cost_before, 24U);
  EXPECT_EQ(result.report.cost_after, 16U);
}

TEST(Residual, NoResidualLogicSkipsSearchAndDropsDeadGates) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("unused");
  g.land(a, b);
  const std::array outputs{
      Residual_output{               a,  true},
      Residual_output{              ~a, false},
      Residual_output{g.constant(true), false}
  };
  Budget     work{10000};
  const auto result = optimize_residual(g, outputs, {}, work);
  check(g, outputs, result, {});
  EXPECT_TRUE(result.report.skipped);
  EXPECT_EQ(result.report.rewrite_windows, 0U);
  EXPECT_EQ(result.report.resub_windows, 0U);
  ASSERT_TRUE(result.network);
  EXPECT_EQ(result.network->graph.size(), 3U);
}

TEST(Residual, WindowAndRebuildRefusalsPreserveACompleteNetwork) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto       root = g.land(g.lor(a, b), g.lor(a, c));
  const std::array outputs{
      Residual_output{root, true}
  };
  for (uint64_t cap : {1U, 2U, 8U, 32U, 128U}) {
    Residual_options o;
    o.window_work = cap;
    Budget     work{1000000};
    const auto result = optimize_residual(g, outputs, o, work);
    check(g, outputs, result, o);
    EXPECT_TRUE(result.report.exhausted);
  }
  Budget     none{0};
  const auto refused = optimize_residual(g, outputs, {}, none);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_FALSE(refused.network);
  Budget denied{10000};
  denied.admission   = [] { return false; };
  const auto stopped = optimize_residual(g, outputs, {}, denied);
  EXPECT_FALSE(stopped.network);
  EXPECT_TRUE(denied.resource_exhausted);
  bool retained_complete = false;
  for (uint64_t cap : {32U, 64U, 128U, 256U, 1024U}) {
    Budget     bounded{cap};
    const auto limited = optimize_residual(g, outputs, {}, bounded);
    if (limited.network) {
      check(g, outputs, limited, {});
      retained_complete |= limited.report.exhausted;
    } else {
      EXPECT_EQ(limited.status, Status::search_exhausted);
    }
  }
  EXPECT_TRUE(retained_complete);
  Residual_options bounded_nodes;
  bounded_nodes.max_nodes = static_cast<uint32_t>(g.size());
  Budget     work{1000000};
  const auto no_growth = optimize_residual(g, outputs, bounded_nodes, work);
  check(g, outputs, no_growth, bounded_nodes);
  EXPECT_TRUE(no_growth.report.exhausted);
  EXPECT_EQ(g.size(), 7U);  // caller's original graph is never changed
}

TEST(Residual, SmallReconvergentMultioutputNetworksPreserveFunctionsAndCost) {
  uint32_t   random = 0x3247521;
  const auto next   = [&] {
    random = random * 1664525U + 1013904223U;
    return random;
  };
  for (uint32_t trial = 0; trial < 20; ++trial) {
    Xag                  g;
    std::vector<Xsignal> signals;
    for (uint32_t i = 0; i < 5; ++i) {
      signals.push_back(g.input(std::to_string(i)));
    }
    for (uint32_t i = 0; i < 20; ++i) {
      auto a = signals[next() % signals.size()], b = signals[next() % signals.size()];
      if (next() & 0x100) {
        a = ~a;
      }
      const auto op = next() % 3;
      signals.push_back(op == 0 ? g.land(a, b) : op == 1 ? g.lxor(a, b) : g.lor(a, b));
    }
    const std::array outputs{
        Residual_output{              signals.back(),  true},
        Residual_output{ signals[signals.size() - 2], false},
        Residual_output{~signals[signals.size() - 3], false}
    };
    Residual_options o;
    o.window_work = 10000;
    o.xor_cost    = 1 + trial % 4;
    o.depth_slack = trial % 2;
    Budget work{1000000};
    SCOPED_TRACE(trial);
    const auto result = optimize_residual(g, outputs, o, work);
    check(g, outputs, result, o);
  }
}
}  // namespace livehd::usyn
