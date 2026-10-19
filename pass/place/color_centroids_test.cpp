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
}  // namespace livehd::placement
