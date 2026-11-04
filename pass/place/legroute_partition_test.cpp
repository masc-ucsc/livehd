// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <stdexcept>

#include "gtest/gtest.h"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
struct Partition_fixture {
  Grid                g;
  std::vector<size_t> cells;
  std::vector<int>    cap;
  Global_routes       routes;
  std::vector<char>   critical;
  Partition_fixture(int cols, int rows) : cells(size_t(cols * rows), 1), cap(size_t(cols * rows), 2) {
    g.cols = cols;
    g.rows = rows;
  }
  void tree(std::vector<std::pair<int, int>> edges, bool protect = false) {
    routes.trees.push_back({true, std::move(edges)});
    critical.push_back(protect);
  }
  Route_partition cut(unsigned target) const { return partition_routes(g, cells, routes, critical, cap, cap, 1, target); }
};
}  // namespace

TEST(LegroutePartition, BalancedRectanglesCoverEveryTile) {
  Partition_fixture f(8, 4);
  const auto        p = f.cut(4);
  ASSERT_EQ(p.regions, 8);
  EXPECT_EQ(p.oversized, 0u);
  EXPECT_EQ(p.max_cells, 4u);
  EXPECT_EQ(p.indivisible_cells, 1u);
  for (int id = 0; id < p.regions; ++id) {
    int    c0 = f.g.cols, c1 = -1, r0 = f.g.rows, r1 = -1;
    size_t count = 0;
    for (size_t t = 0; t < p.region.size(); ++t) {
      ASSERT_GE(p.region[t], 0);
      ASSERT_LT(p.region[t], p.regions);
      if (p.region[t] == id) {
        c0 = std::min(c0, f.g.col(int(t)));
        c1 = std::max(c1, f.g.col(int(t)));
        r0 = std::min(r0, f.g.row(int(t)));
        r1 = std::max(r1, f.g.row(int(t)));
        ++count;
      }
    }
    EXPECT_EQ(count, 4u);
    EXPECT_EQ(size_t((c1 - c0 + 1) * (r1 - r0 + 1)), count);
  }
}

TEST(LegroutePartition, RefinesOnlySelectedConnectedRegionsAndKeepsExistingLabels) {
  Partition_fixture f(8, 4);
  f.g.regions = 2;
  f.g.region.resize(f.g.size());
  for (size_t t = 0; t < f.g.size(); ++t) {
    // A connected staircase boundary; the requested region is not a rectangle.
    f.g.region[t] = f.g.col(int(t)) < 3 + f.g.row(int(t)) / 2 ? 0 : 1;
  }
  const auto before  = f.g.region;
  const auto cuts    = route_cuts(f.g, f.routes, f.critical, f.cap, f.cap, 1);
  const auto refined = refine_route_regions(f.g, f.cells, cuts, 0, {0, 4});
  EXPECT_EQ(f.g.region, before);
  EXPECT_TRUE(connected_regions(f.g, refined));
  EXPECT_TRUE(cuts.allows(f.g, refined));
  std::map<int, size_t> counts;
  for (size_t t = 0; t < f.g.size(); ++t) {
    if (!before[t]) {
      EXPECT_EQ(refined[t], 0);
    } else {
      EXPECT_GE(refined[t], 1);
      ++counts[refined[t]];
    }
  }
  ASSERT_GT(counts.size(), 1u);
  EXPECT_TRUE(counts.contains(1));
  for (const auto& [label, count] : counts) {
    EXPECT_LE(count, 4u) << label;
  }
  EXPECT_EQ(refine_route_regions(f.g, f.cells, cuts, 0, {0, 0}), before);
}

TEST(LegroutePartition, RefinementRetainsProtectedAndOverloadedEdges) {
  Partition_fixture f(4, 2);
  f.g.regions = 2;
  f.g.region  = {0, 0, 1, 1, 0, 0, 1, 1};
  f.tree(
      {
          {0, 1},
          {0, 4},
          {4, 5}
  },
      true);
  auto cuts = route_cuts(f.g, f.routes, f.critical, f.cap, f.cap, 1);
  EXPECT_EQ(refine_route_regions(f.g, f.cells, cuts, -1, {1, 0}), f.g.region);
  cuts         = route_cuts(f.g, f.routes, f.critical, f.cap, f.cap, 1, false);
  auto refined = refine_route_regions(f.g, f.cells, cuts, 4, {1, 0});
  EXPECT_NE(refined, f.g.region);
  EXPECT_NE(refined[0], refined[1]);
  f.tree({
      {0, 1}
  });
  f.tree({
      {0, 1}
  });
  cuts    = route_cuts(f.g, f.routes, f.critical, f.cap, f.cap, 1, false);
  refined = refine_route_regions(f.g, f.cells, cuts, 4, {1, 0});
  EXPECT_EQ(refined[0], refined[1]);
  EXPECT_TRUE(connected_regions(f.g, refined));
  EXPECT_TRUE(cuts.allows(f.g, refined));
  for (int t : {2, 3, 6, 7}) {
    EXPECT_EQ(refined[size_t(t)], 1);
  }
}

TEST(LegroutePartition, RectangularRefinementMatchesAFinerInitialPartition) {
  Partition_fixture f(8, 4);
  const auto        coarse = f.cut(8);
  f.g.region               = coarse.region;
  f.g.regions              = coarse.regions;
  const auto cuts          = route_cuts(f.g, f.routes, f.critical, f.cap, f.cap, 1);
  const auto refined       = refine_route_regions(f.g, f.cells, cuts, -1, std::vector<unsigned>(size_t(f.g.regions), 4));
  const auto fine          = f.cut(4);
  // Refinement retains existing IDs; compare ownership rather than numbering.
  for (size_t a = 0; a < f.g.size(); ++a) {
    for (size_t b = 0; b < f.g.size(); ++b) {
      EXPECT_EQ(refined[a] == refined[b], fine.region[a] == fine.region[b]);
    }
  }
  const auto mesh = refine_route_regions(f.g, f.cells, cuts, -1, {0, 4, 0, 0}, true);
  EXPECT_TRUE(connected_regions(f.g, mesh));
  EXPECT_TRUE(cuts.allows(f.g, mesh));
  EXPECT_NE(mesh, coarse.region);
  for (size_t t = 0; t < f.g.size(); ++t) {
    if (coarse.region[t] != 1) {
      EXPECT_EQ(mesh[t], coarse.region[t]);
    }
  }
}

TEST(LegroutePartition, RefinementRejectsInvalidPartitionsAndTargetCounts) {
  Partition_fixture f(2, 2);
  f.g.regions     = 2;
  f.g.region      = {0, 0, 1, 1};
  const auto cuts = route_cuts(f.g, f.routes, f.critical, f.cap, f.cap, 1);
  EXPECT_THROW(refine_route_regions(f.g, f.cells, cuts, 0, {1}), std::invalid_argument);
  EXPECT_THROW(refine_route_regions(f.g, f.cells, cuts, -2, {1, 0}), std::invalid_argument);
  f.g.region = {0, 1, 1, 0};
  EXPECT_THROW(refine_route_regions(f.g, f.cells, cuts, 0, {1, 0}), std::invalid_argument);
  f.g.region = {0, 0, 2, 2};
  EXPECT_THROW(refine_route_regions(f.g, f.cells, cuts, 0, {1, 0}), std::invalid_argument);
}

TEST(LegroutePartition, OneOverusedEdgeForbidsTheWholeCut) {
  Partition_fixture f(2, 2);
  f.cells = {2, 2, 0, 0};  // only a vertical cut can separate cells
  for (int k = 0; k < 3; ++k) {
    f.tree({
        {0, 1}
    });
  }
  // Total load 3 fits the line's 4 tracks, but its bottom edge has only 2.
  auto p = f.cut(2);
  EXPECT_EQ(p.regions, 1);
  EXPECT_EQ(p.oversized, 1u);
  EXPECT_EQ(p.max_cells, 4u);
  f.cap[0] = f.cap[1] = 3;
  p                   = f.cut(2);
  EXPECT_EQ(p.regions, 2);
  EXPECT_EQ(p.oversized, 0u);
}

TEST(LegroutePartition, ProtectedTreeForcesAnOversizedRegion) {
  Partition_fixture f(4, 1);
  f.tree(
      {
          {0, 1},
          {1, 2},
          {2, 3}
  },
      true);
  auto p = f.cut(2);
  EXPECT_EQ(p.regions, 1);
  EXPECT_EQ(p.oversized, 1u);
  EXPECT_EQ(p.max_cells, 4u);
  EXPECT_EQ(p.indivisible_cells, 4u);
  f.critical[0] = false;
  p             = f.cut(2);
  EXPECT_EQ(p.regions, 2);
  EXPECT_EQ(p.max_cells, 2u);
  EXPECT_EQ(p.indivisible_cells, 1u);
}

TEST(LegroutePartition, AvoidsProtectedEdgeWhenAnotherCutFits) {
  Partition_fixture f(4, 1);
  f.tree(
      {
          {1, 2}
  },
      true);
  const auto p = f.cut(2);
  EXPECT_EQ(p.regions, 3);
  EXPECT_EQ(p.region[1], p.region[2]);
  EXPECT_NE(p.region[0], p.region[1]);
  EXPECT_NE(p.region[3], p.region[2]);
  EXPECT_EQ(p.oversized, 0u);
}

TEST(LegroutePartition, CountsCapacityShareBeforeAdmittingCuts) {
  Partition_fixture f(2, 1);
  f.tree({
      {0, 1}
  });
  f.tree({
      {0, 1}
  });
  const auto p = partition_routes(f.g, f.cells, f.routes, f.critical, f.cap, f.cap, .5, 1);
  EXPECT_EQ(p.regions, 1);
  EXPECT_EQ(p.oversized, 1u);
}

TEST(LegroutePartition, RepairCannotIntroduceProtectedOrOverusedCuts) {
  Partition_fixture f(2, 2);
  f.tree(
      {
          {0, 1}
  },
      true);
  auto cuts = route_cuts(f.g, f.routes, f.critical, f.cap, f.cap, 1);
  EXPECT_TRUE(cuts.allows(f.g, {0, 0, 1, 1}));
  EXPECT_FALSE(cuts.allows(f.g, {0, 1, 0, 1}));
  f.critical[0] = false;
  cuts          = route_cuts(f.g, f.routes, f.critical, f.cap, f.cap, 1);
  EXPECT_TRUE(cuts.allows(f.g, {0, 1, 0, 1}));
  f.tree({
      {0, 1}
  });
  f.tree({
      {0, 1}
  });
  cuts = route_cuts(f.g, f.routes, f.critical, f.cap, f.cap, 1);
  EXPECT_FALSE(cuts.allows(f.g, {0, 1, 0, 1}));
  EXPECT_TRUE(cuts.allows(f.g, {0, 0, 1, 1}));
}

TEST(LegroutePartition, PenaltyPrefersAnUnbalancedCutThatAvoidsCriticalCrossings) {
  Partition_fixture f(4, 1);
  std::fill(f.cap.begin(), f.cap.end(), 100);
  f.tree(
      {
          {1, 2}
  },
      true);
  const auto free = partition_routes(f.g, f.cells, f.routes, f.critical, f.cap, f.cap, 1, 3, 0);
  EXPECT_NE(free.region[1], free.region[2]);
  EXPECT_EQ(free.critical_crossings, 1u);
  const auto costly = partition_routes(f.g, f.cells, f.routes, f.critical, f.cap, f.cap, 1, 3, 100);
  EXPECT_EQ(costly.region[1], costly.region[2]);
  EXPECT_EQ(costly.critical_crossings, 0u);
  EXPECT_EQ(costly.max_cells, 3u);
  EXPECT_EQ(costly.oversized, 0u);
}

TEST(LegroutePartition, PenaltyAllowsACriticalCrossingToMeetTheCellTarget) {
  Partition_fixture f(4, 1);
  f.tree(
      {
          {0, 1},
          {1, 2},
          {2, 3}
  },
      true);
  const auto p = partition_routes(f.g, f.cells, f.routes, f.critical, f.cap, f.cap, 1, 2, 10);
  EXPECT_EQ(p.regions, 2);
  EXPECT_EQ(p.max_cells, 2u);
  EXPECT_EQ(p.oversized, 0u);
  EXPECT_EQ(p.indivisible_cells, 1u);
  EXPECT_EQ(p.crossings, 1u);
  EXPECT_EQ(p.critical_crossings, 1u);
}

TEST(LegroutePartition, PenaltyNeverRelaxesEdgeCapacity) {
  Partition_fixture f(2, 1);
  for (int n = 0; n < 3; ++n) {
    f.tree(
        {
            {0, 1}
    },
        true);
  }
  const auto p = partition_routes(f.g, f.cells, f.routes, f.critical, f.cap, f.cap, 1, 1, 10);
  EXPECT_EQ(p.regions, 1);
  EXPECT_EQ(p.oversized, 1u);
  EXPECT_EQ(p.indivisible_cells, 2u);
}

TEST(LegroutePartition, MissingRoutesCannotCertifyBoundaryCapacity) {
  Partition_fixture f(2, 2);
  f.routes.unrouted = 1;
  EXPECT_THROW(f.cut(2), std::invalid_argument);
  f.routes.unrouted = 0;
  f.tree({
      {1, 2}
  });  // wraps to the next row, not an adjacent edge
  EXPECT_THROW(f.cut(2), std::invalid_argument);
}

TEST(LegroutePartition, MeshTilesTwoInterlockingLShapesWithoutCuttingTheirTrees) {
  Partition_fixture f(3, 2);
  f.tree(
      {
          {0, 1},
          {0, 3}
  },
      true);
  f.tree(
      {
          {2, 5},
          {4, 5}
  },
      true);
  EXPECT_EQ(f.cut(3).oversized, 1u) << "every straight line cuts one of the L-shaped trees";
  for (double cost : {-1.0, 10.0}) {
    const auto p = partition_routes(f.g, f.cells, f.routes, f.critical, f.cap, f.cap, 1, 3, cost, true);
    ASSERT_EQ(p.regions, 2);
    EXPECT_EQ(p.max_cells, 3u);
    EXPECT_EQ(p.oversized, 0u);
    EXPECT_TRUE(connected_regions(f.g, p.region));
    EXPECT_EQ(p.region[0], p.region[1]);
    EXPECT_EQ(p.region[0], p.region[3]);
    EXPECT_EQ(p.region[2], p.region[4]);
    EXPECT_EQ(p.region[2], p.region[5]);
    EXPECT_NE(p.region[0], p.region[2]);
    EXPECT_EQ(p.critical_crossings, 0u);
  }
}

TEST(LegroutePartition, MeshRegionsCoverTheGridAndMeetTheCellTarget) {
  Partition_fixture f(8, 4);
  const auto        p = partition_routes(f.g, f.cells, f.routes, f.critical, f.cap, f.cap, 1, 4, 4, true);
  EXPECT_EQ(p.oversized, 0u);
  EXPECT_LE(p.max_cells, 4u);
  EXPECT_TRUE(connected_regions(f.g, p.region));
  ASSERT_EQ(p.region.size(), f.g.size());
  for (int r : p.region) {
    EXPECT_GE(r, 0);
    EXPECT_LT(r, p.regions);
  }
}

TEST(LegroutePartition, ADisconnectedRepairIsRejected) {
  Partition_fixture f(2, 2);
  EXPECT_TRUE(connected_regions(f.g, {0, 0, 1, 1}));
  EXPECT_FALSE(connected_regions(f.g, {0, 1, 1, 0}));
  EXPECT_FALSE(connected_regions(f.g, {0, -1, 1, 1}));
}
}  // namespace livehd::placement::lr
