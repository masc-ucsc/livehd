// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "preroute_demand.hpp"

#include <numeric>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
namespace dp = DreamPlace::native;
struct Fixture {
  dp::Design          design;
  dp::RudyOptions     options;
  Route_capacity_grid grid{{0, 0, 100, 100}, 2, 2, {"M2", "M3"}, .3};
  Fixture() {
    design.layout      = {0, 0, 100, 100, 1, 1};
    design.node_x      = {10, 30, 60, 80, 10, 80};
    design.node_y      = {10, 30, 60, 80, 80, 10};
    design.node_size_x = design.node_size_y = std::vector<double>(6, 1);
    design.pin_offset_x = design.pin_offset_y = std::vector<double>(6, 0);
    design.pin2node                           = {0, 1, 2, 3, 4, 5};
    design.flat_net2pin                       = design.pin2node;
    design.flat_net2pin_start                 = {0, 2, 4, 6};
    design.net_weights                        = {1, 1, 1};
    design.build_maps();
    design.net_mask = {1, 1, 0};
    options.bins_x = options.bins_y = 2;
    options.horizontal_capacity = options.vertical_capacity = .1;
    options.model                                           = dp::RudyModel::rsmt;
    options.macro_blockages = options.pin_access = true;
  }
};
}  // namespace
TEST(PrerouteDemand, MatchesFreshRsmtWithoutCandidateAndPreservesInput) {
  Fixture         f;
  Preroute_demand state(f.design, f.options, f.grid);
  ASSERT_TRUE(state.prepare());
  auto trial            = state.without(0);
  auto reference        = f.design;
  reference.net_mask[0] = 0;
  const auto expected   = dp::rudy(reference, f.options);
  EXPECT_EQ(trial.remaining.horizontal, expected.horizontal);
  EXPECT_EQ(trial.remaining.vertical, expected.vertical);
  EXPECT_EQ(trial.remaining.pin_utilization, expected.pin_utilization);
  EXPECT_EQ(f.design.net_mask, (std::vector<std::uint8_t>{1, 1, 0}));
  EXPECT_EQ(state.without(0).remaining.horizontal, trial.remaining.horizontal);
  const auto stale = state.without(1);
  state.commit(trial);
  EXPECT_THROW(state.commit(trial), std::invalid_argument);
  EXPECT_THROW(state.commit(stale), std::invalid_argument);
  EXPECT_THROW(state.without(0), std::invalid_argument);
  const auto last = state.without(1);
  EXPECT_EQ(std::accumulate(last.remaining.horizontal.begin(), last.remaining.horizontal.end(), 0.), 0);
  EXPECT_EQ(std::accumulate(last.remaining.vertical.begin(), last.remaining.vertical.end(), 0.), 0);
  ASSERT_TRUE(state.prepare());  // Rebuild against the already committed mask.
  EXPECT_EQ(state.without(1).remaining.horizontal, last.remaining.horizontal);
  EXPECT_EQ(state.without(1).remaining.pin_utilization, last.remaining.pin_utilization);
  EXPECT_THROW(state.without(2), std::invalid_argument);  // inherited clock/reset mask
}
TEST(PrerouteDemand, ExistingWiresDoNotAlsoConsumeEstimatedDemand) {
  Fixture f;
  for (const bool cached : {false, true}) {
    Preroute_demand state(f.design, f.options, f.grid, {1});
    if (cached) {
      ASSERT_TRUE(state.prepare());
    }
    const auto trial     = state.without(0);
    auto       reference = f.design;
    reference.net_mask   = {0, 0, 0};
    const auto expected  = dp::rudy(reference, f.options);
    EXPECT_EQ(trial.remaining.horizontal, expected.horizontal);
    EXPECT_EQ(trial.remaining.vertical, expected.vertical);
    EXPECT_EQ(trial.remaining.pin_utilization, expected.pin_utilization);
    EXPECT_THROW(state.without(1), std::invalid_argument);
    EXPECT_EQ(f.design.net_mask, (std::vector<std::uint8_t>{1, 1, 0}));
  }
  EXPECT_THROW(Preroute_demand(f.design, f.options, f.grid, {3}), std::invalid_argument);
}
TEST(PrerouteDemand, RejectsForeignTrialsAndMismatchedGrid) {
  Fixture         f;
  Preroute_demand a(f.design, f.options, f.grid), b(f.design, f.options, f.grid);
  auto            trial = a.without(0);
  EXPECT_THROW(b.commit(trial), std::invalid_argument);
  f.grid.bins_x = 3;
  EXPECT_THROW(Preroute_demand(f.design, f.options, f.grid), std::invalid_argument);
}
TEST(PrerouteDemand, RestoresMaskWhenRsmtThrows) {
  Fixture f;
  // Net 1 has four distinct pins and requires FLUTE's LUT initializer.
  f.design.flat_net2pin_start = {0, 2, 6};
  f.design.net_weights        = {1, 1};
  f.design.net_mask.clear();
  f.design.build_maps();
  int calls            = 0;
  f.options.initialize = [&] {
    ++calls;
    throw std::runtime_error("test LUT failure");
  };
  Preroute_demand state(f.design, f.options, f.grid);
  EXPECT_THROW(state.without(0), std::runtime_error);
  EXPECT_THROW(state.without(0), std::runtime_error);
  EXPECT_EQ(calls, 2);
  EXPECT_FALSE(state.prepare());  // Failed caching must retain fallback queries.
  // Removing the failing four-pin net still retains net 0 after both errors.
  const auto trial = state.without(1);
  EXPECT_GT(std::accumulate(trial.remaining.horizontal.begin(), trial.remaining.horizontal.end(), 0.), 0);
}
TEST(PrerouteDemand, DoesNotDiscountPhysicalDemandWithSmallTimingWeight) {
  Fixture f;
  f.design.net_weights[1] = 0;
  Preroute_demand state(f.design, f.options, f.grid);
  const auto      trial = state.without(0);
  EXPECT_GT(std::accumulate(trial.remaining.horizontal.begin(), trial.remaining.horizontal.end(), 0.), 0);
}
TEST(PrerouteDemand, CarriesBlockedAccessAndUnreachableDiagnostics) {
  Fixture f;
  f.design.node_macro     = {0, 0, 0, 0, 1, 0};
  f.design.node_x[4]      = 40;
  f.design.node_y[4]      = 40;
  f.design.node_size_x[4] = 50;
  f.design.node_size_y[4] = 50;
  Preroute_demand state(f.design, f.options, f.grid);
  const auto      trial = state.without(0);
  EXPECT_GT(trial.remaining.blocked_pins, 0);
  EXPECT_GT(trial.remaining.unroutable_nets, 0);
}
}  // namespace livehd::placement
