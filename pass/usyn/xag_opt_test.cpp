// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag_opt.hpp"

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
TEST(XagOpt, FactoredAndSharedFunctionsKeepEveryProtectedOutputAndUnusedSource) {
  Xag  graph;
  auto a = graph.input("a"), b = graph.input("b"), c = graph.input("c");
  graph.input("unused");
  auto             root = graph.land(graph.lor(a, b), graph.lor(a, c));
  const std::array outputs{root, ~root, graph.land(root, b)};
  Budget           work{10000000};
  auto             result = optimize_choices(graph, outputs, {}, work);
  ASSERT_EQ(result.status, Status::feasible);
  ASSERT_TRUE(result.network);
  ASSERT_EQ(result.network->outputs.size(), outputs.size());
  EXPECT_EQ(result.network->graph.input_names(), graph.input_names());
  EXPECT_LE(result.report.after.gates, result.report.before.gates);
  const std::array basis{a.id, b.id, c.id};
  const std::array new_basis{1U, 2U, 3U};
  for (size_t i = 0; i < outputs.size(); ++i) {
    auto before = basis_function(graph, outputs[i], basis, {8, 1024}, work);
    auto after  = basis_function(result.network->graph, result.network->outputs[i], new_basis, {8, 1024}, work);
    ASSERT_EQ(before.status, Status::feasible);
    ASSERT_EQ(after.status, Status::feasible);
    EXPECT_EQ(before.table, after.table);
    EXPECT_LE(result.network->graph.node(result.network->outputs[i].id).level, graph.node(outputs[i].id).level);
  }
  EXPECT_GT(result.report.classes, 0U);
  EXPECT_LT(result.report.after.gates, result.report.before.gates);
}
TEST(XagOpt, GeneratedMultiOutputGraphsUseTheOriginalSnapshotForBothExtractions) {
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    Xag                  graph;
    std::vector<Xsignal> signals;
    for (uint32_t i = 0; i < 5; ++i) {
      signals.push_back(graph.input(std::to_string(i)));
    }
    auto       random = seed;
    const auto pick   = [&] {
      random = random * 1664525 + 1013904223;
      return signals[(random >> 8) % signals.size()];
    };
    for (uint32_t i = 0; i < 30; ++i) {
      auto a = pick(), b = pick();
      if (i & 1) {
        a = ~a;
      }
      signals.push_back(i % 3 ? graph.land(a, b) : graph.lxor(a, b));
    }
    const std::array outputs{signals.back(), signals[signals.size() - 4], ~signals[signals.size() - 8]};
    Budget           work{10000000};
    auto             result = optimize_choices(graph, outputs, {}, work);
    ASSERT_EQ(result.status, Status::feasible) << seed;
    ASSERT_TRUE(result.network);
    const std::array basis{1U, 2U, 3U, 4U, 5U};
    for (size_t i = 0; i < outputs.size(); ++i) {
      auto before = basis_function(graph, outputs[i], basis, {8, 1024}, work);
      auto after  = basis_function(result.network->graph, result.network->outputs[i], basis, {8, 1024}, work);
      ASSERT_EQ(before.status, Status::feasible);
      ASSERT_EQ(after.status, Status::feasible);
      EXPECT_EQ(before.table, after.table) << seed << " output=" << i;
    }
  }
}
}  // namespace
}  // namespace livehd::usyn
