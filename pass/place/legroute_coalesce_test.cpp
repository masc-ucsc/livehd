// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <set>
#include <tuple>

#include "gtest/gtest.h"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
using Edge = std::tuple<int, int, int>;

Grid grid(int cols, int rows) {
  Grid g;
  g.cols = cols;
  g.rows = rows;
  g.region.assign(g.size(), 0);
  return g;
}

// Independent unit-step oracle, separate from the implementation's two sweeps.
std::set<Edge> edges(const Grid& g, std::span<const Route_piece> pieces) {
  std::set<Edge> result;
  for (const auto& p : pieces) {
    int       tile   = p.ta;
    const int corner = p.h_first ? g.tile(g.col(p.tb), g.row(p.ta)) : g.tile(g.col(p.ta), g.row(p.tb));
    for (int goal : {corner, p.tb}) {
      while (tile != goal) {
        const int next = g.row(tile) == g.row(goal) ? tile + (goal > tile ? 1 : -1) : tile + (goal > tile ? g.cols : -g.cols);
        result.emplace(p.net, std::min(tile, next), std::max(tile, next));
        tile = next;
      }
    }
  }
  return result;
}

std::set<std::pair<int, int>> endpoints(std::span<const Route_piece> pieces) {
  std::set<std::pair<int, int>> result;
  for (const auto& p : pieces) {
    result.emplace(p.net, p.ta);
    result.emplace(p.net, p.tb);
  }
  return result;
}
}  // namespace

TEST(LegrouteCoalesce, StraightTransitTilesDoNotBecomePhysicalAttachments) {
  const auto               g = grid(7, 1);
  std::vector<Route_piece> original;
  for (int t = 0; t < 6; ++t) {
    original.push_back({0, t, t + 1, true});
  }
  const auto result = coalesce_region_routes(g,
                                             0,
                                             original,
                                             {
                                                 {0, 0},
                                                 {0, 6}
  });
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].ta, 0);
  EXPECT_EQ(result[0].tb, 6);
  EXPECT_EQ(edges(g, result), edges(g, original));
  EXPECT_EQ(endpoints(result),
            (std::set<std::pair<int, int>>{
                {0, 0},
                {0, 6}
  }));
  // L5 creates risers from connection endpoints. No pinless transit tile
  // remains an endpoint in this accepted physical representation.
}

TEST(LegrouteCoalesce, ReversedAndOverlappingSameNetRunsHaveOneExactUnion) {
  const auto                     g = grid(7, 1);
  const std::vector<Route_piece> original{
      {0, 6, 2, true},
      {0, 0, 4, true},
      {0, 3, 2, true},
      {1, 5, 1, true}
  };
  const auto result = coalesce_region_routes(g, 0, original, {});
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(edges(g, result), edges(g, original));
  EXPECT_EQ(endpoints(result),
            (std::set<std::pair<int, int>>{
                {0, 0},
                {0, 6},
                {1, 1},
                {1, 5}
  }));
}

TEST(LegrouteCoalesce, BranchesBendsAndSelectedPinSitesStayExplicit) {
  const auto                     g = grid(7, 4);
  const std::vector<Route_piece> original{
      {0,  0,  6,  true},
      {0,  3, 24, false},
      {0, 24, 27,  true}
  };
  const auto result = coalesce_region_routes(g,
                                             0,
                                             original,
                                             {
                                                 {0,  1},
                                                 {0,  6},
                                                 {0, 27}
  });
  EXPECT_EQ(edges(g, result), edges(g, original));
  const auto sites = endpoints(result);
  for (int tile : {0, 1, 3, 6, 24, 27}) {
    EXPECT_TRUE(sites.contains({0, tile})) << tile;
  }
  for (int tile : {2, 4, 5, 10, 17, 25, 26}) {
    EXPECT_FALSE(sites.contains({0, tile})) << tile;
  }
  EXPECT_EQ(result.size(), 5u);
}

TEST(LegrouteCoalesce, CrossingSitesStayExplicitOnBothRegionalSides) {
  auto g    = grid(5, 2);
  g.regions = 2;
  for (int t = 5; t < 10; ++t) {
    g.region[size_t(t)] = 1;
  }
  const std::vector<Route_piece> top{
      {0, 0, 4, true}
  },
      bottom{{0, 5, 9, true}}, crossing{{0, 2, 7, false}};
  const std::set<std::pair<int, int>> protected_nodes{
      {0, 2},
      {0, 7}
  };
  auto       result = coalesce_region_routes(g, 0, top, protected_nodes);
  const auto lower  = coalesce_region_routes(g, 1, bottom, protected_nodes);
  ASSERT_EQ(result.size(), 2u);
  ASSERT_EQ(lower.size(), 2u);
  EXPECT_TRUE(endpoints(result).contains({0, 2}));
  EXPECT_TRUE(endpoints(lower).contains({0, 7}));
  result.insert(result.end(), lower.begin(), lower.end());
  result.insert(result.end(), crossing.begin(), crossing.end());
  std::vector<Route_piece> original{top.front(), bottom.front(), crossing.front()};
  EXPECT_EQ(edges(g, result), edges(g, original));
  EXPECT_THROW(coalesce_region_routes(g, 0, crossing, protected_nodes), std::logic_error);
}

TEST(LegrouteCoalesce, ZeroLengthAndIsolatedEndpointsSurvive) {
  const auto                     g = grid(6, 2);
  const std::vector<Route_piece> original{
      {0, 0, 5,  true},
      {0, 2, 2,  true},
      {1, 8, 8, false},
      {1, 8, 8,  true}
  };
  const auto result = coalesce_region_routes(g,
                                             0,
                                             original,
                                             {
                                                 {1, 8}
  });
  EXPECT_EQ(edges(g, result), edges(g, original));
  EXPECT_TRUE(endpoints(result).contains({0, 2}));
  EXPECT_TRUE(endpoints(result).contains({1, 8}));
  EXPECT_EQ(std::ranges::count_if(result, [](const auto& piece) { return piece.ta == piece.tb; }), 1);
  EXPECT_EQ(result.size(), 3u);
  EXPECT_TRUE(coalesce_region_routes(g, 0, {}, {}).empty());
}

TEST(LegrouteCoalesce, MeshHoleAndMalformedEndpointsCannotChangeTheProblem) {
  auto g      = grid(3, 3);
  g.regions   = 2;
  g.region[4] = 1;
  const std::vector<Route_piece> through_hole{
      {0, 3, 5, true}
  },
      invalid{{0, -1, 0, true}}, invalid_net{{-1, 0, 1, true}};
  EXPECT_THROW(coalesce_region_routes(g, 0, through_hole, {}), std::logic_error);
  EXPECT_THROW(coalesce_region_routes(g, 0, invalid, {}), std::logic_error);
  EXPECT_THROW(coalesce_region_routes(g, 0, invalid_net, {}), std::logic_error);
}

TEST(LegrouteCoalesce, EverySmallMeshSubgraphPreservesItsExactEdgesAndProtectedSites) {
  const auto                             g = grid(3, 2);
  const std::vector<std::pair<int, int>> possible{
      {0, 1},
      {1, 2},
      {3, 4},
      {4, 5},
      {0, 3},
      {1, 4},
      {2, 5}
  };
  for (unsigned mask = 0; mask < (1u << possible.size()); ++mask) {
    std::vector<Route_piece>      original;
    std::set<std::pair<int, int>> occupied;
    for (size_t k = 0; k < possible.size(); ++k) {
      if (mask & (1u << k)) {
        const auto [a, b] = possible[k];
        original.push_back({0, b, a, true});
        occupied.emplace(0, a);
        occupied.emplace(0, b);
      }
    }
    const std::set<std::pair<int, int>> protected_nodes{
        {0, 1},
        {0, 4}
    };
    const auto result = coalesce_region_routes(g, 0, original, protected_nodes);
    EXPECT_EQ(edges(g, result), edges(g, original)) << mask;
    for (const auto& site : protected_nodes) {
      if (occupied.contains(site)) {
        EXPECT_TRUE(endpoints(result).contains(site)) << mask;
      }
    }
  }
}
}  // namespace livehd::placement::lr
