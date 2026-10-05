// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag_sweep.hpp"

#include <array>
#include <bit>
#include <random>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
std::vector<bool> evaluate(const Xag& graph, std::span<const Xsignal> outputs, uint32_t assignment) {
  std::vector<bool> values(graph.size());
  const auto        value = [&](Xsignal s) { return values[s.id] != s.inverted; };
  for (Id id = 1; id < graph.size(); ++id) {
    const auto& n = graph.node(id);
    if (n.kind == Xag::Kind::source) {
      values[id] = (assignment >> n.source_index) & 1;
    } else if (n.kind == Xag::Kind::and_gate) {
      values[id] = value(n.inputs[0]) && value(n.inputs[1]);
    } else if (n.kind == Xag::Kind::xor_gate) {
      values[id] = value(n.inputs[0]) != value(n.inputs[1]);
    }
  }
  std::vector<bool> result;
  for (auto output : outputs) {
    result.push_back(value(output));
  }
  return result;
}
void equivalent(const Xag& graph, std::span<const Xsignal> outputs, const Sweep_result& result) {
  ASSERT_EQ(result.status, Status::feasible);
  ASSERT_EQ(graph.input_names(), result.graph.input_names());
  ASSERT_EQ(outputs.size(), result.outputs.size());
  ASSERT_LE(graph.input_names().size(), 8U);
  for (uint32_t x = 0; x < (1U << graph.input_names().size()); ++x) {
    ASSERT_EQ(evaluate(graph, outputs, x), evaluate(result.graph, result.outputs, x)) << x;
  }
}
}  // namespace

TEST(XagSweep, DistributedFormsComputedLeavesAndComplementShareExactly) {
  Xag              graph;
  const auto       a = graph.input("a"), b = graph.input("b"), c = graph.input("c");
  const auto       u            = graph.lxor(a, b);
  const auto       factored     = graph.land(u, graph.lor(b, c));
  const auto       distributed  = graph.lor(graph.land(u, b), graph.land(u, c));
  const auto       complemented = graph.land(~graph.land(u, b), ~graph.land(u, c));
  const std::array outputs{factored, distributed, complemented};
  Budget           work{1000000};
  const auto       result = sweep_xag(graph, outputs, work);
  equivalent(graph, outputs, result);
  EXPECT_GT(result.confirmations, 0U);
  EXPECT_EQ(result.outputs[0], result.outputs[1]);
  EXPECT_EQ(result.outputs[0], ~result.outputs[2]);
}

TEST(XagSweep, SupportsDoNotAliasAndUnusedInputsAreRemovedFromTheKey) {
  Xag              graph;
  const auto       a = graph.input("a"), b = graph.input("b"), c = graph.input("c"), d = graph.input("unused");
  const auto       ab = graph.land(a, b), ac = graph.land(a, c);
  const auto       redundant = graph.lor(ab, graph.land(a, ~b));
  const std::array outputs{ab, ac, redundant, d};
  Budget           work{1000000};
  const auto       result = sweep_xag(graph, outputs, work);
  equivalent(graph, outputs, result);
  EXPECT_NE(result.outputs[0], result.outputs[1]);
  EXPECT_EQ(result.graph.node(result.outputs[2].id).kind, Xag::Kind::source);
  EXPECT_EQ(result.graph.node(result.outputs[2].id).source_index, 0U);
}
TEST(XagSweep, SixteenInputCompleteTablesConfirmParityAndRespectStorageBounds) {
  Xag                     graph;
  std::array<Xsignal, 16> inputs;
  for (unsigned i = 0; i < inputs.size(); ++i) {
    inputs[i] = graph.input(std::to_string(i));
  }
  auto left = inputs.front(), right = inputs.back();
  for (unsigned i = 1; i < inputs.size(); ++i) {
    left = graph.lxor(left, inputs[i]);
  }
  for (unsigned i = 1; i < inputs.size(); ++i) {
    right = graph.lxor(right, inputs[inputs.size() - i - 1]);
  }
  const std::array outputs{left, ~right};
  Budget           work{500000000};
  auto             result = sweep_xag(graph, outputs, work, 2000000, 16);
  ASSERT_EQ(result.status, Status::feasible);
  EXPECT_GT(result.confirmations, 0U);
  EXPECT_EQ(result.outputs[0], ~result.outputs[1]);
  EXPECT_LE(result.table_words, 65536U);
  for (uint32_t x = 0; x < 65536; ++x) {
    ASSERT_EQ(evaluate(graph, outputs, x), evaluate(result.graph, result.outputs, x));
  }
  Budget tiny{500000000};
  auto   bounded = sweep_xag(graph, outputs, tiny, 2000000, 16, 2);
  ASSERT_EQ(bounded.status, Status::feasible);
  EXPECT_TRUE(bounded.limited);
  EXPECT_LE(bounded.table_words, 2U);
  for (uint32_t x = 0; x < 65536; ++x) {
    ASSERT_EQ(evaluate(graph, outputs, x), evaluate(bounded.graph, bounded.outputs, x));
  }
}

TEST(XagSweep, AllThreeInputTruthTablesHaveIdenticalSopAndPosOutputs) {
  for (uint32_t truth = 0; truth < 256; ++truth) {
    Xag              graph;
    const std::array inputs{graph.input("a"), graph.input("b"), graph.input("c")};
    auto             sop = graph.constant(false), complement = graph.constant(false);
    for (uint32_t x = 0; x < 8; ++x) {
      auto cube = graph.constant(true);
      for (uint32_t i = 0; i < 3; ++i) {
        cube = graph.land(cube, (x & (1U << i)) ? inputs[i] : ~inputs[i]);
      }
      if (truth & (1U << x)) {
        sop = graph.lor(sop, cube);
      } else {
        complement = graph.lor(complement, cube);
      }
    }
    const std::array outputs{sop, ~complement};
    Budget           work{1000000};
    const auto       result = sweep_xag(graph, outputs, work);
    equivalent(graph, outputs, result);
    EXPECT_EQ(result.outputs[0], result.outputs[1]) << truth;
  }
}

TEST(XagSweep, ExhaustiveGeneratedReconvergentGraphsAndSupportFallback) {
  std::mt19937 random(3141);
  for (uint32_t trial = 0; trial < 100; ++trial) {
    Xag                  graph;
    std::vector<Xsignal> signals;
    for (uint32_t i = 0; i < 8; ++i) {
      signals.push_back(graph.input(std::to_string(i)));
    }
    for (uint32_t i = 0; i < 40; ++i) {
      auto a = signals[random() % signals.size()], b = signals[random() % signals.size()];
      if (random() & 1) {
        a = ~a;
      }
      if (random() & 1) {
        b = ~b;
      }
      signals.push_back((random() & 1) ? graph.land(a, b) : graph.lxor(a, b));
    }
    const std::array outputs{signals.back(), signals[signals.size() - 2], ~signals[20]};
    Budget           work{1000000}, replay{1000000};
    const auto       first  = sweep_xag(graph, outputs, work, 10000, 3);
    const auto       second = sweep_xag(graph, outputs, replay, 10000, 3);
    equivalent(graph, outputs, first);
    equivalent(graph, outputs, second);
    EXPECT_EQ(work.credit_floor(), replay.credit_floor());
    EXPECT_EQ(first.outputs, second.outputs);
    EXPECT_EQ(first.graph.size(), second.graph.size());
  }
}

TEST(XagSweep, ExhaustionCancellationAndInvalidLimitsPublishNoPartialGraph) {
  Xag              graph;
  const auto       a = graph.input("a"), b = graph.input("b"), c = graph.input("c");
  const std::array outputs{graph.land(graph.lor(a, b), c)};
  Budget           tiny{10};
  const auto       partial = sweep_xag(graph, outputs, tiny);
  EXPECT_EQ(partial.status, Status::search_exhausted);
  EXPECT_EQ(partial.graph.size(), 1U);
  EXPECT_TRUE(partial.outputs.empty());
  Budget work{100000};
  EXPECT_EQ(sweep_xag(graph, outputs, work, 100, 17).status, Status::invalid);
  Budget cancelled{100000};
  cancelled.admission = [] { return false; };
  const auto refused  = sweep_xag(graph, outputs, cancelled);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_TRUE(cancelled.resource_exhausted);
  EXPECT_TRUE(refused.outputs.empty());
}
}  // namespace livehd::usyn
