// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "warpspace.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numeric>

#include "gtest/gtest.h"
namespace livehd::placement {
namespace dp = DreamPlace::native;
TEST(Warpspace, UniformDensityHasZeroFieldAndConservesMass) {
  warp::Layout layout;
  layout.lo = {0, 0};
  layout.hi = {100, 100};
  warp::Options o;
  o.bins            = 4;
  o.iters           = 1;
  o.time_limit      = 1;
  o.balance         = 3;
  o.balance_model   = "strain";
  const auto result = warp::density_warp(layout, std::vector<double>(16, 125), o);
  EXPECT_NEAR(result.total, result.out_total, 1e-7);
  for (size_t n = 0; n < result.mesh.points.size(); ++n) {
    EXPECT_LT(warp::length(result.mesh.points[n] - result.mesh.original[n]), 1e-5);
  }
}
TEST(Warpspace, EmptyDemandKeepsBoundaryAndInteriorFixed) {
  warp::Layout layout;
  layout.lo = {0, 0};
  layout.hi = {100, 100};
  warp::Options o;
  o.bins            = 4;
  o.iters           = 1;
  o.time_limit      = 1;
  o.balance         = 3;
  const auto result = warp::density_warp(layout, std::vector<double>(16, 0), o);
  for (size_t n = 0; n < result.mesh.points.size(); ++n) {
    EXPECT_LT(warp::length(result.mesh.points[n] - result.mesh.original[n]), 1e-7);
  }
}
TEST(Warpspace, ReflectedAndOversizedFiltersPreserveMass) {
  std::vector<double> input(64);
  input[0]  = 10;
  input[31] = 20;
  for (int window : {1, 4, 8, 32}) {
    auto out = warp::smooth_box(input, 8, window);
    EXPECT_NEAR(std::accumulate(out.begin(), out.end(), 0.), 30, 1e-10);
    if (window >= 8) {
      for (double value : out) {
        EXPECT_DOUBLE_EQ(value, 30. / 64);
      }
    }
  }
}
TEST(Warpspace, LargeCoordinateStrainCostsRemainNumericallyBounded) {
  warp::Layout layout;
  layout.lo = {0, 0};
  layout.hi = {1000000, 1000000};
  warp::Options o;
  o.bins          = 8;
  o.iters         = 1;
  o.time_limit    = 2;
  o.balance       = 3;
  o.balance_model = "strain";
  std::vector<double> mass(64, 0);
  mass[0] = mass[1] = 1e10;
  const auto result = warp::density_warp(layout, mass, o);
  EXPECT_NEAR(result.total, result.out_total, result.total * 1e-7);
  EXPECT_TRUE(std::isfinite(result.seconds));
  ASSERT_FALSE(result.passes.empty());
  EXPECT_LT(result.passes.front().objective_scale, 1);
}
TEST(Warpspace, MacroOverlapReservesUnionOnceAndKeepsRigidSizes) {
  warp::Layout layout;
  layout.lo = {0, 0};
  layout.hi = {100, 100};
  for (int i = 0; i < 2; ++i) {
    warp::Inst c;
    c.p     = {10., 10.};
    c.w     = 20;
    c.h     = 30;
    c.macro = true;
    layout.insts.push_back(c);
  }
  const auto occupied = warp::macro_occupancy(layout, 4);
  EXPECT_DOUBLE_EQ(std::accumulate(occupied.begin(), occupied.end(), 0.), 600);
  warp::Options o;
  o.bins            = 4;
  o.iters           = 1;
  o.time_limit      = 1;
  o.macro_space     = true;
  o.balance         = 3;
  o.balance_model   = "strain";
  const auto result = warp::density_warp(layout, std::vector<double>(16, 20), o);
  EXPECT_NEAR(result.total_occupied, result.transported_occupied, 1e-6);
  ASSERT_EQ(result.after.insts.size(), 2);
  for (const auto& c : result.after.insts) {
    EXPECT_DOUBLE_EQ(c.w, 20);
    EXPECT_DOUBLE_EQ(c.h, 30);
  }
}
TEST(Warpspace, UniqueOwnersAndUnevenHierarchyAverageLeavesInsteadOfChildAverages) {
  dp::Design d;
  d.num_movable = 5;
  d.node_macro.assign(6, 1);
  d.node_centroid_color = {0, 0, 1, 2, 3, 4};
  d.centroid_groups.resize(7);
  // root -> branch -> two leaves; root -> one leaf; one empty sibling.
  for (int g = 0; g < 7; ++g) {
    d.centroid_groups[g].name = std::to_string(g);
  }
  d.centroid_groups[1].parent = 0;
  d.centroid_groups[2].parent = 1;
  d.centroid_groups[3].parent = 1;
  d.centroid_groups[4].parent = 0;
  d.centroid_groups[5].parent = 0;
  d.centroid_groups[6].parent = 0;
  d.centroid_groups[0].nodes  = {0, 1, 2, 3, 4, 5};
  d.centroid_groups[1].nodes  = {0, 1, 2};
  d.centroid_groups[2].nodes  = {0, 1};
  d.centroid_groups[2].colors = {0};  // explicit/color overlap counted once
  d.centroid_groups[3].nodes  = {2};
  d.centroid_groups[4].nodes  = {3};
  d.centroid_groups[6].nodes  = {5};  // fixed-only
  std::vector<dp::FloorplanCircle> targets(7);
  for (auto& t : targets) {
    t.x = 50;
    t.y = 50;
  }
  const auto shifted = shift_warp_centroids(d,
                                            {
                                                {0,  {2, 0}},
                                                {1,  {6, 0}},
                                                {2,  {8, 0}},
                                                {3, {12, 0}}
  },
                                            targets);
  EXPECT_DOUBLE_EQ(targets[2].x, 54);
  EXPECT_DOUBLE_EQ(targets[3].x, 58);
  EXPECT_DOUBLE_EQ(targets[4].x, 62);
  EXPECT_DOUBLE_EQ(targets[1].x, 56);
  EXPECT_DOUBLE_EQ(targets[0].x, 58);  // (4+8+12)/3, not (6+12)/2
  EXPECT_EQ(shifted[0].count, 3);
  EXPECT_EQ(shifted[2].memories, 2);
  EXPECT_DOUBLE_EQ(targets[5].x, 50);
  EXPECT_DOUBLE_EQ(targets[6].x, 50);
}
dp::Design fixture() {
  dp::Design d;
  d.layout      = {0, 0, 100, 100, 1, 1};
  d.num_movable = 2;
  d.node_x      = {40, 20, 80};
  d.node_y      = {40, 20, 80};
  d.node_size_x = {10, 20, 10};
  d.node_size_y = {10, 20, 10};
  d.node_macro  = {1, 0, 1};
  d.build_maps();
  return d;
}
TEST(Warpspace, InvalidAdapterShapesFailBeforeSolving) {
  auto         d = fixture();
  Warp_options o;
  d.node_y.pop_back();
  EXPECT_THROW(placement_warp(d, {0}, o), std::invalid_argument);
  d             = fixture();
  d.num_movable = int(d.node_x.size()) + 1;
  EXPECT_THROW(placement_warp(d, {0}, o), std::invalid_argument);
  d = fixture();
  d.node_macro.pop_back();
  EXPECT_THROW(placement_warp(d, {0}, o), std::invalid_argument);
}
TEST(Warpspace, DensityGridRejectsUnmappedCellInstances) {
  warp::Layout layout;
  layout.lo = {0, 0};
  layout.hi = {100, 100};
  warp::Inst cell;
  cell.w = cell.h = 1;
  cell.standard   = true;
  layout.insts.push_back(cell);
  warp::Options o;
  o.bins = 4;
  EXPECT_THROW(warp::density_warp(layout, std::vector<double>(16, 1), o), std::runtime_error);
}
TEST(Warpspace, TechnologyDbuScalePreservesMicronFieldAndConvertsVectorsBack) {
  auto         physical = fixture(), dbu = physical;
  Warp_options o;
  o.bins             = 4;
  o.iterations       = 1;
  o.seconds          = 1;
  const auto microns = placement_warp(physical, {0}, o);
  for (auto* values : {&dbu.node_x, &dbu.node_y, &dbu.node_size_x, &dbu.node_size_y}) {
    for (auto& v : *values) {
      v *= 1000;
    }
  }
  dbu.layout.xh         *= 1000;
  dbu.layout.yh         *= 1000;
  dbu.layout.row_height *= 1000;
  dbu.layout.site_width *= 1000;
  o.dbu_per_um           = 1000;
  const auto scaled      = placement_warp(dbu, {0}, o);
  EXPECT_NEAR(microns.density.total, scaled.density.total, 1e-8);
  EXPECT_NEAR(microns.delta.at(0).x, scaled.delta.at(0).x / 1000, 1e-6);
  EXPECT_NEAR(microns.delta.at(0).y, scaled.delta.at(0).y / 1000, 1e-6);
}
TEST(Warpspace, IndependentWarmupBoundedRefreshReplacementAndEarlyStop) {
  auto         d      = fixture();
  const auto   before = d;
  Warp_options o;
  o.enabled         = true;
  o.bins            = 4;
  o.iterations      = 1;
  o.seconds         = 1;
  o.updates         = 5;
  o.warmup          = .1;
  const auto prefix = std::string(std::getenv("TEST_TMPDIR")) + "/macro";
  Macro_warp controller(o, 100, prefix);
  for (int i = 1; i < 10; ++i) {
    EXPECT_FALSE(controller.refresh(d, {0}, i));
  }
  auto a = controller.refresh(d, {0}, 10);
  ASSERT_TRUE(a);
  ASSERT_EQ(a->size(), 1);
  const auto first = a->front();
  EXPECT_FALSE(controller.refresh(d, {0}, 10));
  EXPECT_FALSE(controller.refresh(d, {0}, 11));
  auto b = controller.refresh(d, {0}, 28);
  ASSERT_TRUE(b);
  ASSERT_EQ(b->size(), 1);
  EXPECT_NEAR(b->front().x, first.x, 1e-6);
  EXPECT_NEAR(b->front().y, first.y, 1e-6);  // no accumulated delta
  EXPECT_EQ(d.node_x, before.node_x);
  EXPECT_EQ(d.net_weights, before.net_weights);
  EXPECT_FALSE(controller.refresh(d, {}, 46));    // fixed-only/no memory skips engine entirely
  EXPECT_FALSE(controller.refresh(d, {0}, 100));  // no optimizer step remains to consume a new endpoint
  controller.finish(30);
  EXPECT_TRUE(std::filesystem::exists(prefix + "_summary.json"));
  std::ifstream     summary(prefix + "_summary.json");
  const std::string text{std::istreambuf_iterator<char>(summary), {}};
  EXPECT_NE(text.find("\"early_termination\":true"), std::string::npos);
  EXPECT_NE(text.find("\"iteration_budget\":100"), std::string::npos);
  Warp_options disabled = o;
  disabled.enabled      = false;
  Macro_warp off(disabled, 100, prefix + "_off");
  EXPECT_FALSE(off.refresh(d, {0}, 50));
  auto zero_weight   = o;
  zero_weight.weight = 0;
  Macro_warp zero(zero_weight, 100, prefix + "_zero");
  EXPECT_FALSE(zero.refresh(d, {0}, 50));  // zero force must not trigger a momentum restart
}
TEST(Warpspace, InvalidFieldClearsPullsAtomically) {
  auto d      = fixture();
  d.node_x[1] = d.node_y[1] = 0;
  d.node_size_x[1]          = 100;
  d.node_size_y[1]          = 100;  // infeasible occupied/free density
  Warp_options o;
  o.enabled    = true;
  o.bins       = 4;
  o.iterations = 1;
  o.seconds    = 1;
  Macro_warp controller(o, 10, std::string(std::getenv("TEST_TMPDIR")) + "/failure");
  auto       result = controller.refresh(d, {0}, 1);
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->empty());
}
TEST(Warpspace, SixHundredStepPreviewStartsAtFourHundredAndRefreshesEveryTwentyFive) {
  const auto   d = fixture();
  Warp_options o;
  o.enabled    = true;
  o.bins       = 4;
  o.iterations = 1;
  o.seconds    = 1;
  o.warmup     = 2. / 3.;
  o.updates    = 8;
  Macro_warp       controller(o, 600, std::string(std::getenv("TEST_TMPDIR")) + "/preview_600");
  std::vector<int> applied;
  for (int iteration = 1; iteration <= 600; ++iteration) {
    if (auto update = controller.refresh(d, {0}, iteration)) {
      ASSERT_EQ(update->size(), 1);
      applied.push_back(iteration);
    }
  }
  EXPECT_EQ(applied, (std::vector<int>{400, 425, 450, 475, 500, 525, 550, 575}));
  controller.finish(600);
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Warpspace, FrozenAnchorsAreNotRefreshedAfterGeometryChanges) {
  auto         d = fixture();
  Warp_options o;
  o.enabled = o.once_after_density = true;
  o.bins                           = 4;
  o.iterations                     = 1;
  o.seconds                        = 1;
  Macro_warp controller(o, 100, std::string(std::getenv("TEST_TMPDIR")) + "/frozen");
  // Native caller withholds callbacks until density passes; no timer needed.
  Macro_warp too_late(o, 100, std::string(std::getenv("TEST_TMPDIR")) + "/late");
  EXPECT_FALSE(too_late.refresh(d, {0}, 81));
  auto initial = controller.refresh(d, {0}, 40);
  ASSERT_TRUE(initial);
  ASSERT_EQ(initial->size(), 1);
  d.node_x[1] += 20;
  for (int i = 41; i < 100; ++i) {
    EXPECT_FALSE(controller.refresh(d, {0}, i));
  }
}
TEST(Warpspace, CellSamplesExcludeMacrosAndFixedCells) {
  auto         d = fixture();
  Warp_options o;
  o.bins       = 4;
  o.iterations = 1;
  auto field   = placement_warp(d, {1}, o, true);
  EXPECT_EQ(field.delta.size(), 1);
  EXPECT_THROW(placement_warp(d, {0}, o, true), std::invalid_argument);
  EXPECT_THROW(placement_warp(d, {2}, o, true), std::invalid_argument);
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Warpspace, StabilityStartOpensOnceThenStrengthStaysConstant) {
  const auto   d = fixture();
  Warp_options o;
  o.enabled          = true;
  o.bins             = 4;
  o.iterations       = 1;
  o.seconds          = 1;
  o.weight           = .7;
  o.start_stability  = .2;
  o.refresh_interval = 25;
  o.updates          = 1;
  Macro_warp          controller(o, 150, std::string(std::getenv("TEST_TMPDIR")) + "/stability_start");
  std::vector<int>    applied;
  std::vector<double> weights;
  for (int i = 1; i <= 150; ++i) {
    if (auto update = controller.refresh(d, {0}, i)) {
      ASSERT_FALSE(update->empty());
      applied.push_back(i);
      weights.push_back(update->front().weight);
    }
    controller.observe_motion(i, i <= 50 ? 100 : i < 70 ? 20 : i < 100 ? 19 : 1000, true);
  }
  EXPECT_EQ(applied, (std::vector<int>{71, 96, 121, 146}));
  ASSERT_EQ(weights.size(), 4);
  EXPECT_DOUBLE_EQ(weights[0], .7);
  EXPECT_DOUBLE_EQ(weights[1], .7);
  EXPECT_DOUBLE_EQ(weights[2], .7);
  EXPECT_DOUBLE_EQ(weights[3], .7);
  // No defined reference is not evidence of stability.
  Macro_warp zero(o, 150, std::string(std::getenv("TEST_TMPDIR")) + "/zero_reference");
  for (int i = 1; i < 150; ++i) {
    EXPECT_FALSE(zero.refresh(d, {0}, i));
    zero.observe_motion(i, 0, true);
  }
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Warpspace, MacroTargetsRespectFullBodyHaloAndPositionBox) {
  auto d           = fixture();
  d.node_size_x[0] = 40;
  d.node_size_y[0] = 20;
  d.node_x[0]      = 10;
  d.node_y[0]      = 10;
  d.node_halo_x    = {3, 0, 0};
  d.node_halo_y    = {4, 0, 0};
  Warp_field field;
  field.delta.emplace(0, warp::Point{1000, -1000});
  project_warp_macro_targets(field, d);
  EXPECT_TRUE(field.macro_targets_projected);
  EXPECT_EQ(field.projected_samples, 1);
  EXPECT_DOUBLE_EQ(field.unprojected_delta.at(0).x, 1000);
  EXPECT_DOUBLE_EQ(30 + field.delta.at(0).x, 77);
  EXPECT_DOUBLE_EQ(20 + field.delta.at(0).y, 14);
  d.node_boxes = {
      {20, 20, 30, 30},
      { 0,  0, 90, 90},
      { 0,  0, 90, 90}
  };
  field.delta.at(0) = {1000, -1000};
  project_warp_macro_targets(field, d);
  EXPECT_DOUBLE_EQ(30 + field.delta.at(0).x, 50);
  EXPECT_DOUBLE_EQ(20 + field.delta.at(0).y, 30);
  field.delta.at(0) = {10.1, 10.2};  // A reachable target is unchanged.
  project_warp_macro_targets(field, d);
  EXPECT_EQ(field.projected_samples, 0);
  EXPECT_EQ(field.delta.at(0).x, 10.1);
  EXPECT_EQ(field.delta.at(0).y, 10.2);
  d.node_size_x[0] = 100;
  EXPECT_THROW(project_warp_macro_targets(field, d), std::runtime_error);
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Warpspace, ProgrammableIntervalContinuesBeyondLegacyBudget) {
  const auto   d = fixture();
  Warp_options o;
  o.enabled          = true;
  o.weight           = .7;
  o.refresh_interval = 7;
  o.warmup           = 0;
  o.updates          = 1;
  o.bins             = 4;
  o.iterations       = 1;
  o.seconds          = 1;
  Macro_warp       controller(o, 31, std::string(std::getenv("TEST_TMPDIR")) + "/periodic_constant");
  std::vector<int> applied;
  for (int i = 1; i <= 31; ++i) {
    if (auto pulls = controller.refresh(d, {0}, i)) {
      ASSERT_FALSE(pulls->empty());
      EXPECT_DOUBLE_EQ(pulls->front().weight, .7);
      applied.push_back(i);
    }
  }
  EXPECT_EQ(applied, (std::vector<int>{1, 8, 15, 22, 29}));
  o.refresh_interval = -1;
  EXPECT_THROW(validate_warp_options(o), std::invalid_argument);
}
TEST(Warpspace, StartGateRequiresCompleteMotionWindowButNeverClosesAgain) {
  const auto   d = fixture();
  Warp_options o;
  o.enabled          = true;
  o.weight           = .7;
  o.start_stability  = .2;
  o.refresh_interval = 25;
  o.bins             = 4;
  o.iterations       = 1;
  o.seconds          = 1;
  Macro_warp controller(o, 150, std::string(std::getenv("TEST_TMPDIR")) + "/constant_reset");
  for (int i = 1; i <= 50; ++i) {
    controller.observe_motion(i, 100, true);
  }
  EXPECT_FALSE(controller.refresh(d, {0}, 51));
  controller.observe_motion(70, 0, false);
  for (int i = 71; i < 80; ++i) {
    controller.observe_motion(i, 9, true);
  }
  EXPECT_FALSE(controller.refresh(d, {0}, 80));
  controller.observe_motion(80, 9, true);
  ASSERT_TRUE(controller.refresh(d, {0}, 81));
  controller.observe_motion(90, 0, false);
  auto again = controller.refresh(d, {0}, 106);
  ASSERT_TRUE(again);
  ASSERT_FALSE(again->empty());
  EXPECT_DOUBLE_EQ(again->front().weight, .7);
}
}  // namespace livehd::placement
