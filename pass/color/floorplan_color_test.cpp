// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "floorplan_color.hpp"

#include <algorithm>
#include <stdexcept>

#include "gtest/gtest.h"

namespace livehd::color {
using K = Floorplan_kind;
TEST(FloorplanColor, BothConesMergeButSharedBoundaryDoesNot) {
  const std::vector<Floorplan_node> nodes = {
      {    "ram0",   K::macro, 10},
      {    "ram1",   K::macro, 10},
      {"producer",   K::logic,  1},
      {"consumer",   K::logic,  1},
      {      "ff",   K::state,  2},
      {   "clock", K::barrier,  1}
  };
  const auto shared = floorplan_color(nodes,
                                      {
                                          {2, 0},
                                          {2, 1},
                                          {0, 3},
                                          {3, 4},
                                          {5, 0},
                                          {5, 1}
  });
  ASSERT_EQ(shared.colors.size(), 1);
  EXPECT_EQ(shared.colors[0].members, std::vector<int>({0, 1, 2, 3}));
  EXPECT_EQ(shared.owner[4], -1);
  EXPECT_EQ(shared.owner[5], -1);
  const auto state = floorplan_color(nodes,
                                     {
                                         {4, 2},
                                         {2, 0},
                                         {4, 1},
                                         {0, 3}
  });
  ASSERT_EQ(state.colors.size(), 2);
  EXPECT_EQ(state.recipients[4], std::vector<int>({0, 1}));
}
TEST(FloorplanColor, ExactlyOneExtraStageAndDistinctRecipients) {
  const std::vector<Floorplan_node> nodes = {
      {     "a", K::macro, 10},
      {     "b", K::macro, 10},
      {     "c", K::macro, 10},
      {   "ff0", K::state,  2},
      {   "ff1", K::state,  2},
      {   "ff2", K::state,  2},
      {"shared", K::logic,  3},
      {  "next", K::state,  2},
      {   "far", K::logic,  1}
  };
  const auto result = floorplan_color(nodes,
                                      {
                                          {0, 3},
                                          {1, 4},
                                          {2, 5},
                                          {3, 6},
                                          {3, 6},
                                          {4, 6},
                                          {5, 6},
                                          {6, 7},
                                          {7, 8}
  });
  EXPECT_EQ(result.recipients[6], std::vector<int>({0, 1, 2}));
  EXPECT_TRUE(result.recipients[7].empty());
  EXPECT_TRUE(result.recipients[8].empty());
  EXPECT_EQ(result.owner[6], -1);
  const auto backward = floorplan_color(nodes,
                                        {
                                            {3, 0},
                                            {4, 1},
                                            {6, 3},
                                            {6, 4},
                                            {7, 6},
                                            {8, 7}
  });
  EXPECT_EQ(backward.recipients[6], std::vector<int>({0, 1}));
  EXPECT_TRUE(backward.recipients[7].empty());
  EXPECT_TRUE(backward.recipients[8].empty());
}
TEST(FloorplanColor, ForeignColorsAndMacrosStopReservation) {
  const std::vector<Floorplan_node> nodes = {
      {      "a", K::macro, 10},
      {      "b", K::macro, 10},
      {     "ff", K::state,  2},
      {"foreign", K::logic,  1},
      { "beyond", K::logic,  1}
  };
  const auto result = floorplan_color(nodes,
                                      {
                                          {0, 2},
                                          {2, 3},
                                          {3, 1},
                                          {1, 4}
  });
  EXPECT_TRUE(result.recipients[1].empty());
  EXPECT_TRUE(result.recipients[3].empty());
  EXPECT_TRUE(result.recipients[4].empty());
  EXPECT_EQ(result.colors.size(), 2);
}

TEST(FloorplanColor, RepairsPreserveMacroPartitionAndRefreshAreasAndReservation) {
  std::vector<Floorplan_node> nodes{
      {     "a", K::macro, 10},
      {     "b", K::macro, 10},
      {"shared", K::logic,  1},
      {    "ff", K::state,  2},
      {  "next", K::logic,  3}
  };
  auto initial = floorplan_color(nodes,
                                 {
                                     {2, 0},
                                     {2, 1},
                                     {0, 3},
                                     {3, 4}
  });
  ASSERT_EQ(initial.colors.size(), 1);
  // Removing a shared buffer does not split a live centroid midway through GP.
  nodes[2]       = {"inserted", K::logic, 2};
  auto preserved = initial;
  auto updated   = floorplan_color(nodes,
                                   {
                                       {2, 0},
                                       {0, 3},
                                       {3, 4}
  },
                                   &preserved);
  EXPECT_EQ(updated.colors.size(), 1);
  EXPECT_EQ(updated.owner, initial.owner);
  EXPECT_EQ(updated.colors[0].area, 22);
  EXPECT_EQ(updated.recipients[4], std::vector<int>({0}));
  // A disconnected next-stage cell ceases to reserve space after a graph edit.
  updated = floorplan_color(nodes,
                            {
                                {2, 0},
                                {0, 3}
  },
                            &preserved);
  EXPECT_TRUE(updated.recipients[4].empty());
}

TEST(FloorplanColor, DeterministicTransitiveMergeAndNoMacroTraversal) {
  std::vector<Floorplan_node> nodes{
      {  "z", K::macro, 10},
      {  "a", K::macro, 10},
      {  "m", K::macro, 10},
      { "ab", K::logic,  1},
      { "bc", K::logic,  1},
      {"far", K::macro, 10}
  };
  std::vector<std::pair<int, int>> edges{
      {3, 0},
      {3, 1},
      {4, 1},
      {4, 2},
      {2, 5}
  };
  const auto first = floorplan_color(nodes, edges);
  std::reverse(edges.begin(), edges.end());
  const auto second = floorplan_color(nodes, edges);
  EXPECT_EQ(first.owner, second.owner);
  ASSERT_EQ(first.colors.size(), 2);
  EXPECT_EQ(first.colors[0].name, "a");
  EXPECT_EQ(first.colors[0].macros, 3);
  EXPECT_NE(first.owner[2], first.owner[5]);
}
TEST(FloorplanColor, MemoryFirstRoundRobinAndAreaBudget) {
  const std::vector<Floorplan_node> nodes = {
      { "a", K::macro, 1000},
      { "b", K::macro, 1000},
      { "c", K::macro, 1000},
      { "d", K::macro, 1000},
      {"ab", K::logic,    4},
      {"bc", K::logic,    1},
      {"cd", K::logic,    4}
  };
  std::vector<std::pair<int, int>> edges = {
      {4, 0},
      {4, 1},
      {5, 1},
      {5, 2},
      {6, 2},
      {6, 3}
  };
  const auto all = floorplan_color(nodes, edges, nullptr, {true, 9});
  ASSERT_EQ(all.colors.size(), 1);
  EXPECT_EQ(all.merges_per_round, std::vector<int>({2, 1, 0}));
  EXPECT_EQ(all.colors[0].area, 4009);  // Macro footprint is not a logic-growth budget.
  const auto bounded = floorplan_color(nodes, edges, nullptr, {true, 5});
  ASSERT_EQ(bounded.colors.size(), 2);
  EXPECT_EQ(bounded.merges_per_round, std::vector<int>({2, 0}));
  EXPECT_EQ(bounded.owner[0], bounded.owner[1]);
  EXPECT_EQ(bounded.owner[2], bounded.owner[3]);
  EXPECT_NE(bounded.owner[0], bounded.owner[2]);
  std::reverse(edges.begin(), edges.end());
  EXPECT_EQ(floorplan_color(nodes, edges, nullptr, {true, 5}).owner, bounded.owner);
  EXPECT_THROW(floorplan_color(nodes, edges, nullptr, {true, 0}), std::invalid_argument);
}
TEST(FloorplanColor, MemoryFirstAbsorbsOrdinaryConeAndStopsAtState) {
  const std::vector<Floorplan_node> nodes = {
      {           "a",   K::macro, 10},
      {      "shared",   K::logic,  1},
      {       "extra",   K::logic,  2},
      {          "ff",   K::state,  3},
      {      "beyond",   K::logic,  4},
      {"output_logic",   K::logic,  2},
      {         "out", K::barrier,  0}
  };
  const std::vector<std::pair<int, int>> edges = {
      {1, 0},
      {1, 2},
      {2, 3},
      {3, 4},
      {0, 5},
      {5, 6}
  };
  const auto bounded = floorplan_color(nodes, edges, nullptr, {true, 1});
  EXPECT_EQ(bounded.owner[2], -1);
  EXPECT_GT(bounded.truncated_cones, 0);
  const auto grown = floorplan_color(nodes, edges, nullptr, {true, 3});
  ASSERT_EQ(grown.colors.size(), 1);
  EXPECT_EQ(grown.colors[0].members, std::vector<int>({0, 1, 2}));
  EXPECT_EQ(grown.recipients[3], std::vector<int>({0}));
  EXPECT_EQ(grown.recipients[4], std::vector<int>({0}));
  EXPECT_EQ(grown.owner[5], -1);  // Output-only cones are not unconditional SRAM members.
  auto changed        = nodes;
  changed[2].area     = 5;
  const auto repaired = floorplan_color(changed, edges, &grown, {true, 3});
  EXPECT_EQ(repaired.owner, grown.owner);
  EXPECT_EQ(repaired.colors[0].area, 16);  // Preserve identity through resize even above the original cap.
  EXPECT_EQ(repaired.merges_per_round, grown.merges_per_round);
}
TEST(FloorplanColor, MemoryFirstExactDriverPinsAndNoMemoryTraversal) {
  const std::vector<Floorplan_node> nodes = {
      {"source", K::macro, 10},
      {"a", K::macro, 10, {1}},
      {"b", K::macro, 10, {2}},
      {"a_copy", K::macro, 10, {1}},
      {"empty", K::macro, 10}
  };
  const auto result = floorplan_color(nodes,
                                      {
                                          {0, 1},
                                          {0, 2},
                                          {0, 3}
  },
                                      nullptr,
                                      {true, 10});
  EXPECT_EQ(result.owner[1], result.owner[3]);
  EXPECT_NE(result.owner[1], result.owner[2]);
  EXPECT_NE(result.owner[1], result.owner[0]);
  EXPECT_NE(result.owner[0], result.owner[4]);
  EXPECT_EQ(result.initial_memory_groups, 4);
}
TEST(FloorplanColor, HierarchyPortionsKeepLocalMembershipAndReservation) {
  const std::vector<Floorplan_node> nodes = {
      {  "A.ram", K::macro, 100},
      {  "B.ram", K::macro, 100},
      { "shared", K::logic,   1},
      {  "A.out", K::logic,   1},
      {  "B.out", K::logic,   1},
      {   "A.ff", K::state,   1},
      {   "B.ff", K::state,   1},
      {"A.extra", K::logic,   1},
      {"B.extra", K::logic,   1}
  };
  const std::vector<std::pair<int, int>> edges{
      {2, 0},
      {2, 1},
      {0, 3},
      {1, 4},
      {3, 5},
      {4, 6},
      {5, 7},
      {6, 8}
  };
  for (bool memory : {false, true}) {
    const auto global = floorplan_color(nodes, edges, nullptr, {memory, 10});
    ASSERT_EQ(global.colors.size(), 1);
    const auto local = floorplan_color(nodes,
                                       edges,
                                       nullptr,
                                       {
                                           memory,
                                           10,
                                           {0, 1, -1, 0, 1, 0, 1, 0, 1}
    });
    ASSERT_EQ(local.colors.size(), 3);
    EXPECT_NE(local.owner[0], local.owner[1]);
    EXPECT_NE(local.owner[0], local.owner[2]);
    EXPECT_NE(local.owner[1], local.owner[2]);
    EXPECT_EQ(local.colors[local.owner[0]].macros, 1);
    EXPECT_EQ(local.colors[local.owner[1]].macros, 1);
    EXPECT_EQ(local.colors[local.owner[2]].macros, 0);
    if (!memory) {
      EXPECT_EQ(local.recipients[7], std::vector<int>({local.owner[0]}));
      EXPECT_EQ(local.recipients[8], std::vector<int>({local.owner[1]}));
    }
    const auto frozen = floorplan_color(nodes, edges, &local, {memory, 10});
    EXPECT_EQ(frozen.owner, local.owner);
    EXPECT_EQ(frozen.recipients, local.recipients);
    for (size_t c = 0; c < local.colors.size(); ++c) {
      EXPECT_EQ(frozen.colors[c].name, local.colors[c].name);
      EXPECT_EQ(frozen.colors[c].area, local.colors[c].area);
    }
    const auto flat = floorplan_color(nodes, edges, nullptr, {memory, 10, std::vector<int>(nodes.size(), -1)});
    EXPECT_EQ(flat.owner, global.owner);
    EXPECT_EQ(flat.recipients, global.recipients);
    EXPECT_EQ(flat.colors[0].name, global.colors[0].name);
  }
  EXPECT_THROW(floorplan_color(nodes, edges, nullptr, {false, 0, {0}}), std::invalid_argument);
}
}  // namespace livehd::color
