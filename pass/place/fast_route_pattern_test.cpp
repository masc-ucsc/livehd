// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "fast_route_pattern.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
using Edge               = std::pair<int, int>;
constexpr double blocked = std::numeric_limits<double>::infinity();

// Independent candidate oracle: explicitly walk all four-corner HVH/VHV
// polylines, rather than using the implementation's rectangle sums/prefixes.
double oracle(int cols, int a, int b, const std::map<Edge, double>& weights, bool l_only = false) {
  const int  ax = a % cols, ay = a / cols, bx = b % cols, by = b / cols;
  double     best      = blocked;
  const auto candidate = [&](std::vector<int> corners) {
    std::set<Edge> edges;
    for (size_t i = 1; i < corners.size(); ++i) {
      int cursor = corners[i - 1];
      while (cursor != corners[i]) {
        const int next = cursor / cols == corners[i] / cols ? cursor + (cursor < corners[i] ? 1 : -1)
                                                            : cursor + (cursor < corners[i] ? cols : -cols);
        edges.emplace(std::minmax(cursor, next));
        cursor = next;
      }
    }
    double value = 0;
    for (const auto& edge : edges) {
      value += weights.at(edge);
    }
    if (std::isfinite(value)) {
      best = std::min(best, value);
    }
  };
  for (int x = std::min(ax, bx); x <= std::max(ax, bx); ++x) {
    if (!l_only || x == ax || x == bx) {
      candidate({a, ay * cols + x, by * cols + x, b});
    }
  }
  for (int y = std::min(ay, by); !l_only && y <= std::max(ay, by); ++y) {
    candidate({a, y * cols + ax, y * cols + bx, b});
  }
  return best;
}

std::map<Edge, double> weights(int cols, int rows, double horizontal = 1, double vertical = 1) {
  std::map<Edge, double> result;
  for (int y = 0; y < rows; ++y) {
    for (int x = 0; x < cols; ++x) {
      const int t = y * cols + x;
      if (x + 1 < cols) {
        result[{t, t + 1}] = horizontal;
      }
      if (y + 1 < rows) {
        result[{t, t + cols}] = vertical;
      }
    }
  }
  return result;
}

void check_connected(const Pattern_route& route, int cols, int rows, const std::vector<int>& vertices,
                     const std::map<Edge, double>& weights) {
  ASSERT_EQ(route.status, Pattern_route_status::complete);
  EXPECT_TRUE(std::ranges::is_sorted(route.edges));
  EXPECT_EQ(std::set<Edge>(route.edges.begin(), route.edges.end()).size(), route.edges.size());
  std::vector<int> parent(size_t(cols * rows));
  std::iota(parent.begin(), parent.end(), 0);
  const auto root = [&](int t) {
    while (parent[size_t(t)] != t) {
      t = parent[size_t(t)];
    }
    return t;
  };
  double cost = 0;
  for (const auto& [a, b] : route.edges) {
    ASSERT_GE(a, 0);
    ASSERT_LT(a, b);
    ASSERT_LT(b, cols * rows);
    EXPECT_EQ(std::abs(a % cols - b % cols) + std::abs(a / cols - b / cols), 1);
    ASSERT_TRUE(std::isfinite(weights.at({a, b})));
    cost                    += weights.at({a, b});
    parent[size_t(root(a))]  = root(b);
  }
  for (int t : vertices) {
    EXPECT_EQ(root(t), root(vertices.front()));
  }
  EXPECT_DOUBLE_EQ(route.cost, cost);
}
}  // namespace

TEST(FastRoutePattern, CandidateCostMatchesIndependentRectangularGridOracle) {
  std::mt19937                                 random(1729);
  const std::vector<std::pair<size_t, size_t>> branches{
      {0, 1}
  };
  for (const auto& [cols, rows] : {
           std::pair{4, 3},
           std::pair{3, 7}
  }) {
    for (unsigned trial = 0; trial < 12; ++trial) {
      auto grid = weights(cols, rows);
      for (auto& [edge, value] : grid) {
        // Distinct horizontal/vertical costs include anisotropic physical length.
        value = random() % 7 == 0 ? blocked : double(1 + random() % 23) * (edge.second - edge.first == 1 ? 3 : 1);
      }
      const auto cost = [&](int a, int b) { return grid.at({a, b}); };
      for (int a = 0; a < cols * rows; ++a) {
        for (int b = a + 1; b < cols * rows; ++b) {
          const std::vector<int> vertices{a, b};
          const auto             result   = fast_route_pattern(cols, rows, vertices, branches, cost, 10000);
          const double           expected = oracle(cols, a, b, grid);
          if (std::isfinite(expected)) {
            ASSERT_EQ(result.status, Pattern_route_status::complete) << cols << 'x' << rows << ' ' << a << ' ' << b;
            EXPECT_DOUBLE_EQ(result.cost, expected);
            check_connected(result, cols, rows, vertices, grid);
          } else {
            EXPECT_EQ(result.status, Pattern_route_status::unresolved);
            EXPECT_TRUE(result.edges.empty());
            EXPECT_EQ(result.cost, 0);
          }
          const std::vector<int> reverse{b, a};
          const auto             reversed = fast_route_pattern(cols, rows, reverse, branches, cost, 10000);
          EXPECT_EQ(result.status, reversed.status);
          EXPECT_EQ(result.edges, reversed.edges);
          EXPECT_EQ(result.cost, reversed.cost);
          const auto   fast          = fast_route_pattern(cols, rows, vertices, branches, cost, 10000, Pattern_route_mode::l_first);
          const double l_cost        = oracle(cols, a, b, grid, true);
          const double fast_expected = std::isfinite(l_cost) ? l_cost : expected;
          if (std::isfinite(fast_expected)) {
            ASSERT_EQ(fast.status, Pattern_route_status::complete);
            EXPECT_DOUBLE_EQ(fast.cost, fast_expected);
            check_connected(fast, cols, rows, vertices, grid);
          } else {
            EXPECT_EQ(fast.status, Pattern_route_status::unresolved);
            EXPECT_TRUE(fast.edges.empty());
          }
          const auto fast_reversed = fast_route_pattern(cols, rows, reverse, branches, cost, 10000, Pattern_route_mode::l_first);
          EXPECT_EQ(fast.status, fast_reversed.status);
          EXPECT_EQ(fast.edges, fast_reversed.edges);
        }
      }
    }
  }
}

TEST(FastRoutePattern, InteriorZRoutesAroundBothForbiddenLBends) {
  auto grid      = weights(5, 5);
  grid[{0, 1}]   = blocked;
  grid[{20, 21}] = blocked;
  const std::vector<int>                       vertices{0, 24};
  const std::vector<std::pair<size_t, size_t>> branches{
      {0, 1}
  };
  const auto result = fast_route_pattern(5, 5, vertices, branches, [&](int a, int b) { return grid.at({a, b}); }, 1000);
  check_connected(result, 5, 5, vertices, grid);
  EXPECT_EQ(result.cost, 8);
  EXPECT_EQ(result.edges.size(), 8);
  const auto l_first = fast_route_pattern(
      5,
      5,
      vertices,
      branches,
      [&](int a, int b) { return grid.at({a, b}); },
      1000,
      Pattern_route_mode::l_first);
  check_connected(l_first, 5, 5, vertices, grid);
  EXPECT_EQ(l_first.cost, result.cost);
}

TEST(FastRoutePattern, LFirstAvoidsScanningTheRectangle) {
  constexpr int                                cols = 1024, rows = 511;
  const std::vector<int>                       vertices{0, cols * rows - 1};
  const std::vector<std::pair<size_t, size_t>> branches{
      {0, 1}
  };
  size_t     calls = 0;
  const auto cost  = [&](int a, int b) {
    ++calls;
    EXPECT_TRUE(b - a == 1 || b - a == cols);
    return b - a == cols ? 2.0 : 3.0;
  };
  const auto fast = fast_route_pattern(cols, rows, vertices, branches, cost, 5000, Pattern_route_mode::l_first);
  ASSERT_EQ(fast.status, Pattern_route_status::complete);
  EXPECT_EQ(fast.edges.size(), size_t(cols + rows - 2));
  EXPECT_EQ(fast.cost, 3 * (cols - 1) + 2 * (rows - 1));
  EXPECT_LE(calls, size_t(2 * (cols + rows - 2)));
  EXPECT_LE(fast.work, 5000u);
  const auto exhaustive = fast_route_pattern(cols, rows, vertices, branches, cost, 5000);
  EXPECT_EQ(exhaustive.status, Pattern_route_status::work_limit);
  EXPECT_TRUE(exhaustive.edges.empty());
}

TEST(FastRoutePattern, LFirstQualityTradeoffIsExplicit) {
  auto grid = weights(5, 5);
  for (auto& [edge, value] : grid) {
    const int x = edge.first % 5, y = edge.first / 5;
    if (edge.second - edge.first == 1 ? y == 0 || y == 4 : x == 0 || x == 4) {
      value = 10;
    }
  }
  const std::vector<int>                       vertices{0, 24};
  const std::vector<std::pair<size_t, size_t>> branches{
      {0, 1}
  };
  const auto cost       = [&](int a, int b) { return grid.at({a, b}); };
  const auto fast       = fast_route_pattern(5, 5, vertices, branches, cost, 1000, Pattern_route_mode::l_first);
  const auto exhaustive = fast_route_pattern(5, 5, vertices, branches, cost, 1000);
  check_connected(fast, 5, 5, vertices, grid);
  check_connected(exhaustive, 5, 5, vertices, grid);
  EXPECT_EQ(fast.cost, 80);
  EXPECT_EQ(exhaustive.cost, 44);
  EXPECT_LT(fast.work, exhaustive.work);
}

TEST(FastRoutePattern, ArbitraryDegreeTopologySharesMetalOncePerNet) {
  const auto                                   grid = weights(7, 7, 3, 2);
  // Degree six, beyond FastRoute's fixed three-neighbor Steiner node layout.
  const std::vector<int>                       vertices{24, 0, 6, 42, 48, 3, 45, 24};
  const std::vector<std::pair<size_t, size_t>> branches{
      {0, 1},
      {0, 2},
      {0, 3},
      {0, 4},
      {0, 5},
      {0, 6},
      {0, 7},
      {1, 0}
  };
  std::map<Edge, size_t> calls;
  const auto             result = fast_route_pattern(
      7,
      7,
      vertices,
      branches,
      [&](int a, int b) {
        ++calls[{a, b}];
        return grid.at({a, b});
      },
      10000);
  check_connected(result, 7, 7, vertices, grid);
  // Independent branches have total physical cost 72 (and duplicate branch 15).
  // Shared center-to-top/bottom trunks must reduce that cost, and never duplicate
  // callback evaluations or returned/reserved edges.
  EXPECT_LT(result.cost, 72);
  for (const auto& [edge, count] : calls) {
    EXPECT_EQ(count, 1u) << edge.first << ' ' << edge.second;
  }
}

TEST(FastRoutePattern, FailedLaterBranchNeverPublishesEarlierPartialRoute) {
  auto grid    = weights(6, 3);
  grid[{1, 2}] = std::numeric_limits<double>::quiet_NaN();
  const std::vector<int>                       vertices{0, 1, 2};
  const std::vector<std::pair<size_t, size_t>> branches{
      {0, 1},
      {1, 2}
  };
  const auto result = fast_route_pattern(6, 3, vertices, branches, [&](int a, int b) { return grid.at({a, b}); }, 1000);
  // A maze detour exists through the next row; failure is explicitly a pattern
  // miss, not a proof that the physical topology is disconnected.
  EXPECT_EQ(result.status, Pattern_route_status::unresolved);
  EXPECT_TRUE(result.edges.empty());
  EXPECT_EQ(result.cost, 0);
}

TEST(FastRoutePattern, ExhaustionNeverPublishesPartialOrUnscoredSuccess) {
  const auto                                   grid = weights(8, 7);
  const std::vector<int>                       vertices{0, 1, 55};
  const std::vector<std::pair<size_t, size_t>> branches{
      {0, 1},
      {1, 2}
  };
  const auto cost = [&](int a, int b) { return grid.at({a, b}); };
  for (auto mode : {Pattern_route_mode::exhaustive, Pattern_route_mode::l_first}) {
    const auto complete = fast_route_pattern(8, 7, vertices, branches, cost, 10000, mode);
    ASSERT_EQ(complete.status, Pattern_route_status::complete);
    for (size_t budget : {size_t(0), size_t(5), size_t(20), complete.work - 1}) {
      const auto result = fast_route_pattern(8, 7, vertices, branches, cost, budget, mode);
      EXPECT_EQ(result.status, Pattern_route_status::work_limit);
      EXPECT_LE(result.work, budget);
      EXPECT_TRUE(result.edges.empty());
      EXPECT_EQ(result.cost, 0);
    }
    const auto exact = fast_route_pattern(8, 7, vertices, branches, cost, complete.work, mode);
    EXPECT_EQ(exact.status, Pattern_route_status::complete);
    EXPECT_EQ(exact.edges, complete.edges);
  }
}

TEST(FastRoutePattern, CoincidentVerticesAndDisconnectedCallerTopology) {
  const auto             cost = [](int, int) { return 1.0; };
  const std::vector<int> coincident{3, 3, 3}, separated{3, 5};
  const auto             zero = fast_route_pattern(9, 2, coincident, {}, cost, 10);
  EXPECT_EQ(zero.status, Pattern_route_status::complete);
  EXPECT_TRUE(zero.edges.empty());
  EXPECT_EQ(zero.cost, 0);
  const auto disconnected = fast_route_pattern(9, 2, separated, {}, cost, 10);
  EXPECT_EQ(disconnected.status, Pattern_route_status::unresolved);
  EXPECT_TRUE(disconnected.edges.empty());
}

TEST(FastRoutePattern, RejectsMalformedInputAndNegativeCosts) {
  const auto                                   cost = [](int, int) { return 1.0; };
  const std::vector<int>                       vertices{0, 1}, outside{0, 4};
  const std::vector<std::pair<size_t, size_t>> branches{
      {0, 1}
  },
      invalid{{0, 2}};
  EXPECT_THROW(fast_route_pattern(0, 2, vertices, branches, cost, 100), std::invalid_argument);
  EXPECT_THROW(fast_route_pattern(std::numeric_limits<int>::max(), 2, vertices, branches, cost, 100), std::invalid_argument);
  EXPECT_THROW(fast_route_pattern(2, 2, outside, branches, cost, 100), std::invalid_argument);
  EXPECT_THROW(fast_route_pattern(2, 2, vertices, invalid, cost, 100), std::invalid_argument);
  EXPECT_THROW(fast_route_pattern(2, 2, vertices, branches, {}, 100), std::invalid_argument);
  EXPECT_THROW(fast_route_pattern(2, 2, vertices, branches, [](int, int) { return -1.0; }, 100), std::invalid_argument);
}

}  // namespace livehd::placement
