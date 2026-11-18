// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "macro_edge.hpp"

#include <stdexcept>

#include "gtest/gtest.h"

namespace pl = livehd::placement;

namespace {
pl::Edge_snap_limits core(int64_t max_gap) {
  pl::Edge_snap_limits l;
  l.core_xl = l.core_yl = 0;
  l.core_xh = l.core_yh = 1000;
  l.space_x = l.space_y = 10;
  l.max_gap_x = l.max_gap_y = max_gap;
  return l;
}
pl::Edge_object box(int64_t x, int64_t y, int64_t w, int64_t h, bool movable = true, bool macro = true) {
  return {x, y, x + w, y + h, movable, macro};
}
}  // namespace

TEST(MacroEdgeSnap, SlidesToNearestEdgesWithinLimit) {
  // Near the low-left corner, near the right edge, and one in the middle.
  std::vector<pl::Edge_object> objects{box(30, 40, 100, 100), box(860, 450, 100, 100), box(450, 450, 100, 100)};
  const auto                   report = pl::snap_macros_to_edges(objects, core(50));
  EXPECT_EQ(objects[0].xl, 0);
  EXPECT_EQ(objects[0].yl, 0);
  EXPECT_EQ(objects[1].xh, 1000);
  EXPECT_EQ(objects[1].yl, 450);  // the 450 sliver to y edges exceeds the limit
  EXPECT_EQ(objects[2].xl, 450);  // the middle macro does not move
  EXPECT_EQ(objects[2].yl, 450);
  EXPECT_EQ(report.moved, 2);
  EXPECT_EQ(report.moves, 3);
  EXPECT_EQ(report.distance, 30 + 40 + 40);
}

TEST(MacroEdgeSnap, ChainKeepsChannelAndHalo) {
  auto limits   = core(50);
  limits.halo_x = limits.halo_y = 5;
  limits.space_x = limits.space_y = 12;  // max(channel, 2 * halo)
  // A row of three macros left of the middle: each slides onto the one before it.
  std::vector<pl::Edge_object> objects{box(20, 500, 50, 50), box(100, 500, 50, 50), box(170, 500, 50, 50)};
  pl::snap_macros_to_edges(objects, limits);
  EXPECT_EQ(objects[0].xl, 5);  // the halo stays inside the core
  EXPECT_EQ(objects[1].xl, objects[0].xh + 12);
  EXPECT_EQ(objects[2].xl, objects[1].xh + 12);
}

TEST(MacroEdgeSnap, InteriorClusterAndLargeSliverStay) {
  // The first macro is too far from the edge, so its neighbor is not edge-anchored either.
  std::vector<pl::Edge_object> objects{box(100, 500, 50, 50), box(170, 500, 50, 50)};
  const auto                   report = pl::snap_macros_to_edges(objects, core(50));
  EXPECT_EQ(objects[0].xl, 100);
  EXPECT_EQ(objects[1].xl, 170);
  EXPECT_EQ(report.moved, 0);
}

TEST(MacroEdgeSnap, FixedObjectsAndOffAxisMacros) {
  // A fixed macro at the edge anchors a movable one; a fixed non-macro obstacle only keeps the halo.
  auto limits   = core(60);
  limits.halo_x = limits.halo_y = 3;
  std::vector<pl::Edge_object> objects{box(3, 500, 40, 40, false),
                                       box(80, 500, 40, 40),
                                       box(0, 100, 20, 200, false, false),
                                       box(50, 150, 40, 40),
                                       box(40, 700, 40, 40)};
  pl::snap_macros_to_edges(objects, limits);
  EXPECT_EQ(objects[0].xl, 3);
  EXPECT_EQ(objects[1].xl, 43 + 10);  // channel after the fixed macro
  EXPECT_EQ(objects[3].xl, 50);       // blocked by an obstacle that is not edge-anchored
  EXPECT_EQ(objects[4].xl, 3);        // no conflict across the axis: reaches the edge
}

TEST(MacroEdgeSnap, GridAndDisabledAxis) {
  auto limits      = core(100);
  limits.grid      = 7;
  limits.max_gap_y = 0;
  std::vector<pl::Edge_object> objects{box(925, 20, 50, 50)};
  pl::snap_macros_to_edges(objects, limits);
  EXPECT_LE(objects[0].xh, 1000);
  EXPECT_EQ(objects[0].xl % 7, 0);
  EXPECT_GT(objects[0].xh, 1000 - 7);
  EXPECT_EQ(objects[0].yl, 20);  // y disabled
  limits.grid = 0;
  EXPECT_THROW(pl::snap_macros_to_edges(objects, limits), std::invalid_argument);
}

TEST(MacroEdgeSnap, SliversCloseWithoutAnEdgeAnchor) {
  auto limits    = core(0);  // edge snap off: only slivers move
  limits.halo_x  = limits.halo_y = 5;
  limits.space_x = limits.space_y = 10;
  limits.slivers = true;
  // An interior pair 15 apart (sliver 5 < space 10) closes to the spacing; a pair 30 apart (sliver 20) stays.
  std::vector<pl::Edge_object> objects{box(300, 500, 50, 50), box(365, 500, 50, 50), box(300, 700, 50, 50),
                                       box(380, 700, 50, 50)};
  const auto                   report = pl::snap_macros_to_edges(objects, limits);
  EXPECT_EQ(objects[0].xl, 300);  // its own sliver to the core edge (295) is far above the halo
  EXPECT_EQ(objects[1].xl, objects[0].xh + 10);
  EXPECT_EQ(objects[3].xl, 380);
  EXPECT_EQ(report.moved, 1);
  // Near the core edge: a body 8 from it (under 2 * halo) goes flush with its halo inside the core.
  std::vector<pl::Edge_object> edge{box(8, 500, 50, 50)};
  pl::snap_macros_to_edges(edge, limits);
  EXPECT_EQ(edge[0].xl, 5);
}

TEST(MacroEdgeSnap, SliversKeepAChannelBetweenTwoPinnedFaces) {
  auto limits    = core(0);
  limits.halo_x  = limits.halo_y = 5;
  limits.space_x = limits.space_y = 10;
  limits.slivers = true;
  auto left      = box(300, 500, 50, 50);
  auto right     = box(365, 500, 50, 50);
  left.pin_faces  = 2;  // pins on its right face
  right.pin_faces = 1;  // pins on its left face: the two face each other
  std::vector<pl::Edge_object> objects{left, right};
  pl::snap_macros_to_edges(objects, limits);
  EXPECT_EQ(objects[1].xl, 365);
  // Only one pinned face: the sliver closes.
  objects[0].pin_faces = 0;
  pl::snap_macros_to_edges(objects, limits);
  EXPECT_EQ(objects[1].xl, objects[0].xh + 10);
}

TEST(MacroEdgeSnap, SliversLandOnTheTrackLattice) {
  auto limits    = core(0);
  limits.halo_x  = limits.halo_y = 5;
  limits.space_x = limits.space_y = 10;
  limits.slivers = true;
  auto moving    = box(368, 500, 50, 50);
  moving.period[0] = 4;
  moving.phases[0] = {1};  // x = 1 + 4k
  std::vector<pl::Edge_object> objects{box(300, 500, 50, 50), moving};
  pl::snap_macros_to_edges(objects, limits);
  // the spacing allows 360; the nearest lattice origin at or above it is 361
  EXPECT_EQ(objects[1].xl, 361);
  EXPECT_EQ((objects[1].xl - 1) % 4, 0);
}

TEST(MacroEdgeSnap, PinnedFaceKeepsAChannelToTheCoreEdge) {
  auto limits   = core(50);
  limits.halo_x = limits.halo_y = 5;
  limits.space_x = limits.space_y = 12;
  limits.pin_edge_x = limits.pin_edge_y = 20;  // a full channel for a face with signal pins
  // Both start at the halo off the left edge; only the first has pins on its left face.
  auto pinned      = box(5, 100, 50, 50);
  pinned.pin_faces = 1;
  auto plain       = box(5, 700, 50, 50);
  // A pinned face 12 off the right edge (a sliver the snap would otherwise close to the halo).
  auto right       = box(1000 - 12 - 50, 400, 50, 50);
  right.pin_faces  = 2;
  std::vector<pl::Edge_object> objects{pinned, plain, right};
  pl::snap_macros_to_edges(objects, limits);
  EXPECT_EQ(objects[0].xl, 20);        // pushed back to the pinned-face clearance
  EXPECT_EQ(objects[1].xl, 5);         // an unpinned face keeps the halo
  EXPECT_EQ(objects[2].xh, 1000 - 20);  // pushed back from the right edge too, never snapped to the halo
}

TEST(MacroEdgeSnap, PinnedFacePushBackRespectsTheFarSide) {
  auto limits   = core(50);
  limits.halo_x = limits.halo_y = 5;
  limits.space_x = limits.space_y = 12;
  limits.pin_edge_x = limits.pin_edge_y = 20;
  // The pinned macro has a neighbor at its far side: moving 15 would break the 12 channel to it, so it stays.
  auto pinned      = box(5, 100, 50, 50);
  pinned.pin_faces = 1;
  std::vector<pl::Edge_object> objects{pinned, box(5 + 50 + 12, 100, 50, 50, /*movable=*/false)};
  pl::snap_macros_to_edges(objects, limits);
  EXPECT_EQ(objects[0].xl, 5);
}
