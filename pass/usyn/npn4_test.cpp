// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "npn4.hpp"

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
uint16_t truth_word(const Xag& graph, Xsignal root) {
  std::vector<uint16_t>             words(graph.size());
  constexpr std::array<uint16_t, 4> variables{0xAAAA, 0xCCCC, 0xF0F0, 0xFF00};
  const auto value = [&](Xsignal s) { return static_cast<uint16_t>(words[s.id] ^ (s.inverted ? 65535 : 0)); };
  for (Id id = 1; id < graph.size(); ++id) {
    const auto& node = graph.node(id);
    if (node.kind == Xag::Kind::source) {
      words[id] = variables[node.source_index];
    } else if (node.kind == Xag::Kind::and_gate) {
      words[id] = value(node.inputs[0]) & value(node.inputs[1]);
    } else if (node.kind == Xag::Kind::xor_gate) {
      words[id] = value(node.inputs[0]) ^ value(node.inputs[1]);
    }
  }
  return value(root);
}
}  // namespace

TEST(Npn4, ExhaustiveAllFunctionsAndAllGeneratedClassTransforms) {
  for (uint32_t truth = 0; truth < 65536; ++truth) {
    Xag              graph;
    const std::array inputs{graph.input("a"), graph.input("b"), graph.input("c"), graph.input("d")};
    Budget           work{1000};
    const auto       candidates = npn4_candidates(graph, static_cast<uint16_t>(truth), inputs, work, 100);
    ASSERT_EQ(candidates.status, Status::feasible) << truth;
    ASSERT_FALSE(candidates.signals.empty());
    ASSERT_LE(candidates.signals.size(), 2U);
    for (auto signal : candidates.signals) {
      ASSERT_EQ(truth_word(graph, signal), truth) << truth;
    }
  }
}

TEST(Npn4, OrderedComplementedAndDependentLeavesRetainTheirTotalFunction) {
  for (uint32_t truth = 0; truth < 65536; truth += 127) {
    Xag                     graph;
    const auto              a = graph.input("a"), b = graph.input("b"), c = graph.input("c");
    const std::array        inputs{graph.lxor(a, b), ~a, graph.land(b, c), b};
    std::array<uint16_t, 4> leaf;
    for (uint32_t i = 0; i < 4; ++i) {
      leaf[i] = truth_word(graph, inputs[i]);
    }
    uint16_t expected = 0;
    for (uint32_t x = 0; x < 16; ++x) {
      uint32_t index = 0;
      for (uint32_t i = 0; i < 4; ++i) {
        index |= ((leaf[i] >> x) & 1) << i;
      }
      expected |= ((truth >> index) & 1) << x;
    }
    Budget     work{1000};
    const auto candidates = npn4_candidates(graph, static_cast<uint16_t>(truth), inputs, work, 100);
    ASSERT_EQ(candidates.status, Status::feasible);
    for (auto signal : candidates.signals) {
      ASSERT_EQ(truth_word(graph, signal), expected) << truth;
    }
  }
}

TEST(Npn4, BoundsKeepTheIncumbentAndPublishNoPartialCandidateSet) {
  Xag              graph;
  const std::array inputs{graph.input("a"), graph.input("b"), graph.input("c"), graph.input("d")};
  const auto       incumbent = graph.lxor(inputs[0], inputs[1]);
  Budget           tiny{1};
  const auto       no_work = npn4_candidates(graph, 0xAB35, inputs, tiny);
  EXPECT_EQ(no_work.status, Status::search_exhausted);
  EXPECT_TRUE(no_work.signals.empty());
  Budget     work{1000};
  const auto no_nodes = npn4_candidates(graph, 0xAB35, inputs, work, static_cast<uint32_t>(graph.size()));
  EXPECT_EQ(no_nodes.status, Status::search_exhausted);
  EXPECT_TRUE(no_nodes.signals.empty());
  EXPECT_EQ(truth_word(graph, incumbent), 0x6666);
  auto invalid  = inputs;
  invalid[0].id = 10000;
  EXPECT_EQ(npn4_candidates(graph, 0xAB35, invalid, work).status, Status::invalid);
}
}  // namespace livehd::usyn
