// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "steiner_topology.hpp"

#include <algorithm>
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
// Preserve the original quadratic sweep as an independent ordering oracle.
std::vector<std::vector<size_t>> reference_neighbors(const std::vector<Steiner_location>& locations) {
  const size_t        count = locations.size();
  std::vector<size_t> order(count);
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return std::pair{locations[a].second, locations[a].first} < std::pair{locations[b].second, locations[b].first};
  });
  std::vector<std::vector<size_t>> neighbors(count);
  std::vector<int64_t>             upper_right(count, std::numeric_limits<int64_t>::max());
  std::vector<int64_t>             upper_left(count, std::numeric_limits<int64_t>::min());
  for (size_t idx = 0; idx < count; ++idx) {
    const auto n = order[idx];
    const auto x = locations[n].first;
    for (size_t i = 0; i < idx; ++i) {
      const auto below = order[i];
      const auto bx    = locations[below].first;
      if (bx <= x && x < upper_right[below]) {
        neighbors[below].push_back(n);
        upper_right[below] = x;
      } else if (upper_left[below] < x && x < bx) {
        neighbors[below].push_back(n);
        upper_left[below] = x;
      }
    }
    int64_t right = std::numeric_limits<int64_t>::max(), left = std::numeric_limits<int64_t>::min();
    for (size_t i = idx; i > 0; --i) {
      const auto below = order[i - 1];
      const auto bx    = locations[below].first;
      if (x <= bx && bx < right) {
        neighbors[n].push_back(below);
        right = bx;
      } else if (left < bx && bx < x) {
        neighbors[n].push_back(below);
        left = bx;
      }
    }
  }
  return neighbors;
}
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
TEST(SteinerTopology, IndexedNeighborsPreserveQuadraticSweepOrder) {
  std::mt19937 random(271828);
  EXPECT_TRUE(detail::timing_neighbors({}).empty());
  for (int sample = 0; sample < 120; ++sample) {
    std::vector<Steiner_location> points;
    for (int i = 0; i < (sample < 60 ? 100 + sample : 600 + sample); ++i) {
      // Dense grids and narrow stripes exercise equal coordinates and ties;
      // sparse clouds exercise both quadrants and long query ranges.
      const int     scale = sample % 3 == 0 ? 9 : 10000;
      const int64_t x     = int(random() % scale) - scale / 2;
      const int64_t y     = int(random() % (sample % 3 == 1 ? 3 : scale)) - scale / 2;
      points.emplace_back(x, y);
    }
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    std::shuffle(points.begin(), points.end(), random);
    EXPECT_EQ(detail::timing_neighbors(points), reference_neighbors(points)) << "sample " << sample;
  }
  for (int geometry = 0; geometry < 4; ++geometry) {
    std::vector<Steiner_location> points;
    for (int i = 0; i < 1000; ++i) {
      if (geometry == 0) {
        points.emplace_back(i, 0);
      }
      if (geometry == 1) {
        points.emplace_back(0, i);
      }
      if (geometry == 2) {
        points.emplace_back(i, 2 * i);
      }
      if (geometry == 3) {
        points.emplace_back(i, i < 500 ? 1 : 0);
      }
    }
    EXPECT_EQ(detail::timing_neighbors(points), reference_neighbors(points)) << "geometry " << geometry;
  }
}
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
  for (int i = 0; i < 5000; ++i) {
    pins.emplace_back(i, 2 * i);
    pins.emplace_back(i, 2 * i);
  }
  const auto tree    = timing_steiner_topology(pins, 999);
  const auto lengths = path_lengths(tree, 999);
  for (size_t i = 0; i < pins.size(); ++i) {
    EXPECT_EQ(lengths[tree.terminals[i]], std::abs(pins[i].first - pins[999].first) + std::abs(pins[i].second - pins[999].second));
  }
}
TEST(SteinerTopology, LargeDenseVisibilityStopsAtWorkBound) {
  std::vector<Steiner_location> pins;
  // Every point in the lower right row sees the entire upper left row.
  for (int i = 0; i < 512; ++i) {
    pins.emplace_back(i + 512, 0);
    pins.emplace_back(i, 1);
  }
  EXPECT_THROW(timing_steiner_topology(pins, 0), std::length_error);
}
}  // namespace livehd::placement
