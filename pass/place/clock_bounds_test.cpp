// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_bounds.hpp"

#include <algorithm>
#include <limits>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
TEST(ClockBounds, JointLeafArrivalsRespectHoldingEdgesPeriodAndBlockedComponents) {
  auto x = solve_leaf_arrivals(
      {
          0,
          50,
          50,
          50,
          40
  },
      {{0, 1, -30, 0}, {1, 2, -20, 0}, {2, 0, 47, 2}, {3, 0, -9, 0}, {0, 3, 0, 0}, {0, 4, -35, 0}, {4, 4, -200, 0}});
  // The two communicating leaves consume 45 ps of hold headroom together.
  EXPECT_NEAR(x[1], 27, 1e-4);
  EXPECT_NEAR(x[2], 45, 1e-4);
  EXPECT_DOUBLE_EQ(x[3], 0);
  EXPECT_DOUBLE_EQ(x[4], 35);
}

TEST(ClockBounds, ImprovesReachableLeavesBesideATightCycle) {
  // Leaves 1 and 2 must move together to preserve the two failing paths.
  // Delaying both by 10 ps and leaf 3 by 30 ps fixes the other two paths.
  std::vector<Leaf_slack_edge> paths{{1, 2, -15, 0}, {2, 1, -2, 4}, {0, 1, -10, 0}, {2, 3, -20, 0}};
  EXPECT_EQ(solve_leaf_arrivals({0, 40, 40, 40}, paths), (std::vector<double>{0, 10, 10, 30}));
  std::reverse(paths.begin(), paths.end());
  EXPECT_EQ(solve_leaf_arrivals({0, 40, 40, 40}, paths), (std::vector<double>{0, 10, 10, 30}));
}

TEST(ClockBounds, FixedLaunchAndExhaustedBudgetStillProtectTheirPaths) {
  // Leaf 1 cannot shift without hurting a path to the fixed port. Leaf 2
  // has also consumed its budget. Both can launch into a later leaf.
  const auto x = solve_leaf_arrivals({0, 50, 0, 50}, {{1, 0, -8, 0}, {0, 2, -4, 0}, {1, 3, -14, 0}, {2, 3, -20, 0}});
  EXPECT_EQ(x, (std::vector<double>{0, 0, 0, 20}));
}

TEST(ClockBounds, PreservingGlobalHoldCanHideANewViolation) {
  Endpoint_hold before{
      {"A:D", -5},
      {"B:D", -3},
      {"C:D",  1}
  },
      after{{"A:D", -5}, {"B:D", 2}, {"C:D", -2}};
  const auto result = check_clock_bounds(before, after, {}, {}, .05, 0);
  ASSERT_FALSE(result.allowed());
  ASSERT_TRUE(result.hold);
  EXPECT_EQ(result.hold->pin, "C:D");
  EXPECT_DOUBLE_EQ(result.hold->excess_ps, 2);
}
TEST(ClockBounds, RetriesOnlyRegressingSiblingCapturesOnceEach) {
  const Schedule_leaf first{10, 0, false, 10, 100, "parent"};
  const Schedule_leaf second{20, 2, false, 21, 100, "parent"};
  const Schedule_leaf third{30, 1, false, 31, 100, "parent"};
  std::map<Pin_label, Schedule_leaf> captures{{"A:D", first}, {"B:D", second}, {"C:D", second}, {"D:D", third}};
  Endpoint_hold before{{"A:D", -20}, {"B:D", -4}, {"C:D", -3}, {"D:D", 5}, {"E:D", 10}};
  Endpoint_hold after{{"A:D", -12}, {"B:D", -4.1f}, {"C:D", -3.07f}, {"D:D", 1.9f}, {"E:D", 2}};
  const auto retry = [&] { return sibling_hold_drives(first, captures, before, after, 2); };
  EXPECT_EQ(retry(), (std::map<uint64_t, unsigned>{{20, 3}, {30, 2}}));
  after["D:D"] = 1.99999f;
  EXPECT_EQ(retry(), (std::map<uint64_t, unsigned>{{20, 3}}));
  captures.at("B:D").parent = "unrelated";
  EXPECT_TRUE(retry().empty());
  captures.at("B:D") = first;
  EXPECT_TRUE(retry().empty());
  captures.at("B:D") = second;
  captures.at("B:D").fixed = true;
  EXPECT_TRUE(retry().empty());
  captures.at("B:D").fixed = false;
  captures.at("B:D").steps = std::numeric_limits<unsigned>::max();
  EXPECT_TRUE(retry().empty());
  captures.erase("B:D");
  EXPECT_TRUE(retry().empty());
}

TEST(ClockBounds, ProtectsTheExistingMarginAtEachPin) {
  Endpoint_hold previous{
      {"A:D", -3},
      {"B:D",  5},
      {"C:D", 22}
  },
      edited{{"A:D", -1}, {"B:D", 5}, {"C:D", 12}};
  EXPECT_TRUE(check_clock_bounds(previous, edited, {}, {}, .05, 12).allowed());
  edited["B:D"]    = 4;
  const auto check = check_clock_bounds(previous, edited, {}, {}, .05, 12);
  ASSERT_TRUE(check.hold);
  EXPECT_EQ(check.hold->pin, "B:D");
  EXPECT_DOUBLE_EQ(check.hold->limit_ps, 5);
}
TEST(ClockBounds, UsesEachDomainPeriodAndEveryTransitionRelativeToOriginalArrival) {
  Sink_arrivals base{
      {"A:CK", {"fast", 100, {25, 26, 32, 33, 45, 46, 53, 54}}},
      {"B:CK", {"slow", 400, {25, 26, 32, 33, 45, 46, 53, 54}}}
  };
  auto trial                         = base;
  trial.at("A:CK").arrivals.back()  -= 5;
  trial.at("B:CK").arrivals.front() += 20;
  const auto first                   = check_clock_bounds({}, {}, base, trial, .05, 0);
  ASSERT_TRUE(first.allowed());
  EXPECT_DOUBLE_EQ(first.max_shift_ps, 20);
  trial.at("A:CK").arrivals.back() -= 2;
  const auto second                 = check_clock_bounds({}, {}, base, trial, .05, 0);
  ASSERT_TRUE(second.offset);
  EXPECT_EQ(second.offset->pin, "A:CK");
  EXPECT_DOUBLE_EQ(second.offset->excess_ps, 2);
}
TEST(ClockBounds, RequiresTheSameEndpointsClockWaveformsAndDomains) {
  Sink_arrivals base{
      {"A:CK", {"clock", 100, {10, 20, 30, 40}}}
  },
      other = base;
  EXPECT_FALSE(check_clock_bounds(
                   {
                       {"A:D", 1}
  },
                   {{"B:D", 1}},
                   base,
                   base,
                   .05,
                   0)
                   .allowed());
  other.at("A:CK").clock = "other_clock";
  EXPECT_FALSE(check_clock_bounds({}, {}, base, other, .05, 0).same_coverage);
  other = base;
  other.at("A:CK").arrivals.pop_back();
  EXPECT_FALSE(check_clock_bounds({}, {}, base, other, .05, 0).same_coverage);
}
}  // namespace
}  // namespace livehd::placement
