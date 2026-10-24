// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cell_jump.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "gtest/gtest.h"

namespace dp = DreamPlace::native;
using namespace livehd::placement;
namespace {
dp::Design fixture() {
  dp::Design d;
  d.layout      = {0, 0, 100, 60, 1, 1};
  d.num_movable = 5;
  d.node_x      = {10, 80, 80, 12, 80, 40, 0};
  d.node_y      = {10, 10, 20, 20, 30, 0, 10};
  d.node_size_x = {2, 2, 2, 2, 2, 20, 0};
  d.node_size_y = {1, 1, 1, 1, 1, 40, 0};
  d.node_macro  = {0, 0, 0, 0, 0, 1, 0};
  d.pin2node    = {6, 0, 0, 1, 1, 2, 0, 3, 0, 4};
  d.pin_offset_x.assign(10, .5);
  d.pin_offset_y.assign(10, .5);
  d.flat_net2pin       = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  d.flat_net2pin_start = {0, 2, 4, 6, 8, 10};
  d.net_mask           = {1, 1, 1, 1, 0};
  d.build_maps();
  return d;
}
TEST(CellJump, OpenMacroIntersection) {
  EXPECT_TRUE(cell_jump_crosses(0, 5, 20, 5, 5, 0, 10, 10));
  EXPECT_TRUE(cell_jump_crosses(20, 5, 0, 5, 5, 0, 10, 10));
  EXPECT_TRUE(cell_jump_crosses(7, -10, 7, 20, 5, 0, 10, 10));
  EXPECT_TRUE(cell_jump_crosses(0, -5, 20, 15, 5, 0, 10, 10));
  EXPECT_FALSE(cell_jump_crosses(0, 10, 20, 10, 5, 0, 10, 10));
  EXPECT_FALSE(cell_jump_crosses(0, 5, 5, 0, 5, 0, 10, 10));
  EXPECT_FALSE(cell_jump_crosses(0, 5, 5, 5, 5, 0, 10, 10));
  EXPECT_FALSE(cell_jump_crosses(0, 5, 4, 5, 5, 0, 10, 10));
}
TEST(CellJump, DepthFrontierUsesCurrentParentAndSkipsControls) {
  auto      d = fixture();
  Cell_jump jump({.depth = 3}, {5});
  EXPECT_TRUE(jump.update(d, 90).empty());
  const auto two = jump.update(d, 100);
  ASSERT_EQ(two.size(), 1);
  EXPECT_EQ(two[0].node, 1);
  EXPECT_EQ(two[0].fix_iterations, 2);
  EXPECT_LT(two[0].x, 40);
  EXPECT_LT(std::abs(two[0].x - d.node_x[0]) + std::abs(two[0].y - d.node_y[0]), 5);
  EXPECT_FALSE(two[0].x < 12 && two[0].x + 2 > 10 && two[0].y < 11 && two[0].y + 1 > 10);
  ASSERT_EQ(jump.samples().size(), 1);
  EXPECT_EQ(jump.samples()[0].roots, 1);
  EXPECT_EQ(jump.samples()[0].candidates, 2);
  EXPECT_EQ(jump.samples()[0].crossing_edges_after, 0);
  EXPECT_TRUE(jump.update(d, 100).empty());
  // GP has moved cell two since its jump. Cell three must use this new point.
  d.node_x[1]      = 20;
  d.node_y[1]      = 8;
  const auto three = jump.update(d, 110);
  ASSERT_EQ(three.size(), 1);
  EXPECT_EQ(three[0].node, 2);
  EXPECT_LT(std::abs(three[0].x - 20) + std::abs(three[0].y - 8), 5);
  EXPECT_EQ(jump.samples()[1].candidates, 1);
  EXPECT_TRUE(jump.update(d, 120).empty());
  EXPECT_EQ(d.node_x[4], 80);  // reset/clock-masked connection never traversed
}
TEST(CellJump, MacroPinsAndPinOffsetsDetermineCrossing) {
  auto d        = fixture();
  d.pin2node[0] = 5;  // Anchor from macro output/input rather than top-level IO.
  d.build_maps();
  Cell_jump  jump({.depth = 2}, {5});
  const auto two = jump.update(d, 100);
  ASSERT_EQ(two.size(), 1);
  EXPECT_EQ(two[0].node, 1);
  EXPECT_TRUE(jump.update(d, 110).empty());
  d                 = fixture();
  d.pin_offset_y[2] = d.pin_offset_y[3] = 45;  // Segment above the macro.
  Cell_jump above({.depth = 2}, {5});
  EXPECT_TRUE(above.update(d, 100).empty());
}
TEST(CellJump, RespectsFixedExclusionsAndBounds) {
  auto d = fixture();
  // Use origin boxes to prevent the child from jumping across its fence.
  d.node_boxes.assign(d.node_x.size(), {0, 0, 98, 59});
  d.node_boxes[1] = {70, 0, 98, 59};
  Cell_jump  jump({.depth = 2}, {5});
  const auto two = jump.update(d, 100);
  ASSERT_EQ(two.size(), 1);
  EXPECT_GE(two[0].x, 70);
  EXPECT_EQ(jump.samples()[0].crossing_edges_after, 1);
  for (unsigned depth : {0, 1}) {
    Cell_jump off({.depth = depth}, {5});
    EXPECT_TRUE(off.update(d, 100).empty());
    EXPECT_TRUE(off.samples().empty());
  }
  EXPECT_THROW((Cell_jump({.depth = 5}, {5})), std::invalid_argument);
  EXPECT_THROW((Cell_jump({.depth = 4, .settle = std::numeric_limits<unsigned>::max()}, {5})), std::invalid_argument);
  EXPECT_THROW((Cell_jump({.depth = 4, .interval = std::numeric_limits<unsigned>::max()}, {5})), std::invalid_argument);
  EXPECT_THROW((Cell_jump({.depth = 2, .fix_period = unsigned(std::numeric_limits<int>::max())}, {5})), std::invalid_argument);
}
}  // namespace
