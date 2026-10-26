// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "filler_budget.hpp"

#include "gtest/gtest.h"
namespace dp = DreamPlace::native;
namespace p  = livehd::placement;
namespace {
dp::Design design() {
  dp::Design d;
  d.layout      = {0, 0, 100, 100, 1, 1};
  d.num_movable = 1;
  d.node_x      = {10, 50};
  d.node_y      = {10, 0};
  d.node_size_x = {1, 50};
  d.node_size_y = {1, 100};
  d.node_macro  = {0, 1};
  d.build_maps();
  return d;
}
dp::Options options() {
  dp::Options o;
  o.bins_x = o.bins_y = 8;
  o.max_iterations = o.min_iterations = 1;
  o.add_fillers                       = true;
  o.legalize = o.detailed_place = false;
  o.target_density              = .5;
  o.scale_macro_density         = true;
  o.filler_area_fraction        = .25;
  return o;
}
void matches_native(dp::Design d, const dp::Options& o, const p::Filler_budget& b) {
  const auto r = dp::place(d, o);
  EXPECT_LE(r.filler_area, b.requested_area);
  EXPECT_LT(b.requested_area - r.filler_area, r.filler_size_x * r.filler_size_y + 1e-9);
}
TEST(FillerBudget, RelativeChargeDiscountsFixedOccupancyAndMacroBodies) {
  auto d = design();
  auto o = options();
  auto b = p::relative_filler_budget(d, o);
  EXPECT_DOUBLE_EQ(b.fraction, .25);
  EXPECT_DOUBLE_EQ(b.nonmacro_area, 5000);
  EXPECT_DOUBLE_EQ(b.requested_area, .25 * (.5 * 5000 - 1));
  matches_native(d, o, b);
  // The same physical macro moved into the movable prefix consumes its full
  // area before the utilization target is applied to standard-cell space.
  d.num_movable = 2;
  b             = p::relative_filler_budget(d, o);
  EXPECT_DOUBLE_EQ(b.requested_area, .25 * (.5 * 5000 - 1));
  matches_native(d, o, b);
}
TEST(FillerBudget, ScalesWithCoreUtilizationAndMacroPercentage) {
  auto d            = design();
  auto o            = options();
  auto b            = p::relative_filler_budget(d, o);
  d.layout.xh       = 200;
  const auto larger = p::relative_filler_budget(d, o);
  EXPECT_DOUBLE_EQ(larger.requested_area, .25 * (.5 * 15000 - 1));
  EXPECT_GT(larger.requested_area, b.requested_area);
  matches_native(d, o, larger);
  o.target_density  = .7;
  const auto denser = p::relative_filler_budget(d, o);
  EXPECT_DOUBLE_EQ(denser.requested_area, .25 * (.7 * 15000 - 1));
  EXPECT_GT(denser.requested_area, larger.requested_area);
  matches_native(d, o, denser);
  d.node_size_x[1] = 100;
  b                = p::relative_filler_budget(d, o);
  EXPECT_DOUBLE_EQ(b.requested_area, .25 * (.7 * 10000 - 1));
  matches_native(d, o, b);
}
TEST(FillerBudget, ParticleResolutionDoesNotChangeRequestedCharge) {
  auto d              = design();
  auto o              = options();
  o.filler_cell_space = true;
  const auto b        = p::relative_filler_budget(d, o);
  EXPECT_DOUBLE_EQ(b.requested_area, .25 * (1 - .5) * 5000);
  o.filler_cell_scale = 2;
  const auto larger   = p::relative_filler_budget(d, o);
  EXPECT_DOUBLE_EQ(larger.requested_area, b.requested_area);
  EXPECT_DOUBLE_EQ(larger.fraction, b.fraction);
  matches_native(d, o, larger);
  o.add_fillers = false;
  EXPECT_DOUBLE_EQ(p::relative_filler_budget(d, o).requested_area, 0);
}
}  // namespace
