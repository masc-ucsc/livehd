// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

#include "gtest/gtest.h"

namespace livehd::usyn {
TEST(Xag, StructuralHashingKeepsXorAndCanonicalComplements) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b");
  EXPECT_EQ(g.land(a, b), g.land(b, a));
  EXPECT_EQ(g.lxor(a, b), g.lxor(~a, ~b));
  EXPECT_EQ(g.lxor(a, ~b), ~g.lxor(a, b));
  EXPECT_EQ(g.land(a, ~a), g.constant(false));
  EXPECT_EQ(g.lxor(a, ~a), g.constant(true));
  EXPECT_EQ(g.land(a, g.constant(true)), a);
  EXPECT_EQ(g.mux(b, a, a), a);
  EXPECT_EQ(g.node(g.lxor(a, b).id).kind, Xag::Kind::xor_gate);
  EXPECT_EQ(g.node(a.id).fanouts, 2U);  // one AND, one XOR; no duplicate edges
  EXPECT_THROW(g.land({1000}, a), std::invalid_argument);
}

TEST(Xag, WholeReconvergentConeExposesTheCommonLogicalInput) {
  Xag        g;
  const auto a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto u = g.lor(a, b), v = g.lor(a, c), root = g.land(u, v);
  Budget     work{10000};
  const auto whole = whole_cone(g, root, {}, work);
  ASSERT_EQ(whole.status, Status::feasible);
  EXPECT_TRUE(whole.whole_cone);
  EXPECT_EQ(whole.leaves, (std::vector<Id>{a.id, b.id, c.id}));
  const auto f = window_function(g, whole, work);
  ASSERT_EQ(f.status, Status::feasible);
  for (uint32_t x = 0; x < 8; ++x) {
    EXPECT_EQ(f.table.get(x), (x & 1) || (x & 6) == 6);
  }
  const auto grown = grow_window(g, root, {3, 100}, work);
  ASSERT_EQ(grown.status, Status::feasible);
  EXPECT_EQ(grown.leaves, whole.leaves);
  const std::array<Id, 2> boundary{u.id, v.id};
  const auto              shallow = collect_window(g, root, boundary, {}, work);
  ASSERT_EQ(shallow.status, Status::feasible);
  EXPECT_FALSE(shallow.whole_cone);
  // u and v are complemented AND nodes: their positive XAG nodes are NORs.
  const auto sf = window_function(g, shallow, work);
  ASSERT_EQ(sf.status, Status::feasible);
  EXPECT_EQ(sf.table.words[0], 1U);
}

TEST(Xag, WordSimulationUsesAllSixteenVariablesAndOrderedLeaves) {
  Xag             g;
  std::vector<Id> leaves;
  auto            root = g.constant(false);
  for (uint32_t i = 0; i < 16; ++i) {
    const auto input = g.input(std::to_string(i));
    leaves.push_back(input.id);
    root = g.lxor(root, input);
  }
  Budget     work{100000};
  const auto window = collect_window(g, ~root, leaves, {}, work);
  ASSERT_EQ(window.status, Status::feasible);
  const auto f = window_function(g, window, work);
  ASSERT_EQ(f.status, Status::feasible);
  EXPECT_EQ(f.table.words.size(), 1024U);
  for (uint32_t x = 0; x < 65536; ++x) {
    ASSERT_EQ(f.table.get(x), (std::popcount(x) & 1) == 0) << x;
  }
}

TEST(Xag, PartialExpansionPreservesAsymmetricAndDependentBoundaries) {
  Xag                     g;
  const auto              a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto              u = g.lxor(a, b), root = g.lor(u, g.land(a, c));
  const std::array<Id, 3> leaves{c.id, u.id, a.id};
  Budget                  work{10000};
  const auto              cut = collect_window(g, root, leaves, {}, work);
  ASSERT_EQ(cut.status, Status::feasible);
  const auto f = window_function(g, cut, work);
  ASSERT_EQ(f.status, Status::feasible);
  for (uint32_t x = 0; x < 8; ++x) {
    EXPECT_EQ(f.table.get(x), (x & 2) || (x & 5) == 5);
  }
}

TEST(Xag, LargeWholeConeFallsBackToBoundedGrowthWithoutTables) {
  Xag  g;
  auto root = g.input("first");
  for (uint32_t i = 0; i < 10000; ++i) {
    root = g.land(root, g.input(std::to_string(i)));
  }
  Budget     whole_work{500};
  const auto whole = whole_cone(g, root, {}, whole_work);
  EXPECT_EQ(whole.status, Status::search_exhausted);
  EXPECT_FALSE(whole.whole_cone);
  Budget     growth_work{5000};
  const auto bounded = grow_window(g, root, {8, 100}, growth_work);
  ASSERT_EQ(bounded.status, Status::feasible) << bounded.reason;
  EXPECT_EQ(bounded.leaves.size(), 8U);
  EXPECT_LE(bounded.interior.size(), 100U);
  EXPECT_FALSE(bounded.whole_cone);
  const auto f = window_function(g, bounded, growth_work);
  ASSERT_EQ(f.status, Status::feasible);
  for (uint32_t x = 0; x < 256; ++x) {
    EXPECT_EQ(f.table.get(x), x == 255);
  }
}

TEST(Xag, RejectsInvalidCutsAndPreservesExplicitBudgetFailures) {
  Xag                     g;
  const auto              a = g.input("a"), b = g.input("b"), root = g.lxor(a, b);
  Budget                  work{10000};
  const std::array<Id, 1> missing{a.id};
  EXPECT_EQ(collect_window(g, root, missing, {}, work).status, Status::invalid);
  const std::array<Id, 2> duplicate{a.id, a.id};
  EXPECT_EQ(collect_window(g, root, duplicate, {}, work).status, Status::invalid);
  Budget none{0};
  EXPECT_EQ(whole_cone(g, root, {}, none).status, Status::search_exhausted);
  const auto constant = whole_cone(g, g.constant(true), {}, work);
  const auto f        = window_function(g, constant, work);
  ASSERT_EQ(f.status, Status::feasible);
  EXPECT_EQ(f.table.inputs, 0U);
  EXPECT_EQ(f.table.words[0], 1U);
}
TEST(Xag, DivisorFunctionsKeepCommonBasisOrderAndUnusedInputs) {
  Xag              g;
  const auto       a = g.input("a"), b = g.input("b"), c = g.input("c");
  const auto       ab = g.land(a, b);
  const std::array basis{c.id, ab.id};
  Budget           work{10000};
  const auto       f = basis_function(g, ~ab, basis, {}, work);
  ASSERT_EQ(f.status, Status::feasible);
  ASSERT_EQ(f.table.inputs, 2U);
  for (uint32_t x = 0; x < 4; ++x) {
    EXPECT_EQ(f.table.get(x), !(x & 2));
  }
  const auto sub = collect_subwindow(g, ~ab, basis, {}, work);
  ASSERT_EQ(sub.status, Status::feasible);
  EXPECT_EQ(sub.leaves, (std::vector<Id>{ab.id}));
  EXPECT_EQ(basis_function(g, a, basis, {}, work).status, Status::invalid);
  const std::array duplicate{ab.id, ab.id};
  EXPECT_EQ(basis_function(g, ab, duplicate, {}, work).status, Status::invalid);
  Budget none{0};
  EXPECT_EQ(basis_function(g, ab, basis, {}, none).status, Status::search_exhausted);
  EXPECT_EQ(basis_function(g, g.constant(true), basis, {}, work).table, Truth_table(2, true));
  const auto combined = basis_function(g, g.lxor(ab, c), basis, {}, work);
  ASSERT_EQ(combined.status, Status::feasible);
  EXPECT_EQ(combined.table.words[0], 6U);
}
TEST(Xag, PriorityWindowsRetainGreedyAndShallowComputedBasis) {
  Xag        graph;
  const auto a = graph.input("a"), b = graph.input("b"), c = graph.input("c");
  const auto ab = graph.land(a, b), ac = graph.land(a, c), root = graph.lxor(ab, ac);
  Budget     work{100000}, replay{100000};
  const auto cuts     = priority_windows(graph, root, {4, 100}, work, 8);
  const auto repeated = priority_windows(graph, root, {4, 100}, replay, 8);
  ASSERT_EQ(cuts.status, Status::feasible);
  ASSERT_FALSE(cuts.windows.empty());
  EXPECT_LE(cuts.windows.size(), 8U);
  ASSERT_EQ(cuts.windows.size(), repeated.windows.size());
  EXPECT_EQ(work.credit_floor(), replay.credit_floor());
  bool shallow = false, whole = false;
  for (size_t i = 0; i < cuts.windows.size(); ++i) {
    const auto& cut = cuts.windows[i];
    EXPECT_EQ(cut.leaves, repeated.windows[i].leaves);
    const auto function = window_function(graph, cut, work);
    ASSERT_EQ(function.status, Status::feasible);
    if (cut.leaves == std::vector<Id>{ab.id, ac.id}) {
      shallow = true;
      EXPECT_EQ(function.table.words[0], 6U);
    }
    if (cut.leaves == std::vector<Id>{a.id, b.id, c.id}) {
      whole = true;
      EXPECT_EQ(function.table.words[0], 0x28U);
    }
    EXPECT_FALSE(std::binary_search(cut.leaves.begin(), cut.leaves.end(), root.id));
  }
  EXPECT_TRUE(shallow);
  EXPECT_TRUE(whole);
  Budget none{0};
  EXPECT_EQ(priority_windows(graph, root, {4, 100}, none).status, Status::search_exhausted);
  EXPECT_EQ(priority_windows(graph, root, {4, 100}, work, 0).status, Status::invalid);
}
}  // namespace livehd::usyn
