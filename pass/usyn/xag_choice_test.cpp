// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag_choice.hpp"

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
bool evaluate(const Xag& graph, Xsignal output, uint32_t pattern) {
  std::vector<bool> values(graph.size());
  const auto        read = [&](Xsignal s) { return values[s.id] != s.inverted; };
  for (Id id = 1; id < graph.size(); ++id) {
    const auto& n = graph.node(id);
    if (n.kind == Xag::Kind::source) {
      values[id] = (pattern >> n.source_index) & 1;
    }
    if (n.kind == Xag::Kind::and_gate) {
      values[id] = read(n.inputs[0]) && read(n.inputs[1]);
    }
    if (n.kind == Xag::Kind::xor_gate) {
      values[id] = read(n.inputs[0]) != read(n.inputs[1]);
    }
  }
  return read(output);
}
TEST(XagChoice, TotalBasisPhaseAndOwnershipSurviveSharedOutputExtraction) {
  Xag  graph;
  auto a = graph.input("a"), b = graph.input("b"), c = graph.input("c");
  graph.input("unused");
  const auto       root        = graph.land(graph.lor(a, b), graph.lor(a, c));
  const auto       alternative = graph.lor(a, graph.land(b, c));
  const std::array basis{a.id, b.id, c.id};
  Budget           work{1000000};
  auto             choice = make_choice(graph, root, basis, work);
  ASSERT_TRUE(choice);
  ASSERT_EQ(retain_choice(graph, *choice, alternative, work), Status::feasible);
  EXPECT_EQ(retain_choice(graph, *choice, ~alternative, work), Status::invalid);
  EXPECT_EQ(retain_choice(graph, *choice, alternative, work), Status::feasible);
  ASSERT_EQ(choice->members().size(), 2U);
  const std::array outputs{root, ~root, graph.land(root, b)};
  const std::array choices{*choice};
  const std::array selected{1U};
  auto             result = extract_choices(graph, choices, selected, outputs, work);
  ASSERT_EQ(result.status, Status::feasible);
  EXPECT_EQ(result.graph.input_names(), graph.input_names());
  EXPECT_LT(result.graph.size(), graph.size());
  for (uint32_t assignment = 0; assignment < 16; ++assignment) {
    for (size_t i = 0; i < outputs.size(); ++i) {
      EXPECT_EQ(evaluate(graph, outputs[i], assignment), evaluate(result.graph, result.outputs[i], assignment));
    }
  }
  EXPECT_EQ(choice->members().size(), 2U);
}
TEST(XagChoice, ComputedLeavesAreIndependentAndRootDependenciesAreRejected) {
  Xag              graph;
  auto             a = graph.input("a"), b = graph.input("b");
  auto             computed = graph.lxor(a, b), root = graph.land(a, computed);
  Budget           work{1000000};
  const std::array basis{a.id, computed.id};
  auto             choice = make_choice(graph, root, basis, work);
  ASSERT_TRUE(choice);
  // Equal only on reachable correlated assignments, not on the total basis.
  EXPECT_EQ(retain_choice(graph, *choice, graph.land(a, ~b), work), Status::invalid);
  auto cancelled = graph.lor(graph.land(root, a), graph.land(root, ~a));
  EXPECT_EQ(retain_choice(graph, *choice, cancelled, work), Status::invalid);
  const std::array bad_basis{a.id, root.id};
  EXPECT_FALSE(make_choice(graph, root, bad_basis, work));
}
TEST(XagChoice, OverlappingEquivalentClassesCannotPublishAnExtractionCycle) {
  Xag              graph;
  auto             a = graph.input("a"), b = graph.input("b"), c = graph.input("c");
  auto             x = graph.land(graph.lor(a, b), graph.lor(a, c));
  auto             y = graph.lor(a, graph.land(b, c));
  const std::array basis{a.id, b.id, c.id};
  Budget           work{1000000};
  auto             first = make_choice(graph, x, basis, work), second = make_choice(graph, y, basis, work);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_EQ(retain_choice(graph, *first, y, work), Status::feasible);
  ASSERT_EQ(retain_choice(graph, *second, x, work), Status::feasible);
  const std::array choices{*first, *second};
  const std::array selected{1U, 1U};
  const std::array outputs{a, x};
  auto             failed = extract_choices(graph, choices, selected, outputs, work);
  EXPECT_EQ(failed.status, Status::invalid);
  EXPECT_TRUE(failed.outputs.empty());
  const std::array incumbent{0U, 0U};
  EXPECT_EQ(extract_choices(graph, choices, incumbent, outputs, work).status, Status::feasible);
  Budget cancelled{1000000};
  cancelled.admission          = [] { return false; };
  cancelled.admission_interval = 1;
  auto refused                 = extract_choices(graph, choices, incumbent, outputs, cancelled);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_TRUE(refused.outputs.empty());
  EXPECT_TRUE(cancelled.resource_exhausted);
}
}  // namespace
}  // namespace livehd::usyn
