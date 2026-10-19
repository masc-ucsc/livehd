// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "color_centroids.hpp"

#include <limits>
#include <stdexcept>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
dp::Design fixture() {
  dp::Design d;
  d.layout               = {0, 0, 1000, 1000, 1, 1};
  d.num_movable          = 5;
  d.node_x               = {0, 0, 0, 0, 0, 0, 0};
  d.node_y               = d.node_x;
  d.node_size_x          = {50, 40, 41, 1, 1, 50, 1};
  d.node_size_y          = {100, 100, 100, 1, 1, 100, 1};
  d.node_macro           = {1, 1, 1, 0, 0, 1, 0};
  d.centroid_node_area   = {5000, 4000, 4100, 1, 1, 5000, 1};
  d.node_centroid_color  = {0, 1, 2, 0, 2, 3, 4};
  d.centroid_color_names = {"eligible", "at_threshold", "hierarchy_owned", "fixed_macro", "logic_only"};
  dp::CentroidGroup g;
  g.name              = "top.block";
  g.colors            = {2};
  g.copies_per_parent = 4;
  d.centroid_groups   = {g};
  d.build_maps();
  d.validate();
  return d;
}
}  // namespace

TEST(ColorCentroids, PhysicalAreaAndHierarchyOwnership) {
  auto d = fixture();
  add_color_centroids(d, 4000);
  d.validate();
  ASSERT_EQ(d.centroid_groups.size(), 2);
  const auto& g = d.centroid_groups.back();
  EXPECT_EQ(g.name, "@color:eligible");
  EXPECT_EQ(g.colors, std::vector<int>({0}));
  EXPECT_TRUE(g.nodes.empty());
  EXPECT_EQ(g.scope_macros, 1);
  EXPECT_EQ(g.scope_cells, 2);
  EXPECT_EQ(g.equivalent_cells, 5001);
  // The owned color also has a member outside the hierarchy candidate; the
  // complete color must still be excluded from fallback candidate selection.
  EXPECT_EQ(d.centroid_groups.front().colors, std::vector<int>({2}));
  const auto summary = dp::detail::centroid_summary(d);
  EXPECT_EQ(summary.back().area, 5001);
  add_color_centroids(d, 4000);
  EXPECT_EQ(d.centroid_groups.size(), 2);

  d = fixture();
  add_color_centroids(d, 3999);
  ASSERT_EQ(d.centroid_groups.size(), 3);
  EXPECT_EQ(d.centroid_groups.back().colors, std::vector<int>({1}));
  d = fixture();
  add_color_centroids(d, 0);
  EXPECT_EQ(d.centroid_groups.size(), 3);  // No fixed-only or logic-only group.
  EXPECT_THROW(add_color_centroids(d, -1), std::invalid_argument);
  EXPECT_THROW(add_color_centroids(d, std::numeric_limits<double>::infinity()), std::invalid_argument);
}

TEST(ColorCentroids, RepulsionUsesCandidateParentInsteadOfType) {
  auto d = fixture();
  add_color_centroids(d, 3999);
  ASSERT_EQ(d.centroid_groups.size(), 3);
  EXPECT_NE(d.centroid_groups[0].family, d.centroid_groups[1].family);
  for (const auto& g : d.centroid_groups) {
    EXPECT_EQ(g.repulsion_scope, 0);
  }
  const auto force = dp::detail::centroid_repulsion(d, 1, .5);
  EXPECT_GT(force.value, 0);
  EXPECT_EQ(dp::detail::centroid_circle_overlaps(d, .5).pairs, 3);
  // One child and two root candidates: only the two roots may repel, even
  // though the child shares its original type family with one root.
  d.centroid_groups[2].parent          = 0;
  d.centroid_groups[2].repulsion_scope = 1;
  d.validate();
  EXPECT_EQ(dp::detail::centroid_circle_overlaps(d, .5).pairs, 1);
  const auto nested = dp::detail::centroid_repulsion(d, 1, .5);
  EXPECT_EQ(nested.x[1], 0);
  EXPECT_EQ(nested.y[1], 0);
  // Default native behavior is retained when explicit scopes are absent.
  for (auto& g : d.centroid_groups) {
    g.repulsion_scope = -1;
  }
  EXPECT_EQ(dp::detail::centroid_circle_overlaps(d, .5).pairs, 1);
}

TEST(ColorCentroids, UniqueColorsBypassSnapExactly) {
  const dp::Layout                       layout{0, 0, 1000, 1000, 1, 1};
  const std::vector<dp::FloorplanCircle> circles = {
      {     "a", 100, 100, 10, 100, 0, -1},
      {     "b", 101, 400, 10, 100, 0, -1},
      {"color0", 200, 100, 10, 100, 1, -1},
      {"color1", 201, 400, 10, 100, 1, -1}
  };
  const auto snap = snap_centroid_targets(circles, 2, layout, {});
  ASSERT_EQ(snap.circles.size(), 4);
  ASSERT_FALSE(snap.relations.empty());
  EXPECT_NEAR(snap.circles[0].x, snap.circles[1].x, 1e-5);
  for (size_t i = 2; i < circles.size(); ++i) {
    EXPECT_EQ(snap.circles[i].x, circles[i].x);
    EXPECT_EQ(snap.circles[i].y, circles[i].y);
  }
  for (const auto& relation : snap.relations) {
    for (int node : relation.nodes) {
      EXPECT_LT(node, 2);
    }
  }
  const auto colors_only = snap_centroid_targets(circles, 0, layout, {});
  EXPECT_TRUE(colors_only.relations.empty());
  EXPECT_EQ(colors_only.attempted, 0);
  for (size_t i = 0; i < circles.size(); ++i) {
    EXPECT_EQ(colors_only.circles[i].x, circles[i].x);
    EXPECT_EQ(colors_only.circles[i].y, circles[i].y);
  }
  EXPECT_THROW(snap_centroid_targets(circles, 5, layout, {}), std::invalid_argument);
}

TEST(ColorCentroids, PlacementLeavesReserveAreaWithoutAttractingIt) {
  dp::Design d;
  d.layout      = {0, 0, 100, 100, 1, 1};
  d.num_movable = 6;
  d.node_x      = {10, 60, 12, 25, 30, 70};
  d.node_y      = {10, 60, 12, 25, 30, 70};
  d.node_size_x = d.node_size_y = {10, 10, 1, 1, 1, 1};
  d.node_macro                  = {1, 1, 0, 0, 0, 0};
  d.centroid_node_area          = {100, 100, 1, 1, 1, 1};
  d.node_centroid_color         = {0, 1, 0, -1, -1, -1};
  d.centroid_color_names        = {"top.A.ram", "top.B.ram"};
  dp::CentroidGroup a, b;
  a.name   = "top.A";
  b.name   = "top.B";
  a.family = b.family = 0;
  a.nodes             = {0, 2, 3, 4};
  b.nodes             = {1, 5};
  d.centroid_groups   = {a, b};
  color::Floorplan_coloring coloring;
  coloring.owner  = d.node_centroid_color;
  coloring.colors = {
      {"top.A.ram", {0, 2}, 101, 1},
      {"top.B.ram",    {1}, 100, 1}
  };
  coloring.recipients = {
      {},
      {},
      {},
      {0, 1},
      {0, 1},
      {}
  };
  const std::vector<std::string> paths{"top.A.ram", "top.B.ram", "top.A.logic", "top.A.ff", "top.A.local", "top.B.free"};
  d.build_maps();
  const auto sizes = d.node_size_x;
  placement_color_centroids(d, paths, coloring, 10, false);
  d.validate();
  ASSERT_EQ(d.centroid_groups.size(), 4);
  EXPECT_EQ(d.centroid_groups[2].parent, 0);
  EXPECT_EQ(d.centroid_groups[3].parent, 1);
  EXPECT_EQ(d.centroid_groups[2].repulsion_area, 102);
  EXPECT_EQ(d.centroid_groups[3].repulsion_area, 101);
  EXPECT_EQ(d.centroid_groups[0].repulsion_area, 102);
  EXPECT_EQ(d.centroid_groups[1].repulsion_area, 102);
  EXPECT_EQ(d.centroid_groups[0].repulsion_area + d.centroid_groups[1].repulsion_area, 204);
  const auto summary = dp::detail::centroid_summary(d);
  EXPECT_EQ(summary[2].area, 101);
  EXPECT_EQ(summary[3].area, 100);
  auto physical = d;
  for (auto& g : physical.centroid_groups) {
    g.repulsion_area = -1;
  }
  EXPECT_EQ(dp::detail::centroid_attraction(physical, 1).x, dp::detail::centroid_attraction(d, 1).x);
  EXPECT_EQ(dp::detail::centroid_attraction(physical, 1).y, dp::detail::centroid_attraction(d, 1).y);
  EXPECT_EQ(d.node_size_x, sizes);
  // Only the two structural siblings repel. Their color leaves belong to
  // different parents and neither can repel its own ancestor.
  EXPECT_EQ(d.centroid_groups[2].repulsion_scope, 1);
  EXPECT_EQ(d.centroid_groups[3].repulsion_scope, 2);
  // Rebuild after cell sizing keeps identities and recomputes physical budgets.
  d.centroid_node_area[2] = 2;
  d.node_size_x[2]        = 2;
  coloring.colors[0].area = 102;
  placement_color_centroids(d, paths, coloring, 10, true);
  d.validate();
  EXPECT_EQ(d.centroid_groups[2].repulsion_area, 103);
  EXPECT_EQ(d.centroid_groups[0].repulsion_area, 103);
}

TEST(ColorCentroids, BelowThresholdRecipientsDoNotConsumeReservation) {
  auto d = fixture();
  d.centroid_groups.clear();
  color::Floorplan_coloring coloring;
  coloring.owner  = d.node_centroid_color;
  coloring.colors = {
      {"eligible", {0, 3}, 5001, 1},
      {   "small",    {1}, 4000, 1},
      {   "other", {2, 4}, 4101, 1},
      {   "fixed",    {5}, 5000, 1},
      {   "logic",    {6},    1, 0}
  };
  coloring.owner[3] = d.node_centroid_color[3] = -1;
  coloring.colors[0].members                   = {0};
  coloring.colors[0].area                      = 5000;
  coloring.recipients.resize(d.node_x.size());
  coloring.recipients[3] = {0, 1, 2};
  placement_color_centroids(d, std::vector<std::string>(7), coloring, 4500, false);
  d.validate();
  ASSERT_EQ(d.centroid_groups.size(), 1);
  EXPECT_EQ(d.centroid_groups[0].repulsion_area, 5001);
  EXPECT_EQ(dp::detail::centroid_summary(d)[0].area, 5000);
}

TEST(ColorCentroids, SharedColorPortionsStayInsideTheirHierarchy) {
  dp::Design d;
  d.layout      = {0, 0, 1000, 1000, 1, 1};
  d.num_movable = 7;
  d.node_x      = {10, 700, 40, 900, 20, 710, 50};
  d.node_y      = d.node_x;
  d.node_size_x = d.node_size_y = {10, 10, 10, 1, 1, 1, 1};
  d.node_macro                  = {1, 1, 1, 0, 0, 0, 0};
  d.centroid_node_area          = {100, 100, 100, 1, 1, 1, 1};
  dp::CentroidGroup a, b, child;
  a.name            = "A";
  b.name            = "B";
  child.name        = "A.child";
  child.parent      = 0;
  d.centroid_groups = {a, b, child};
  const std::vector<std::string> paths{"A.ram", "B.ram", "A.child.ram", "shared", "A.logic", "B.logic", "A.child.logic"};
  const auto                     scopes = centroid_node_scopes(d, paths);
  EXPECT_EQ(scopes, std::vector<int>({0, 1, 2, -1, 0, 1, 2}));
  using K                                        = color::Floorplan_kind;
  const std::vector<color::Floorplan_node> nodes = {
      {paths[0], K::macro, 100},
      {paths[1], K::macro, 100},
      {paths[2], K::macro, 100},
      {paths[3], K::logic,   1},
      {paths[4], K::logic,   1},
      {paths[5], K::logic,   1},
      {paths[6], K::logic,   1}
  };
  const std::vector<std::pair<int, int>> edges{
      {3, 0},
      {3, 1},
      {3, 2},
      {4, 0},
      {5, 1},
      {6, 2}
  };
  auto global = color::floorplan_color(nodes, edges, nullptr, {true, 10});
  EXPECT_THROW(placement_color_centroids(d, paths, global, 10, false), std::invalid_argument);
  auto local            = color::floorplan_color(nodes, edges, nullptr, {true, 10, scopes});
  d.node_centroid_color = local.owner;
  for (const auto& c : local.colors) {
    d.centroid_color_names.push_back(c.name);
  }
  d.build_maps();
  placement_color_centroids(d, paths, local, 10, false);
  d.validate();
  ASSERT_EQ(d.centroid_groups.size(), 6);
  for (int i = 0; i < 3; ++i) {
    const auto& leaf = d.centroid_groups[3 + i];
    EXPECT_EQ(leaf.parent, i);
    EXPECT_EQ(leaf.repulsion_scope, i + 1);
    EXPECT_EQ(leaf.scope_macros, 1);
    EXPECT_EQ(leaf.repulsion_area, 101);
  }
  EXPECT_EQ(d.centroid_groups[0].repulsion_area, 202);
  EXPECT_EQ(d.centroid_groups[1].repulsion_area, 101);
  EXPECT_EQ(d.centroid_groups[2].repulsion_area, 101);
  EXPECT_EQ(d.centroid_groups[0].nodes, std::vector<int>({0, 2, 4, 6}));
  EXPECT_EQ(dp::detail::centroid_attraction(d, 1).x[3], 0);
  // A repair buffer inherits its origin's local color and structural membership,
  // even when its generated name no longer carries a hierarchy path.
  ++d.num_movable;
  d.node_x.push_back(15);
  d.node_y.push_back(15);
  d.node_size_x.push_back(1);
  d.node_size_y.push_back(1);
  d.node_macro.push_back(0);
  d.centroid_node_area.push_back(1);
  d.node_centroid_color.push_back(local.owner[0]);
  local.owner.push_back(local.owner[0]);
  local.recipients.emplace_back();
  auto& c = local.colors[local.owner[0]];
  c.members.push_back(7);
  ++c.area;
  d.centroid_groups[0].nodes.push_back(7);
  d.build_maps();
  auto repaired_paths = paths;
  repaired_paths.push_back("new_buffer");
  placement_color_centroids(d, repaired_paths, local, 10, true);
  d.validate();
  EXPECT_EQ(d.centroid_groups.size(), 6);
  EXPECT_EQ(d.centroid_groups[3].parent, 0);
  EXPECT_EQ(d.centroid_groups[3].repulsion_area, 102);
  EXPECT_EQ(d.centroid_groups[0].repulsion_area, 203);
}

TEST(ColorCentroids, ColorLeafInheritsParentSnapTranslationOnly) {
  const std::vector<dp::FloorplanCircle> circles = {
      {    "a", 100, 100, 10, 100, 0, -1},
      {    "b", 101, 400, 10, 100, 0, -1},
      {"color", 105, 110,  5,  20, 1,  0}
  };
  const auto snap = snap_centroid_targets(circles, 2, {0, 0, 1000, 1000, 1, 1}, {});
  EXPECT_NEAR(snap.circles[2].x - snap.circles[0].x, 5, 1e-9);
  EXPECT_NEAR(snap.circles[2].y - snap.circles[0].y, 10, 1e-9);
  for (const auto& r : snap.relations) {
    for (int n : r.nodes) {
      EXPECT_LT(n, 2);
    }
  }
}
}  // namespace livehd::placement
