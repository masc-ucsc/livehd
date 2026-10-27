// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "steiner_topology.hpp"

#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

#include "gtest/gtest.h"
namespace livehd::placement {
TEST(SteinerTopology, BranchesShareSteinerJunctionAndKeepPinOrder) {
  const std::vector<Steiner_location> pins{
      {  0,   0},
      {100,   0},
      { 50, 100},
      {  0,   0}
  };
  const auto tree = steiner_topology(pins);
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
  const auto one = steiner_topology({
      {10, 20},
      {10, 20}
  });
  EXPECT_EQ(one.locations.size(), 1);
  EXPECT_EQ(one.terminals.size(), 2);
  EXPECT_TRUE(one.edges.empty());
  const auto two = steiner_topology({
      {-10, -20},
      { 10,  20}
  });
  EXPECT_EQ(two.edges.size(), 1);
  EXPECT_THROW(steiner_topology({}), std::invalid_argument);
  EXPECT_THROW(steiner_topology({
                   {std::numeric_limits<int64_t>::max(), 0}
  }),
               std::invalid_argument);
  std::vector<Steiner_location> many;
  for (int i = 0; i < 1001; ++i) {
    many.emplace_back(i, i);
  }
  EXPECT_THROW(steiner_topology(many), std::invalid_argument);
}

namespace {
std::vector<int64_t> path_lengths(const Steiner_topology& tree, size_t driver) {
  std::vector<std::vector<size_t>> adjacent(tree.locations.size());
  for (const auto& [a, b] : tree.edges) {
    adjacent.at(a).push_back(b);
    adjacent.at(b).push_back(a);
  }
  std::vector<int64_t> lengths(tree.locations.size(), -1);
  std::vector<size_t>  pending{tree.terminals.at(driver)};
  lengths[pending.front()] = 0;
  for (size_t i = 0; i < pending.size(); ++i) {
    const auto a = pending[i];
    for (const auto b : adjacent[a]) {
      if (lengths[b] < 0) {
        lengths[b] = lengths[a] + std::abs(tree.locations[a].first - tree.locations[b].first)
                     + std::abs(tree.locations[a].second - tree.locations[b].second);
        pending.push_back(b);
      }
    }
  }
  EXPECT_EQ(pending.size(), tree.locations.size());
  EXPECT_EQ(tree.edges.size() + 1, tree.locations.size());
  return lengths;
}
}  // namespace
TEST(SteinerTopology, TimingTreeSharesWireAndRetainsCoincidentTerminals) {
  const std::vector<Steiner_location> pins{
      {  0,   0},
      {100,   0},
      { 50, 100},
      {  0,   0}
  };
  const auto tree = timing_steiner_topology(pins, 3);
  ASSERT_EQ(tree.terminals.size(), pins.size());
  for (size_t i = 0; i < pins.size(); ++i) {
    EXPECT_EQ(tree.locations.at(tree.terminals[i]), pins[i]);
  }
  EXPECT_EQ(tree.terminals[0], tree.terminals[3]);
  const auto lengths = path_lengths(tree, 3);
  EXPECT_EQ(lengths[tree.terminals[1]], 100);
  EXPECT_EQ(lengths[tree.terminals[2]], 150);
  int64_t wire = 0;
  for (const auto& [a, b] : tree.edges) {
    wire += std::abs(tree.locations[a].first - tree.locations[b].first)
            + std::abs(tree.locations[a].second - tree.locations[b].second);
  }
  EXPECT_EQ(wire, 200);
  EXPECT_THROW(timing_steiner_topology({}, 0), std::invalid_argument);
  EXPECT_THROW(timing_steiner_topology(pins, pins.size()), std::invalid_argument);
  EXPECT_THROW(timing_steiner_topology(
                   {
                       {std::numeric_limits<int64_t>::max(), 0}
  },
                   0),
               std::invalid_argument);
  const auto one = timing_steiner_topology(
      {
          {10, 20},
          {10, 20}
  },
      1);
  EXPECT_EQ(one.locations.size(), 1);
  EXPECT_TRUE(one.edges.empty());
}
TEST(SteinerTopology, TimingTreeAccountsForDriverAndAvoidsLongDetours) {
  // Fixed scattered terminals: an unrooted minimum-length tree puts multiple
  // source-to-sink paths behind a winding shared trunk.
  std::mt19937                  random(817);
  std::vector<Steiner_location> pins{
      {0, 0}
  };
  for (size_t i = 0; i < 40; ++i) {
    const int64_t x = random() % 1000, y = random() % 1000;
    pins.emplace_back(x, y);
  }
  const auto geometric     = steiner_topology(pins);
  const auto timed         = timing_steiner_topology(pins, 0);
  const auto moved_driver  = timing_steiner_topology(pins, pins.size() - 1);
  const auto base_lengths  = path_lengths(geometric, 0);
  const auto timed_lengths = path_lengths(timed, 0);
  path_lengths(moved_driver, pins.size() - 1);
  int64_t base_total = 0, timed_total = 0;
  for (size_t i = 0; i < pins.size(); ++i) {
    base_total  += base_lengths[geometric.terminals[i]];
    timed_total += timed_lengths[timed.terminals[i]];
  }
  EXPECT_LT(timed_total, base_total);
  EXPECT_TRUE(timed.locations != moved_driver.locations || timed.edges != moved_driver.edges);
}
TEST(SteinerTopology, TimingTreeHandlesCollinearAndLargeCoincidentInputs) {
  std::vector<Steiner_location> pins;
  for (int i = 0; i < 1000; ++i) {
    pins.emplace_back(i, 2 * i);
    pins.emplace_back(i, 2 * i);
  }
  const auto tree    = timing_steiner_topology(pins, 999);
  const auto lengths = path_lengths(tree, 999);
  for (size_t i = 0; i < pins.size(); ++i) {
    EXPECT_EQ(lengths[tree.terminals[i]], std::abs(pins[i].first - pins[999].first) + std::abs(pins[i].second - pins[999].second));
  }
  pins.emplace_back(1000, 2000);
  EXPECT_THROW(timing_steiner_topology(pins, 0), std::invalid_argument);
}
}  // namespace livehd::placement
