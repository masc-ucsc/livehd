// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag_representations.hpp"

#include <algorithm>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
TEST(XagRepresentations, AllThreeInputFunctionsHaveExactTotalCandidates) {
  for (uint32_t bits = 0; bits < 256; ++bits) {
    Xag              graph;
    const std::array inputs{graph.input("a"), graph.input("b"), graph.input("c")};
    const std::array basis{inputs[0].id, inputs[1].id, inputs[2].id};
    Truth_table      function(3);
    function.words[0] = bits;
    Budget work{1000000};
    auto   result = represent_function(graph, function, inputs, work);
    ASSERT_EQ(result.status, Status::feasible) << bits;
    ASSERT_FALSE(result.candidates.empty()) << bits;
    EXPECT_LE(result.bdd_nodes, 128U);
    for (const auto& candidate : result.candidates) {
      auto checked = basis_function(graph, candidate.signal, basis, {8, 1024}, work);
      ASSERT_EQ(checked.status, Status::feasible);
      EXPECT_EQ(checked.table, function) << bits << " " << candidate.representation;
    }
  }
}
TEST(XagRepresentations, DependentComputedLeavesStillUseAnIndependentBasis) {
  Xag              graph;
  auto             a = graph.input("a"), b = graph.input("b"), parity = graph.lxor(a, b);
  const std::array inputs{a, parity};
  const std::array basis{a.id, parity.id};
  Truth_table      function(2);
  function.words[0] = 6;
  Budget work{1000000};
  auto   result = represent_function(graph, function, inputs, work);
  ASSERT_EQ(result.status, Status::feasible);
  ASSERT_FALSE(result.candidates.empty());
  for (const auto& candidate : result.candidates) {
    auto checked = basis_function(graph, candidate.signal, basis, {8, 1024}, work);
    ASSERT_EQ(checked.status, Status::feasible);
    EXPECT_EQ(checked.table, function);
  }
  const std::array aliases{a, a};
  EXPECT_EQ(represent_function(graph, function, aliases, work).status, Status::invalid);
}
TEST(XagRepresentations, DisjointParityBlocksAreFactoredOnTheirFullIndependentBasis) {
  Xag              graph;
  const std::array inputs{graph.input("a"), graph.input("b"), graph.input("c"), graph.input("d")};
  const std::array basis{inputs[0].id, inputs[1].id, inputs[2].id, inputs[3].id};
  Truth_table      function(4);
  for (uint32_t x = 0; x < 16; ++x) {
    function.set(x, ((x & 1) != ((x >> 1) & 1)) && (((x >> 2) & 1) != ((x >> 3) & 1)));
  }
  Budget work{1000000};
  auto   result = represent_function(graph, function, inputs, work);
  ASSERT_EQ(result.status, Status::feasible);
  EXPECT_GT(result.dsd_blocks, 0U);
  const auto found = std::find_if(result.candidates.begin(), result.candidates.end(), [](const auto& candidate) {
    return candidate.representation == "dsd";
  });
  ASSERT_NE(found, result.candidates.end());
  auto checked = basis_function(graph, found->signal, basis, {8, 1024}, work);
  ASSERT_EQ(checked.status, Status::feasible);
  EXPECT_EQ(checked.table, function);
  EXPECT_LE(graph.node(found->signal.id).level, 3U);
}
TEST(XagRepresentations, ScratchLimitsAndCancellationDoNotAuthorizePartialCandidates) {
  Xag              graph;
  const std::array inputs{graph.input("a"), graph.input("b"), graph.input("c")};
  Truth_table      function(3);
  function.words[0] = 0x96;
  Budget work{0};
  auto   refused = represent_function(graph, function, inputs, work);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_TRUE(refused.candidates.empty());
  Budget cancelled{1000000};
  cancelled.admission          = [] { return false; };
  cancelled.admission_interval = 1;
  auto stopped                 = represent_function(graph, function, inputs, cancelled);
  EXPECT_EQ(stopped.status, Status::search_exhausted);
  EXPECT_TRUE(stopped.candidates.empty());
  EXPECT_TRUE(cancelled.resource_exhausted);
}
TEST(XagRepresentations, HashingAgainstComputedLeavesRejectsImageOnlyIdentitiesButKeepsTotalSop) {
  Xag              graph;
  const auto       a = graph.input("a"), b = graph.input("b"), p = graph.lxor(a, b);
  const std::array inputs{a, b, p};
  const std::array basis{a.id, b.id, p.id};
  Truth_table      function(3);
  for (uint32_t x = 0; x < 8; ++x) {
    function.set(x, ((x & 1) != ((x >> 1) & 1)) && ((x >> 2) & 1));
  }
  Budget work{1000000};
  auto   result = represent_function(graph, function, inputs, work);
  ASSERT_EQ(result.status, Status::feasible);
  EXPECT_GT(result.rejected, 0U);
  ASSERT_FALSE(result.candidates.empty());
  for (const auto& candidate : result.candidates) {
    auto checked = basis_function(graph, candidate.signal, basis, {8, 1024}, work);
    ASSERT_EQ(checked.status, Status::feasible);
    EXPECT_EQ(checked.table, function);
    EXPECT_NE(candidate.signal, p);
  }
}
}  // namespace
}  // namespace livehd::usyn
