// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "capacity_bins.hpp"

#include <set>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
Ann_place cell(int64_t x, int64_t y, int64_t w, int64_t h) {
  Ann_place p;
  p.x      = x;
  p.y      = y;
  p.width  = w;
  p.height = h;
  return p;
}
}  // namespace

TEST(Capacity_bins, ApportionsAreaAndChecksTheCap) {
  Capacity_bins grid({0, 0, 40, 20}, 4, 2);
  ASSERT_EQ(grid.bins.size(), 8u);
  EXPECT_EQ(grid.index(3, 1), 7u);
  for (auto& b : grid.bins) {
    b.capacity = 100;
  }
  EXPECT_DOUBLE_EQ(grid.total_capacity(), 800);
  const auto straddle = cell(5, 5, 10, 2);  // half in bin (0,0), half in bin (1,0)
  grid.deposit(straddle, true);
  EXPECT_DOUBLE_EQ(grid.bins[grid.index(0, 0)].after, 10);
  EXPECT_DOUBLE_EQ(grid.bins[grid.index(1, 0)].after, 10);
  EXPECT_DOUBLE_EQ(grid.bins[grid.index(0, 0)].density(true), .1);
  EXPECT_EQ(grid.bin_xy(straddle), std::make_pair(1, 0));
  EXPECT_TRUE(grid.within_cap(.1));
  EXPECT_FALSE(grid.within_cap(.05));
  EXPECT_TRUE(grid.fits(cell(0, 0, 9, 10), nullptr, 1));
  const auto other = cell(0, 0, 9, 10);
  EXPECT_FALSE(grid.fits(cell(0, 0, 9, 10), &other, 1));  // jointly 180 + 10 > 100
  EXPECT_TRUE(grid.has_room(grid.index(3, 1), .5));
}

TEST(Capacity_bins, RingsAndTheirExteriorCoverTheGrid) {
  Capacity_bins    grid({0, 0, 50, 50}, 5, 5);
  std::set<size_t> seen;
  for (int ring = 0; ring < 5; ++ring) {
    grid.each_ring(1, 1, ring, [&](size_t b) { EXPECT_TRUE(seen.insert(b).second); });
  }
  EXPECT_EQ(seen.size(), 25u);
  std::vector<physical::Rect> exterior;
  grid.each_ring_exterior(1, 1, 0, 2, [&](const physical::Rect& r) { exterior.push_back(r); });
  ASSERT_EQ(exterior.size(), 4u);
  EXPECT_DOUBLE_EQ(exterior[0].xh, 10);  // left of bin column 1
  EXPECT_DOUBLE_EQ(exterior[1].xl, 20);
  EXPECT_DOUBLE_EQ(exterior[2].yh, 10);
  EXPECT_DOUBLE_EQ(exterior[3].yl, 18);  // reach_y below the upper bin edge
  exterior.clear();
  grid.each_ring_exterior(1, 1, 2, 2, [&](const physical::Rect& r) { exterior.push_back(r); });
  EXPECT_EQ(exterior.size(), 2u);  // only right and above remain
}
}  // namespace livehd::placement
