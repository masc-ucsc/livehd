// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "steiner_topology.hpp"

#include <limits>
#include <stdexcept>

#include "gtest/gtest.h"
namespace livehd::placement {
TEST(SteinerTopology, BranchesShareSteinerJunctionAndKeepPinOrder) {
  const std::vector<Steiner_location> pins{{0, 0}, {100, 0}, {50, 100}, {0, 0}};
  const auto                          tree = steiner_topology(pins);
  ASSERT_EQ(tree.terminals.size(), pins.size());
  for (size_t i = 0; i < pins.size(); ++i) {
    EXPECT_EQ(tree.locations.at(tree.terminals[i]), pins[i]);
  }
  EXPECT_EQ(tree.terminals[0], tree.terminals[3]);
  ASSERT_EQ(tree.locations.size(), 4);
  EXPECT_EQ(tree.edges.size(), 3);
  int64_t length = 0;
  for (const auto& [a, b] : tree.edges) {
    length += std::abs(tree.locations[a].first - tree.locations[b].first)
              + std::abs(tree.locations[a].second - tree.locations[b].second);
  }
  EXPECT_EQ(length, 200);
  EXPECT_NE(std::find(tree.locations.begin(), tree.locations.end(), Steiner_location{50, 0}), tree.locations.end());
}
TEST(SteinerTopology, DegeneratePinsAndInputBounds) {
  const auto one = steiner_topology({{10, 20}, {10, 20}});
  EXPECT_EQ(one.locations.size(), 1);
  EXPECT_EQ(one.terminals.size(), 2);
  EXPECT_TRUE(one.edges.empty());
  const auto two = steiner_topology({{-10, -20}, {10, 20}});
  EXPECT_EQ(two.edges.size(), 1);
  EXPECT_THROW(steiner_topology({}), std::invalid_argument);
  EXPECT_THROW(steiner_topology({{std::numeric_limits<int64_t>::max(), 0}}), std::invalid_argument);
  std::vector<Steiner_location> many;
  for (int i = 0; i < 1001; ++i) {
    many.emplace_back(i, i);
  }
  EXPECT_THROW(steiner_topology(many), std::invalid_argument);
}
}  // namespace livehd::placement
