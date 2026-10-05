// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sop_factor.hpp"

#include <array>
#include <random>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
std::vector<uint64_t> evaluate(const Xag& graph, std::span<const Xsignal> outputs, uint32_t base) {
  std::vector<uint64_t> values(graph.size());
  const auto            value = [&](Xsignal signal) { return signal.inverted ? ~values[signal.id] : values[signal.id]; };
  for (Id id = 1; id < graph.size(); ++id) {
    const auto& node = graph.node(id);
    if (node.kind == Xag::Kind::source) {
      for (uint32_t lane = 0; lane < 64; ++lane) {
        values[id] |= uint64_t{((base + lane) >> node.source_index) & 1} << lane;
      }
    } else if (node.kind == Xag::Kind::and_gate) {
      values[id] = value(node.inputs[0]) & value(node.inputs[1]);
    } else if (node.kind == Xag::Kind::xor_gate) {
      values[id] = value(node.inputs[0]) ^ value(node.inputs[1]);
    }
  }
  std::vector<uint64_t> result;
  for (auto output : outputs) {
    result.push_back(value(output));
  }
  return result;
}
void equivalent(const Xag& graph, std::span<const Xsignal> outputs, const Sop_result& result) {
  ASSERT_EQ(result.status, Status::feasible);
  EXPECT_EQ(graph.input_names(), result.graph.input_names());
  ASSERT_EQ(outputs.size(), result.outputs.size());
  ASSERT_LE(graph.input_names().size(), 20U);
  for (uint32_t base = 0; base < (1U << graph.input_names().size()); base += 64) {
    ASSERT_EQ(evaluate(graph, outputs, base), evaluate(result.graph, result.outputs, base)) << base;
  }
}
}  // namespace

TEST(SopFactor, ExhaustiveWideDecodedMuxOnTwentyIndependentInputs) {
  Xag                     graph;
  std::array<Xsignal, 4>  select;
  std::array<Xsignal, 16> data;
  for (uint32_t i = 0; i < 4; ++i) {
    select[i] = graph.input(std::to_string(i));
  }
  for (uint32_t i = 0; i < 16; ++i) {
    data[i] = graph.input(std::to_string(i + 4));
  }
  auto root = graph.constant(false);
  for (uint32_t arm = 0; arm < 16; ++arm) {
    auto product = data[arm];
    for (uint32_t i = 0; i < 4; ++i) {
      product = graph.land(product, (arm & (1U << i)) ? select[i] : ~select[i]);
    }
    root = graph.lor(root, product);
  }
  const std::array outputs{root, ~root, select[1]};
  Budget           work{10000000};
  const auto       result = factor_sop(graph, outputs, work, 10000);
  equivalent(graph, outputs, result);
  EXPECT_GT(result.roots, 0U);
  EXPECT_GT(result.cofactors, 0U);
}

TEST(SopFactor, DependentComputedAtomsSparseTermsAndContradictionsAreExact) {
  Xag              graph;
  const auto       a = graph.input("a"), b = graph.input("b"), c = graph.input("c"), d = graph.input("d");
  const auto       selector = graph.lxor(a, b);
  const auto       root     = graph.lor(graph.land(selector, graph.land(a, c)), graph.land(~selector, graph.land(a, d)));
  const auto       shared   = graph.lor(root, graph.land(a, graph.land(b, ~c)));
  const std::array outputs{root, shared, graph.lxor(root, shared)};
  Budget           work{1000000};
  const auto       result = factor_sop(graph, outputs, work, 10000);
  equivalent(graph, outputs, result);
  EXPECT_GT(result.cofactors, 0U);
}

TEST(SopFactor, ExhaustiveGeneratedProductsPreserveLiteralPolarityAndSharing) {
  std::mt19937 random(5123);
  for (uint32_t trial = 0; trial < 100; ++trial) {
    Xag                  graph;
    std::vector<Xsignal> inputs;
    for (uint32_t i = 0; i < 8; ++i) {
      inputs.push_back(graph.input(std::to_string(i)));
    }
    auto root = graph.constant(false);
    for (uint32_t term = 0; term < 12; ++term) {
      auto product = graph.constant(true);
      for (uint32_t j = 0; j < 5; ++j) {
        auto literal = inputs[random() % inputs.size()];
        if (random() & 1) {
          literal = ~literal;
        }
        product = graph.land(product, literal);
      }
      root = graph.lor(root, product);
    }
    const std::array outputs{root, ~root, inputs[0]};
    Budget           work{1000000}, replay{1000000};
    const auto       first  = factor_sop(graph, outputs, work, 10000);
    const auto       second = factor_sop(graph, outputs, replay, 10000);
    equivalent(graph, outputs, first);
    EXPECT_EQ(first.outputs, second.outputs);
    EXPECT_EQ(first.graph.size(), second.graph.size());
    EXPECT_EQ(work.credit_floor(), replay.credit_floor());
  }
}

TEST(SopFactor, SearchRefusalPublishesNoIncompleteAlternative) {
  Xag              graph;
  const auto       a = graph.input("a"), b = graph.input("b"), c = graph.input("c");
  const std::array outputs{graph.lor(graph.land(a, b), graph.land(~a, c))};
  Budget           tiny{1};
  const auto       stopped = factor_sop(graph, outputs, tiny);
  EXPECT_EQ(stopped.status, Status::search_exhausted);
  EXPECT_TRUE(stopped.outputs.empty());
  EXPECT_EQ(stopped.graph.size(), 1U);
  Budget cancelled{100000};
  cancelled.admission = [] { return false; };
  EXPECT_EQ(factor_sop(graph, outputs, cancelled).status, Status::search_exhausted);
  EXPECT_TRUE(cancelled.resource_exhausted);
  Budget work{100000};
  EXPECT_EQ(factor_sop(graph, std::array<Xsignal, 1>{{{10000, false}}}, work).status, Status::invalid);
}
}  // namespace livehd::usyn
