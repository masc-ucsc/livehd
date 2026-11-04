// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <numeric>
#include <queue>
#include <random>
#include <set>

#include "gtest/gtest.h"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
Grid grid(int cols, int rows) {
  Grid g;
  g.cols    = cols;
  g.rows    = rows;
  g.regions = 1;
  g.region.assign(size_t(cols * rows), 0);
  return g;
}

void check_tree(const Grid& g, const Global_tree& tree, const std::vector<int>& endpoints) {
  ASSERT_TRUE(tree.routed);
  std::vector<int> parent(g.size());
  std::iota(parent.begin(), parent.end(), 0);
  const auto root = [&](int t) {
    while (parent[size_t(t)] != t) {
      t = parent[size_t(t)];
    }
    return t;
  };
  std::set<std::pair<int, int>> seen;
  for (const auto& [a, b] : tree.edges) {
    ASSERT_GE(a, 0);
    ASSERT_LT(a, b);
    ASSERT_LT(size_t(b), g.size());
    EXPECT_EQ(std::abs(g.col(a) - g.col(b)) + std::abs(g.row(a) - g.row(b)), 1);
    EXPECT_TRUE(seen.emplace(a, b).second);
    EXPECT_NE(root(a), root(b)) << "cycle in routed tree";
    parent[size_t(root(a))] = root(b);
  }
  for (int t : endpoints) {
    EXPECT_EQ(root(t), root(endpoints.front()));
  }
}
}  // namespace

TEST(LegrouteGlobal, DenseBroadcastDoesNotRebuildTheTreeForEverySink) {
  const auto       g = grid(64, 32);
  std::vector<int> cap(g.size(), 8);
  Global_net       net;
  net.endpoints.resize(g.size());
  std::iota(net.endpoints.begin(), net.endpoints.end(), 0);
  for (bool own : {false, true}) {
    net.own_regions = own ? std::vector<int>{0} : std::vector<int>{};
    const auto r    = global_route(g, cap, cap, .5, 0, {net}, 0, 0);
    ASSERT_EQ(r.unrouted, 0u);
    EXPECT_EQ(r.overflow_edges, 0u);
    check_tree(g, r.trees[0], net.endpoints);
    EXPECT_EQ(r.trees[0].edges.size(), g.size() - 1);
    EXPECT_EQ(r.search_restarts, 1u);
    EXPECT_GT(r.search_pops, g.size());
    RecordProperty(own ? "own_region_pops" : "counted_pops", int(r.search_pops));
    RecordProperty(own ? "own_region_pushes" : "counted_pushes", int(r.search_pushes));
    // An operation bound, independent of build mode or machine speed. Rebuilding the old tree per sink
    // takes over two million pops here; retaining the frontier needs only a small multiple of the grid.
    EXPECT_LT(r.search_pops, 12 * g.size());
    EXPECT_LT(r.search_pushes, 12 * g.size());
  }
}

TEST(LegrouteGlobal, PatternDisabledPreservesMazeOrderingAndAccounting) {
  auto g   = grid(6, 1);
  g.tile_w = 800;
  g.tile_h = 500;
  std::vector<int> cap(g.size(), 2);
  Global_net       net;
  net.endpoints                                   = {0, 5};
  const auto                             baseline = global_route(g, cap, cap, 1, 0, {net}, 0, 0);
  const auto                             disabled = global_route(g, cap, cap, 1, 0, {net}, 0, 0, 0, 1, false);
  const std::vector<std::pair<int, int>> expected{
      {4, 5},
      {3, 4},
      {2, 3},
      {1, 2},
      {0, 1}
  };
  EXPECT_EQ(disabled.trees[0].edges, expected);
  EXPECT_EQ(disabled.trees[0].edges, baseline.trees[0].edges);
  EXPECT_EQ(disabled.search_pops, baseline.search_pops);
  EXPECT_EQ(disabled.search_pushes, baseline.search_pushes);
  EXPECT_EQ(disabled.search_restarts, baseline.search_restarts);
  EXPECT_EQ(disabled.pattern_attempts + disabled.pattern_work, 0u);
}

TEST(LegrouteGlobal, PatternUsesZWhenBothLsAreBlockedAndCommitsOnlyAvailableCapacity) {
  auto g   = grid(5, 5);
  g.tile_w = 300;
  g.tile_h = 100;
  std::vector<int> h(g.size(), 1), v(g.size(), 1);
  h[size_t(g.tile(1, 0))] = h[size_t(g.tile(1, 4))] = 0;
  Global_net net;
  net.endpoints = {g.tile(0, 0), g.tile(4, 4)};
  const auto z  = global_route(g, h, v, 1, 0, {net}, 0, 0, 0, 1, true);
  ASSERT_EQ(z.pattern_completed, 1u);
  EXPECT_EQ(z.pattern_fallbacks, 0u);
  EXPECT_EQ(z.search_pops, 0u);
  EXPECT_EQ(z.overflow_tracks, 0u);
  check_tree(g, z.trees[0], net.endpoints);
  for (const auto& [a, b] : z.trees[0].edges) {
    EXPECT_GT(std::min((g.row(a) == g.row(b) ? h : v)[a], (g.row(a) == g.row(b) ? h : v)[b]), 0);
  }

  g = grid(6, 1);
  h.assign(g.size(), 1);
  net.endpoints   = {0, 5};
  const auto full = global_route(g, h, h, 1, 0, {net, net}, 0, 0, 0, 1, true);
  EXPECT_EQ(full.pattern_attempts, 2u);
  EXPECT_EQ(full.pattern_completed, 1u);
  EXPECT_EQ(full.pattern_fallbacks, 1u);
  EXPECT_EQ(full.unrouted, 1u);
  EXPECT_EQ(full.overflow_tracks, 0u);
  EXPECT_TRUE(full.trees[1].edges.empty());
}

TEST(LegrouteGlobal, PatternFallbackPreservesMazeDetourAndLeavesNoPartialReservations) {
  const auto       g = grid(9, 9);
  std::vector<int> h(g.size(), 1), v(g.size(), 1);
  for (int c = 1; c < g.cols; ++c) {
    v[size_t(g.tile(c, 4))] = 0;
  }
  Global_net detour, next;
  detour.endpoints    = {g.tile(3, 3), g.tile(3, 6)};
  const auto maze     = global_route(g, h, v, 1, 0, {detour}, 0, 0);
  const auto fallback = global_route(g, h, v, 1, 0, {detour}, 0, 0, 0, 1, true);
  ASSERT_EQ(fallback.pattern_fallbacks, 1u);
  EXPECT_EQ(fallback.pattern_work_limited, 0u);
  EXPECT_EQ(fallback.trees[0].edges, maze.trees[0].edges);
  EXPECT_EQ(fallback.search_pops, maze.search_pops);
  check_tree(g, fallback.trees[0], detour.endpoints);

  detour.crit = 1;
  detour.endpoints.insert(detour.endpoints.begin() + 1, g.tile(4, 3));
  next.endpoints     = {g.tile(3, 3), g.tile(4, 3)};
  const auto bounded = global_route(g, h, v, 1, 0, {detour, next}, 0, 0, 12, 1, true);
  const auto before  = global_route(g, h, v, 1, 0, {detour, next}, 0, 0, 12);
  EXPECT_FALSE(bounded.trees[0].routed);
  EXPECT_TRUE(bounded.trees[0].edges.empty());
  EXPECT_TRUE(bounded.trees[0].search_limited);
  EXPECT_EQ(bounded.trees[1].edges, before.trees[1].edges);
  EXPECT_TRUE(bounded.trees[1].routed);
  EXPECT_EQ(bounded.overflow_tracks, 0u);
  EXPECT_LE(bounded.pattern_work, bounded.pattern_attempts * 12);
  EXPECT_GT(bounded.pattern_work_limited, 0u);
  EXPECT_EQ(bounded.search_pops, before.search_pops);
}

TEST(LegrouteGlobal, PatternWorkLimitDoesNotBecomeAMazeSearchLimit) {
  const auto       g = grid(24, 1);
  std::vector<int> cap(g.size(), 4);
  Global_net       net;
  net.endpoints = {0, 23};
  const auto r  = global_route(g, cap, cap, 1, 0, {net}, 0, 0, 64, 1, true);
  EXPECT_EQ(r.pattern_work_limited, 1u);
  EXPECT_EQ(r.pattern_fallbacks, 1u);
  EXPECT_EQ(r.pattern_work, 64u);
  EXPECT_EQ(r.search_limited, 0u);
  EXPECT_EQ(r.unrouted, 0u);
  EXPECT_GT(r.search_pops, 0u);
  check_tree(g, r.trees[0], net.endpoints);
}

TEST(LegrouteGlobal, PatternBypassesFixedOwnRegionAndLargeFanoutRequests) {
  auto             g = grid(7, 5);
  std::vector<int> cap(g.size(), 4);
  Global_net       net;
  net.endpoints = {0, 20, 34};
  for (int mode = 0; mode < 3; ++mode) {
    if (mode == 0) {
      net.fixed_edges = {
          {0, 1}
      };
    }
    if (mode == 1) {
      net.fixed_edges.clear();
      net.own_regions = {0};
    }
    if (mode == 2) {
      net.own_regions.clear();
      net.endpoints.resize(33);
      std::iota(net.endpoints.begin(), net.endpoints.end(), 0);
    }
    const auto before  = global_route(g, cap, cap, 1, 0, {net}, 0, 0);
    const auto enabled = global_route(g, cap, cap, 1, 0, {net}, 0, 0, 0, 1, true);
    EXPECT_EQ(enabled.pattern_attempts, 0u);
    EXPECT_EQ(enabled.search_pops, before.search_pops);
    EXPECT_EQ(enabled.trees[0].edges, before.trees[0].edges);
  }
}

TEST(LegrouteGlobal, PatternTreesAreAcyclicAndPrunedForArbitraryTerminalTopologies) {
  auto g   = grid(8, 8);
  g.tile_w = 800;
  g.tile_h = 50;
  std::vector<int> cap(g.size(), 8);
  std::mt19937     random(57);
  bool             aspect_changed_tree = false;
  for (int trial = 0; trial < 40; ++trial) {
    Global_net    net;
    std::set<int> endpoints;
    while (endpoints.size() < 8) {
      endpoints.insert(int(random() % g.size()));
    }
    net.endpoints.assign(endpoints.begin(), endpoints.end());
    const auto r = global_route(g, cap, cap, 1, 0, {net}, 0, 0, 0, 1, true);
    ASSERT_EQ(r.pattern_completed, 1u);
    EXPECT_EQ(r.search_pops, 0u);
    EXPECT_LE(r.pattern_work, 100000u);
    check_tree(g, r.trees[0], net.endpoints);
    std::vector<int> degree(g.size());
    for (const auto& [a, b] : r.trees[0].edges) {
      ++degree[a];
      ++degree[b];
    }
    for (size_t t = 0; t < degree.size(); ++t) {
      if (degree[t] == 1) {
        EXPECT_TRUE(endpoints.contains(int(t)));
      }
    }
    auto transposed_metric = g;
    std::swap(transposed_metric.tile_w, transposed_metric.tile_h);
    const auto other = global_route(transposed_metric, cap, cap, 1, 0, {net}, 0, 0, 0, 1, true);
    ASSERT_EQ(other.pattern_completed, 1u);
    aspect_changed_tree |= other.trees[0].edges != r.trees[0].edges;
  }
  EXPECT_TRUE(aspect_changed_tree) << "rectangular physical dimensions must reach the topology builder";
}

TEST(LegrouteGlobal, GrowingTheWindowRetainsPreviouslyConnectedBranches) {
  const auto       g = grid(9, 9);
  std::vector<int> h(g.size(), 4), v(g.size(), 4);
  for (int c = 1; c < g.cols; ++c) {
    v[size_t(g.tile(c, 4))] = 0;  // the sole passage through the horizontal wall is at column zero
  }
  Global_net net;
  net.endpoints = {g.tile(3, 3), g.tile(4, 3), g.tile(3, 6)};
  const auto r  = global_route(g, h, v, 1, 0, {net}, 0, 0);
  ASSERT_EQ(r.unrouted, 0u);
  EXPECT_EQ(r.search_restarts, 3u);
  check_tree(g, r.trees[0], net.endpoints);
  EXPECT_TRUE(std::ranges::any_of(r.trees[0].edges, [&](auto edge) { return edge == std::pair{g.tile(0, 4), g.tile(0, 5)}; }));
}

TEST(LegrouteGlobal, AnUnreachableSinkDoesNotReserveItsPartialTree) {
  const auto       g = grid(7, 7);
  std::vector<int> h(g.size(), 1), v(g.size(), 1);
  for (int c = 0; c < g.cols; ++c) {
    v[size_t(g.tile(c, 3))] = 0;
  }
  Global_net blocked, next;
  blocked.crit      = 1;
  blocked.endpoints = {g.tile(2, 2), g.tile(3, 2), g.tile(2, 5)};
  next.endpoints    = {g.tile(2, 2), g.tile(3, 2)};
  const auto r      = global_route(g, h, v, 1, 0, {blocked, next}, 2, 0);
  EXPECT_EQ(r.unrouted, 1u);
  EXPECT_EQ(r.topology_disconnected, 1u);
  EXPECT_TRUE(r.trees[0].topology_disconnected);
  EXPECT_FALSE(r.trees[0].search_limited);
  EXPECT_EQ(r.trees[0].search_pops, 0u);
  EXPECT_EQ(r.trees[0].last_attempt_pops, 0u);
  EXPECT_EQ(r.trees[0].search_attempts, 1u);
  EXPECT_FALSE(r.trees[0].routed);
  EXPECT_TRUE(r.trees[0].edges.empty());
  check_tree(g, r.trees[1], next.endpoints);
  EXPECT_EQ(r.trees[1].edges.size(), 1u);
  EXPECT_EQ(r.overflow_edges, 0u);
  const auto alone = global_route(g, h, v, 1, 0, {next}, 2, 0);
  EXPECT_EQ(r.trees[1].edges, alone.trees[0].edges);
  EXPECT_EQ(r.search_pops, alone.search_pops);
  EXPECT_EQ(r.search_pushes, alone.search_pushes);
}

TEST(LegrouteGlobal, TopologyPrecheckDistinguishesZeroCapacityFromSearchBudget) {
  const auto       g = grid(80, 60);
  std::vector<int> h(g.size(), 2), v(g.size(), 2);
  for (int c = 0; c < g.cols; ++c) {
    v[size_t(g.tile(c, 30))] = 0;
  }
  Global_net net;
  net.endpoints = {g.tile(2, 2), g.tile(70, 58)};
  for (unsigned rounds : {0u, 4u}) {
    const auto r = global_route(g, h, v, 1, 0, {net}, 1, rounds, 1);
    EXPECT_EQ(r.unrouted, 1u);
    EXPECT_EQ(r.topology_disconnected, 1u);
    EXPECT_EQ(r.search_limited, 0u);
    EXPECT_EQ(r.search_pops, 0u);
    EXPECT_EQ(r.search_restarts, 0u);
    EXPECT_EQ(r.overflow_tracks, 0u);
    EXPECT_TRUE(r.trees[0].edges.empty());
  }
}

TEST(LegrouteGlobal, TopologyPrecheckDoesNotRejectOwnRegionsOrZeroCapacityFixedBridges) {
  const auto       g = grid(3, 1);
  std::vector<int> h(g.size(), 1), v(g.size(), 0);
  Global_net       net;
  net.endpoints     = {0, 2};
  const auto closed = global_route(g, h, v, .5, 0, {net}, 0, 0);
  EXPECT_EQ(closed.topology_disconnected, 1u);  // global share rounds one track to zero
  net.own_regions = {0};
  for (double own_share : {0., .5}) {
    const auto own = global_route(g, h, v, .5, own_share, {net}, 0, 0);
    check_tree(g, own.trees[0], net.endpoints);
    EXPECT_EQ(own.topology_disconnected, 0u);
    EXPECT_GT(own.search_pops, 0u);
  }
  net.own_regions.clear();
  h[1]            = 0;
  net.fixed_edges = {
      {0, 1},
      {1, 2}
  };
  const auto fixed = global_route(g, h, v, 1, 0, {net}, 0, 0);
  check_tree(g, fixed.trees[0], net.endpoints);
  EXPECT_EQ(fixed.topology_disconnected, 0u);
  EXPECT_GT(fixed.search_pops, 0u);
}

TEST(LegrouteGlobal, NegotiationStillRespectsPerEdgeCapacity) {
  const auto       g = grid(3, 2);
  std::vector<int> cap(g.size(), 2);
  Global_net       net;
  net.endpoints = {0, 2};
  // Both rows must be in the initial window: negotiation can use an overflowing path, so it need not
  // trigger the disconnected-search window expansion exercised separately above.
  const auto r  = global_route(g, cap, cap, .5, 0, {net, net}, 1, 4);
  ASSERT_EQ(r.unrouted, 0u);
  EXPECT_EQ(r.overflow_edges, 0u);
  std::set<std::pair<int, int>> used;
  for (const auto& tree : r.trees) {
    check_tree(g, tree, net.endpoints);
    for (const auto& edge : tree.edges) {
      EXPECT_TRUE(used.insert(edge).second) << "two nets exceed the edge's one-track share";
    }
  }
}

TEST(LegrouteGlobal, ZeroCostFixedEdgesDoNotCreateParentCycles) {
  const auto       g = grid(6, 1);
  std::vector<int> cap(g.size(), 4);
  Global_net       net;
  net.endpoints   = {0, 2, 5};
  net.fixed_edges = {
      {0, 1},
      {1, 2},
      {2, 3},
      {3, 4},
      {4, 5}
  };
  const auto r = global_route(g, cap, cap, 1, 0, {net}, 0, 0);
  ASSERT_EQ(r.unrouted, 0u);
  check_tree(g, r.trees[0], net.endpoints);
  EXPECT_EQ(r.trees[0].edges.size(), 5u);
}

TEST(LegrouteGlobal, DistantTwoPinSearchFollowsTheGoal) {
  const auto       g = grid(160, 160);
  std::vector<int> cap(g.size(), 8);
  Global_net       net;
  net.endpoints   = {g.tile(8, 8), g.tile(148, 148)};
  net.own_regions = {0};  // unit-cost interior edges
  const auto r    = global_route(g, cap, cap, 1, 0, {net}, 0, 0);
  ASSERT_EQ(r.unrouted, 0u);
  check_tree(g, r.trees[0], net.endpoints);
  EXPECT_EQ(r.trees[0].edges.size(), 280u);
  RecordProperty("search_pops", int(r.search_pops));
  EXPECT_LT(r.search_pops, 600u);  // Dijkstra visits almost the whole 25,600-tile grid
}

TEST(LegrouteGlobal, RemainingGoalsRekeyAfterEachAttachment) {
  const auto       g = grid(80, 80);
  std::vector<int> cap(g.size(), 8);
  Global_net       net;
  net.own_regions = {0};
  net.endpoints   = {g.tile(10, 10), g.tile(60, 10), g.tile(10, 60), g.tile(60, 60)};
  const auto r    = global_route(g, cap, cap, 1, 0, {net}, 0, 0);
  ASSERT_EQ(r.unrouted, 0u);
  check_tree(g, r.trees[0], net.endpoints);
  EXPECT_EQ(r.trees[0].edges.size(), 150u);
  EXPECT_EQ(r.search_restarts, 1u);
  EXPECT_LT(r.search_pops, 1200u);
}
TEST(LegrouteGlobal, SearchBudgetLeavesNoPartialReservation) {
  const auto       g = grid(20, 20);
  std::vector<int> cap(g.size(), 1);
  Global_net       far, near;
  far.crit       = 1;
  far.endpoints  = {0, 399};
  near.endpoints = {0, 1};
  const auto r   = global_route(g, cap, cap, 1, 0, {far, near}, 0, 0, 4);
  EXPECT_EQ(r.search_limited, 1u);
  EXPECT_EQ(r.unrouted, 1u);
  EXPECT_TRUE(r.trees[0].search_limited);
  EXPECT_TRUE(r.trees[0].edges.empty());
  check_tree(g, r.trees[1], near.endpoints);
  EXPECT_LE(r.search_pops, 8u);
}

TEST(LegrouteGlobal, GuidedPathsMatchIndependentWeightedShortestPaths) {
  auto g    = grid(12, 10);
  g.regions = 3;  // third region has no tiles: permits an exact precheck-bypass control below
  for (size_t t = 0; t < g.size(); ++t) {
    g.region[t] = g.col(int(t)) >= 6;
  }
  std::mt19937 random(71);
  for (int sample = 0; sample < 20; ++sample) {
    std::vector<int> cap(g.size());
    for (int& c : cap) {
      c = int(random() % 7);  // blocked tiles and varying congestion cost
    }
    cap[0] = cap.back() = 4;
    Global_net net;
    net.endpoints = {0, int(g.size()) - 1};
    std::vector<double> distance(g.size(), INFINITY);
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    distance[0] = 0;
    queue.emplace(0, 0);
    const auto cost = [&](int a, int b) {
      const int capacity = std::min(cap[size_t(a)], cap[size_t(b)]);
      return capacity ? 1 + 2.0 / capacity + (g.region[a] != g.region[b]) : INFINITY;
    };
    while (!queue.empty()) {
      const auto [d, t] = queue.top();
      queue.pop();
      if (d != distance[size_t(t)]) {
        continue;
      }
      for (int q : {g.col(t) ? t - 1 : -1,
                    g.col(t) + 1 < g.cols ? t + 1 : -1,
                    g.row(t) ? t - g.cols : -1,
                    g.row(t) + 1 < g.rows ? t + g.cols : -1}) {
        if (q >= 0 && d + cost(t, q) < distance[size_t(q)]) {
          distance[size_t(q)] = d + cost(t, q);
          queue.emplace(distance[size_t(q)], q);
        }
      }
    }
    for (double weight : {1., 2., 4., 8.}) {
      SCOPED_TRACE(weight);
      const auto result = global_route(g, cap, cap, 1, 0, {net}, 20, 0, 0, weight);
      if (!std::isfinite(distance.back())) {
        EXPECT_EQ(result.unrouted, 1u);
        EXPECT_EQ(result.topology_disconnected, 1u);
        EXPECT_EQ(result.search_pops, 0u);
        EXPECT_EQ(result.search_limited, 0u);
        EXPECT_FALSE(result.trees[0].routed);
        EXPECT_TRUE(result.trees[0].edges.empty());
        continue;
      }
      ASSERT_EQ(result.unrouted, 0u);
      auto bypass          = net;
      bypass.own_regions   = {2};  // unused region changes no search edge predicate or cost
      const auto unchanged = global_route(g, cap, cap, 1, 0, {bypass}, 20, 0, 0, weight);
      EXPECT_EQ(result.trees[0].edges, unchanged.trees[0].edges);
      EXPECT_EQ(result.search_pops, unchanged.search_pops);
      EXPECT_EQ(result.search_pushes, unchanged.search_pushes);
      EXPECT_EQ(result.search_restarts, unchanged.search_restarts);
      EXPECT_EQ(result.overflow_edges, 0u);
      check_tree(g, result.trees[0], net.endpoints);
      double actual = 0;
      for (const auto& [a, b] : result.trees[0].edges) {
        ASSERT_TRUE(std::isfinite(cost(a, b))) << "route crosses a blocked tile";
        actual += cost(a, b);
      }
      if (weight == 1) {
        EXPECT_NEAR(actual, distance.back(), 1e-9);
      } else {
        EXPECT_LE(actual, weight * distance.back() + 1e-9);
      }
    }
  }
}

TEST(LegrouteGlobal, WeightedSearchReducesExpansionsWithoutChangingDefault) {
  const auto       g = grid(160, 160);
  std::vector<int> cap(g.size(), 8);
  Global_net       net;
  net.endpoints               = {g.tile(8, 8), g.tile(148, 148)};
  // Counted interior edges cost 1.25. Unit Manhattan is conservative and can
  // expand much of the grid even though an unobstructed route is available.
  const auto baseline         = global_route(g, cap, cap, 1, 0, {net}, 0, 0);
  const auto explicit_default = global_route(g, cap, cap, 1, 0, {net}, 0, 0, 0, 1);
  EXPECT_EQ(baseline.trees[0].edges, explicit_default.trees[0].edges);
  EXPECT_EQ(baseline.search_pops, explicit_default.search_pops);
  EXPECT_EQ(baseline.search_pushes, explicit_default.search_pushes);
  for (double weight : {2., 4., 8.}) {
    const auto result = global_route(g, cap, cap, 1, 0, {net}, 0, 0, 0, weight);
    ASSERT_EQ(result.unrouted, 0u);
    EXPECT_EQ(result.overflow_tracks, 0u);
    check_tree(g, result.trees[0], net.endpoints);
    EXPECT_EQ(result.trees[0].edges.size(), 280u);
    EXPECT_LT(result.search_pops, baseline.search_pops / 4);
  }
}

TEST(LegrouteGlobal, WeightedSearchKeepsZeroCostFixedEdgesAndBroadcastsUnguided) {
  const auto       g = grid(40, 1);
  std::vector<int> cap(g.size(), 4);
  Global_net       fixed, broadcast;
  fixed.endpoints = {0, 20, 39};
  for (int t = 0; t < 39; ++t) {
    fixed.fixed_edges.emplace_back(t, t + 1);
  }
  broadcast.endpoints.resize(40);
  std::iota(broadcast.endpoints.begin(), broadcast.endpoints.end(), 0);
  for (const auto& net : {fixed, broadcast}) {
    const auto baseline = global_route(g, cap, cap, 1, 0, {net}, 0, 0);
    for (double weight : {2., 4., 8.}) {
      const auto result = global_route(g, cap, cap, 1, 0, {net}, 0, 0, 0, weight);
      check_tree(g, result.trees[0], net.endpoints);
      EXPECT_EQ(result.trees[0].edges, baseline.trees[0].edges);
      EXPECT_EQ(result.search_pops, baseline.search_pops);
      EXPECT_EQ(result.search_pushes, baseline.search_pushes);
    }
  }
}

TEST(LegrouteGlobal, WeightedSearchRetainsHardCapacityAndBudgetFailures) {
  const auto       g = grid(20, 1);
  std::vector<int> cap(g.size(), 1);
  Global_net       far, near;
  far.crit       = 1;
  far.endpoints  = {0, 19};
  near.endpoints = {0, 1};
  for (double weight : {1., 2., 4., 8.}) {
    const auto bounded = global_route(g, cap, cap, 1, 0, {far, near}, 0, 0, 4, weight);
    EXPECT_EQ(bounded.search_limited, 1u);
    EXPECT_EQ(bounded.topology_disconnected, 0u);
    EXPECT_EQ(bounded.unrouted, 1u);
    EXPECT_TRUE(bounded.trees[0].edges.empty());
    check_tree(g, bounded.trees[1], near.endpoints);
    EXPECT_EQ(bounded.overflow_tracks, 0u);
    const auto full = global_route(g, cap, cap, 1, 0, {far, near}, 0, 0, 0, weight);
    check_tree(g, full.trees[0], far.endpoints);
    EXPECT_EQ(full.unrouted, 1u);
    EXPECT_EQ(full.topology_disconnected, 0u);  // occupancy closes edges, but the static graph stays connected
    EXPECT_EQ(full.search_limited, 0u);
    EXPECT_FALSE(full.trees[1].routed);
    EXPECT_TRUE(full.trees[1].edges.empty());
    EXPECT_EQ(full.overflow_tracks, 0u);
  }
}

TEST(LegrouteGlobal, InvalidWeightsAreRejectedBeforeSearching) {
  const auto       g = grid(1, 1);
  std::vector<int> cap(1, 1);
  for (double weight : {0., .5, double(INFINITY), double(NAN)}) {
    EXPECT_THROW(global_route(g, cap, cap, 1, 0, {}, 0, 0, 0, weight), std::invalid_argument);
    Legroute_options options;
    options.global_astar_weight = weight;
    EXPECT_THROW(validate_legroute_options(options), std::invalid_argument);
  }
}

TEST(LegrouteGlobal, PerNetWorkCountsAllAttemptsAndKeepsLastAttemptSeparate) {
  const auto       g = grid(2, 1);
  std::vector<int> cap(g.size(), 1);
  Global_net       net;
  net.endpoints     = {0, 1};
  const auto result = global_route(g, cap, cap, 1, 0, {net, net}, 0, 3);
  EXPECT_EQ(result.unrouted, 0u);
  EXPECT_EQ(result.overflow_tracks, 1u);
  EXPECT_EQ(result.rounds, 3u);
  size_t total = 0;
  for (const auto& tree : result.trees) {
    EXPECT_EQ(tree.search_attempts, 4u);
    EXPECT_EQ(tree.last_attempt_pops, 2u);
    EXPECT_EQ(tree.search_pops, 8u);
    total += tree.search_pops;
  }
  EXPECT_EQ(result.search_pops, total);
  const auto limited = global_route(g, cap, cap, 1, 0, {net}, 0, 3, 1);
  EXPECT_EQ(limited.unrouted, 1u);
  EXPECT_EQ(limited.trees[0].last_attempt_pops, 1u);
  EXPECT_EQ(limited.trees[0].search_pops, 1u);
  EXPECT_EQ(limited.trees[0].search_attempts, 1u);
  EXPECT_TRUE(limited.trees[0].search_limited);
  EXPECT_TRUE(limited.trees[0].edges.empty());
}
}  // namespace livehd::placement::lr
