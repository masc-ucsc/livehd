// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gap_forces.hpp"

#include "gtest/gtest.h"

namespace b  = livehd::placement::buffering;
namespace dp = DreamPlace::native;
namespace {
dp::Design design() {
  dp::Design d;
  d.layout      = {0, 0, 100, 10, 1, 1};
  d.node_x      = {30, 0, 90};
  d.node_y      = {2, 2, 2};
  d.node_size_x = d.node_size_y = {1, 1, 1};
  d.num_movable                 = 1;
  d.pin2node                    = {0, 1, 2};
  d.pin_offset_x = d.pin_offset_y = {0, 0, 0};
  d.flat_net2pin                  = {0, 1, 2};
  d.flat_net2pin_start            = {0, 3};
  d.build_maps();
  d.validate();
  return d;
}
b::Gap_force_payload weights() {
  return {
      {-1,  0,  0},
      { 0, .4, .6},
      2
  };
}
b::Gap_entry_pull pull() {
  return {
      "n",
      0,
      0,
      {70, 2},
      .25,
      b::Net_class::critical
  };
}
}  // namespace
TEST(GapForces, InvalidUnitsAndBoundsAreRejected) {
  EXPECT_THROW(b::bind_gap_forces(design(),
                                  weights(),
                                  {
                                      {"n", 0}
  },
                                  {pull()},
                                  0),
               std::invalid_argument);
  EXPECT_THROW(b::bind_gap_forces(design(),
                                  weights(),
                                  {
                                      {"n", 0}
  },
                                  {pull()},
                                  1,
                                  2),
               std::invalid_argument);
}
#if DREAMPLACE_NATIVE_PIN_TIMING_API >= 3
TEST(GapForces, CriticalGapSuppressesOnlyConflictingStraightPairs) {
  auto       d      = design();
  const auto base   = weights();
  auto       result = b::bind_gap_forces(d,
                                   base,
                                         {
                                       {"n", 0}
  },
                                         {pull()},
                                   1);
  EXPECT_TRUE(result.supported_api);
  EXPECT_EQ(result.applied, 1);
  EXPECT_EQ(result.suppressed_pairs, 1);
  ASSERT_EQ(result.timing.gap_pulls.size(), 1);
  EXPECT_EQ(result.timing.gap_pulls[0].suppressed_sinks, (std::vector<int>{1}));
  EXPECT_DOUBLE_EQ(result.timing.gap_pulls[0].x, 70);
  EXPECT_EQ(result.timing.weight, base.weight);
  EXPECT_FALSE(d.pin_timing);
  d.pin_timing = result.timing;
  d.validate();
  const auto force = dp::detail::pin_timing_force(d, .7);
  EXPECT_LT(force.x[0], 0);
  EXPECT_DOUBLE_EQ(force.x[1], 0);
  EXPECT_GT(force.x[2], 0);
}
TEST(GapForces, OutsideWindowNeverSuppressesTheStraightObjective) {
  auto p      = pull();
  p.priority  = b::Net_class::outside_window;
  auto result = b::bind_gap_forces(design(),
                                   weights(),
                                   {
                                       {"n", 0}
  },
                                   {p},
                                   1);
  EXPECT_EQ(result.suppressed_pairs, 0);
  ASSERT_EQ(result.timing.gap_pulls.size(), 1);
  EXPECT_FALSE(result.timing.gap_pulls[0].critical);
}
TEST(GapForces, MissingVirtualAndInactiveDriversStayExplicit) {
  auto virtual_source         = pull();
  virtual_source.driver_stage = 1;
  auto absent                 = pull();
  absent.owner                = "missing";
  auto result                 = b::bind_gap_forces(design(),
                                   weights(),
                                                   {
                                       {"n", 0}
  },
                                                   {virtual_source, absent},
                                   1);
  EXPECT_EQ(result.applied, 0);
  EXPECT_EQ(result.virtual_driver, 1);
  EXPECT_EQ(result.unbound_driver, 1);
  auto off        = weights();
  off.coefficient = 0;
  result          = b::bind_gap_forces(design(),
                              off,
                                       {
                                  {"n", 0}
  },
                                       {pull()},
                              1);
  EXPECT_EQ(result.no_active_force, 1);
  EXPECT_TRUE(result.timing.gap_pulls.empty());
}
TEST(GapForces, FixedDriversNeverSuppressMovableSinkForces) {
  auto d        = design();
  d.num_movable = 0;
  auto result   = b::bind_gap_forces(d,
                                   weights(),
                                     {
                                       {"n", 0}
  },
                                     {pull()},
                                   1);
  EXPECT_EQ(result.applied, 0);
  EXPECT_EQ(result.fixed_driver, 1);
  EXPECT_EQ(result.suppressed_pairs, 0);
  EXPECT_TRUE(result.timing.gap_pulls.empty());
  EXPECT_EQ(result.timing.weight, weights().weight);
}
TEST(GapForces, EveryCheckpointClearsObsoleteGapTargets) {
  auto old  = b::bind_gap_forces(design(),
                                weights(),
                                 {
                                    {"n", 0}
  },
                                 {pull()},
                                1);
  auto next = b::bind_gap_forces(design(), old.timing, {}, {}, 1);
  EXPECT_TRUE(next.timing.gap_pulls.empty());
  EXPECT_EQ(next.timing.weight, weights().weight);
}
TEST(GapForces, CoordinateUnitsAndFractionsRemainBounded) {
  auto p      = pull();
  p.target    = {.07, .002};
  auto result = b::bind_gap_forces(design(),
                                   weights(),
                                   {
                                       {"n", 0}
  },
                                   {p},
                                   1000);
  ASSERT_EQ(result.timing.gap_pulls.size(), 1);
  EXPECT_DOUBLE_EQ(result.timing.gap_pulls[0].x, 70);
  EXPECT_THROW(b::bind_gap_forces(design(),
                                  weights(),
                                  {
                                      {"n", 0}
  },
                                  {pull(), pull()},
                                  1),
               std::invalid_argument);
  EXPECT_THROW(b::bind_gap_forces(design(),
                                  weights(),
                                  {
                                      {"n", 99}
  },
                                  {pull()},
                                  1),
               std::invalid_argument);
}
TEST(GapForces, CheckpointPayloadActuallyDrivesNativeGp) {
  auto        control = design(), target = design();
  dp::Options o;
  o.max_iterations = o.min_iterations = 8;
  o.bins_x                            = 8;
  o.bins_y                            = 4;
  o.num_threads                       = 1;
  o.add_fillers                       = false;
  o.legalize = o.detailed_place = false;
  o.checkpoint_interval         = 1;
  o.checkpoint                  = [](const dp::Design&, int iteration) {
    dp::GpUpdate u;
    if (iteration == 1) {
      u.pin_timing = weights();
    }
    return u;
  };
  dp::place(control, o);
  o.checkpoint = [](const dp::Design& d, int iteration) {
    dp::GpUpdate u;
    if (iteration == 1) {
      u.pin_timing = b::bind_gap_forces(d,
                                        weights(),
                                        {
                                            {"n", 0}
      },
                                        {pull()},
                                        1)
                         .timing;
    }
    return u;
  };
  dp::place(target, o);
  EXPECT_GT(target.node_x[0], control.node_x[0]);
  EXPECT_DOUBLE_EQ(target.node_x[1], 0);
  EXPECT_DOUBLE_EQ(target.node_x[2], 90);
}
#else
TEST(GapForces, PublishedDependencyReportsMissingGapApiExplicitly) {
  const auto result = b::bind_gap_forces(design(),
                                         weights(),
                                         {
                                             {"n", 0}
  },
                                         {pull()},
                                         1);
  EXPECT_FALSE(result.supported_api);
  EXPECT_EQ(result.applied, 0);
  EXPECT_EQ(result.timing, weights());
}
#endif
