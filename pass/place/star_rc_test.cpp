// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "star_rc.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "gtest/gtest.h"

namespace livehd::placement::late {
namespace {
const physical::Rc_net reference{
    "d", {{"d", 1}, {"", 3}, {"a", 2}, {"b", 4}},
     {{0, 1, 10}, {1, 2, 20}, {1, 3, 30}}
};
const Rc_policy policy{
    {2, 1, 4, 2},
    1, "analytic technology"
};
Star_net input() {
  return {
      {{"d", 0, 0, {}}, {"a", 10, 0, {2, 3}}, {"b", 20, 0, {5, 7}}},
      &reference
  };
}
TEST(StarRc, AnchorScalesWireOnlyAndMatchesEachTransitionElmore) {
  Star_bank  bank;
  const auto result = bank.evaluate({input()}, policy);
  EXPECT_EQ(result.stats.calibrated, 1);
  EXPECT_EQ(result.stats.pins_evaluated, 3);
  const auto& state = result.nets[0];
  EXPECT_DOUBLE_EQ(state.wire_cap_ratio, 1. / 3);
  EXPECT_DOUBLE_EQ(state.wire_cap_ff, 10);
  EXPECT_DOUBLE_EQ(state.timing.total_cap_ff[0], 17);
  EXPECT_DOUBLE_EQ(state.timing.total_cap_ff[1], 20);
  EXPECT_NEAR(state.timing.elmore_ps[1][0], .24, 1e-14);
  EXPECT_NEAR(state.timing.elmore_ps[1][1], .29, 1e-14);
  EXPECT_NEAR(state.timing.elmore_ps[2][0], .43, 1e-14);
  EXPECT_NEAR(state.timing.elmore_ps[2][1], .52, 1e-14);
  for (unsigned t = 0; t < 2; ++t) {
    EXPECT_GE(state.timing.pi[t].near_cap_ff, 0);
    EXPECT_NEAR(state.timing.pi[t].near_cap_ff + state.timing.pi[t].far_cap_ff, state.timing.total_cap_ff[t], 1e-12);
  }
}
TEST(StarRc, InteriorMotionChangesPredictionWithoutSteinerRefresh) {
  Star_bank bank;
  auto      net = input();
  bank.evaluate({net}, policy);
  net.reference     = nullptr;  // no Steiner input is needed between refreshes
  net.pins[1].x     = 12;       // bounding box remains [0,20]
  const auto result = bank.evaluate({net}, policy);
  EXPECT_EQ(result.stats.reused, 1);
  EXPECT_EQ(result.stats.calibrated, 0);
  EXPECT_NEAR(result.nets[0].wire_cap_ff, 32. / 3, 1e-12);
  EXPECT_NEAR(result.nets[0].timing.total_cap_ff[0], 32. / 3 + 7, 1e-12);
  EXPECT_NEAR(result.nets[0].timing.elmore_ps[1][0], .096 * (.24 / (20 * (2 + 5. / 3) * .001)), 1e-12);
  const auto at_twelve  = result.nets[0].timing.elmore_ps[1][0];
  net.pins[1].x        += 1e-6;
  const auto nearby     = bank.evaluate({net}, policy);
  EXPECT_GT(nearby.nets[0].timing.elmore_ps[1][0], at_twelve);
  EXPECT_LT(nearby.nets[0].timing.elmore_ps[1][0] - at_twelve, 1e-6);
}
TEST(StarRc, ResizingRecomputesLoadDependentCompensationAtFrozenAnchor) {
  Star_bank bank;
  auto      net = input();
  bank.evaluate({net}, policy);
  net.pins[1].load_ff = {10, 20};
  net.reference       = nullptr;
  const auto rebound  = bank.evaluate({net}, policy);
  EXPECT_EQ(rebound.stats.load_rebound, 1);
  EXPECT_EQ(rebound.stats.calibrated, 0);
  EXPECT_EQ(rebound.nets[0].timing.total_cap_ff[0], 25);
  EXPECT_EQ(rebound.nets[0].timing.total_cap_ff[1], 37);
  EXPECT_NEAR(rebound.nets[0].timing.elmore_ps[1][0], .48, 1e-12);
  EXPECT_NEAR(rebound.nets[0].timing.elmore_ps[1][1], .8, 1e-12);
  EXPECT_NEAR(rebound.nets[0].timing.elmore_ps[2][0], .51, 1e-12);
  EXPECT_NEAR(rebound.nets[0].timing.elmore_ps[2][1], .69, 1e-12);
  EXPECT_EQ(bank.evaluate({net}, policy).stats.load_rebound, 0);
  auto changed = policy;
  ++changed.timing_generation;
  EXPECT_EQ(bank.evaluate({net}, changed).stats.load_rebound, 1);
}
TEST(StarRc, CoincidentAnchorRetainsWireAndDelayWithoutZeroDivision) {
  Star_bank bank;
  auto      net = input();
  for (auto& pin : net.pins) {
    pin.x = pin.y = 0;
  }
  const auto anchor = bank.evaluate({net}, policy);
  EXPECT_EQ(anchor.stats.zero_wire_anchors, 1);
  EXPECT_EQ(anchor.nets[0].wire_cap_ff, 10);
  EXPECT_EQ(anchor.nets[0].timing.total_cap_ff[0], 17);
  EXPECT_NEAR(anchor.nets[0].timing.elmore_ps[1][0], .24, 1e-12);
  net.reference     = nullptr;
  net.pins[1].x     = 1e-9;
  const auto nearby = bank.evaluate({net}, policy);
  EXPECT_NEAR(nearby.nets[0].timing.elmore_ps[1][0], .24, 1e-9);
  EXPECT_NEAR(nearby.nets[0].wire_cap_ff, 10, 1e-8);
}
TEST(StarRc, ZeroResistanceAndCapacitanceRemainFinite) {
  Star_bank        bank;
  auto             net  = input();
  physical::Rc_net zero = reference;
  for (auto& node : zero.nodes) {
    node.capacitance_ff = 0;
  }
  for (auto& edge : zero.edges) {
    edge.resistance_ohm = 0;
  }
  net.reference     = &zero;
  const auto result = bank.evaluate({net}, Rc_policy{});
  EXPECT_EQ(result.nets[0].wire_cap_ff, 0);
  EXPECT_EQ(result.nets[0].timing.total_cap_ff[0], 7);
  EXPECT_EQ(result.nets[0].timing.elmore_ps[1][0], 0);
  EXPECT_EQ(result.nets[0].timing.elmore_ps[2][1], 0);
}
TEST(StarRc, DbuConversionAndDirectionalRcAreExplicit) {
  Star_bank a, b;
  auto      net = input(), scaled = input();
  net.pins[1].y = 2;
  scaled        = net;
  for (auto& pin : scaled.pins) {
    pin.x *= 1000;
    pin.y *= 1000;
  }
  auto scaled_policy = policy;
  scaled_policy.dbu  = 1000;
  const auto left    = a.evaluate({net}, policy).nets[0];
  const auto right   = b.evaluate({scaled}, scaled_policy).nets[0];
  EXPECT_DOUBLE_EQ(left.wire_cap_ratio, 10. / 34);
  EXPECT_EQ(left.wire_cap_ff, right.wire_cap_ff);
  EXPECT_EQ(left.timing.elmore_ps, right.timing.elmore_ps);
  EXPECT_EQ(left.timing.total_cap_ff, right.timing.total_cap_ff);
}
TEST(StarRc, PolicyMembershipRefreshAndRemovalCreateNewAnchors) {
  Star_bank bank;
  bank.evaluate({input()}, policy);
  auto changed = policy;
  ++changed.geometry_generation;
  EXPECT_EQ(bank.evaluate({input()}, changed).stats.calibrated, 1);
  EXPECT_EQ(bank.evaluate({input()}, changed, true).stats.calibrated, 1);
  auto net         = input();
  auto ref         = reference;
  net.pins[2].name = ref.nodes[3].pin = "replacement";
  net.reference                       = &ref;
  EXPECT_EQ(bank.evaluate({net}, changed).stats.calibrated, 1);
  EXPECT_EQ(bank.evaluate({}, changed).stats.removed, 1);
  EXPECT_EQ(bank.evaluate({input()}, policy).stats.calibrated, 1);
}
TEST(StarRc, FailedFramePreservesPriorCalibrationAndLoads) {
  Star_bank  bank;
  const auto before  = bank.evaluate({input()}, policy);
  auto       resized = input(), invalid = input();
  resized.pins[1].load_ff = {10, 20};
  invalid.pins[0].name    = "new";
  invalid.reference       = nullptr;
  EXPECT_THROW(bank.evaluate({resized, invalid}, policy), std::invalid_argument);
  auto replay      = input();
  replay.reference = nullptr;
  const auto after = bank.evaluate({replay}, policy);
  EXPECT_EQ(after.stats.reused, 1);
  EXPECT_EQ(after.stats.load_rebound, 0);
  EXPECT_EQ(after.nets[0].timing.elmore_ps, before.nets[0].timing.elmore_ps);
}
TEST(StarRc, InvalidGeometryLoadsAndReferenceAreRejected) {
  Star_bank bank;
  auto      net = input();
  net.pins[1].x = std::numeric_limits<double>::infinity();
  EXPECT_THROW(bank.evaluate({net}, policy), std::invalid_argument);
  net                 = input();
  net.pins[0].load_ff = {1, 1};
  EXPECT_THROW(bank.evaluate({net}, policy), std::invalid_argument);
  net                    = input();
  net.pins[1].load_ff[1] = -1;
  EXPECT_THROW(bank.evaluate({net}, policy), std::invalid_argument);
  net              = input();
  net.pins[1].name = "missing";
  EXPECT_THROW(bank.evaluate({net}, policy), std::invalid_argument);
  auto invalid = policy;
  invalid.dbu  = 0;
  EXPECT_THROW(bank.evaluate({input()}, invalid), std::invalid_argument);
  EXPECT_THROW(bank.evaluate({input(), input()}, policy), std::invalid_argument);
}

TEST(StarRc, AdmittanceCorrectionMatchesAnchorAndStaysPassiveAfterMotion) {
  Star_bank    bank;
  auto         net = input();
  Star_options options;
  options.calibrate_admittance = true;
  std::vector<std::array<double, 2>> caps{
      {1,  1},
      {3,  3},
      {4,  5},
      {9, 11}
  };
  const auto expected = analyze_rc_tree(reference, 0, caps);
  auto       result   = bank.evaluate({net}, policy, false, options);
  for (unsigned t = 0; t < 2; ++t) {
    EXPECT_EQ(result.nets[0].timing.pi[t].near_cap_ff, expected.pi[t].near_cap_ff);
    EXPECT_EQ(result.nets[0].timing.pi[t].far_cap_ff, expected.pi[t].far_cap_ff);
    EXPECT_EQ(result.nets[0].timing.pi[t].resistance_ohm, expected.pi[t].resistance_ohm);
  }
  net.reference = nullptr;
  for (double x : {0., 1e-9, 9.99999, 10.00001, 100., 10000.}) {
    net.pins[1].x = x;
    result        = bank.evaluate({net}, policy, false, options);
    for (unsigned t = 0; t < 2; ++t) {
      const auto& pi = result.nets[0].timing.pi[t];
      EXPECT_GE(pi.near_cap_ff, 0);
      EXPECT_GE(pi.far_cap_ff, 0);
      EXPECT_TRUE(std::isfinite(pi.resistance_ohm));
      EXPECT_GE(pi.resistance_ohm, 0);
      EXPECT_NEAR(pi.near_cap_ff + pi.far_cap_ff, result.nets[0].timing.total_cap_ff[t], 1e-10);
    }
  }
}

TEST(StarRc, IndividualDisplacementRefreshesWithinUnchangedBoundingBox) {
  Star_bank    bank;
  auto         net = input();
  Star_options options;
  options.max_pin_displacement_fraction = .1;
  bank.evaluate({net}, policy, false, options);
  net.pins[1].x = 11;
  EXPECT_EQ(bank.evaluate({net}, policy, false, options).stats.reused, 1);
  net.pins[1].x  = 13;
  auto refreshed = bank.evaluate({net}, policy, false, options);
  EXPECT_EQ(refreshed.stats.displacement_refreshed, 1);
  EXPECT_EQ(refreshed.stats.calibrated, 1);
  EXPECT_DOUBLE_EQ(refreshed.stats.max_displacement_fraction, .15);
  EXPECT_NEAR(refreshed.nets[0].timing.elmore_ps[1][0], .24, 1e-12);
  EXPECT_EQ(bank.evaluate({net}, policy, false, options).stats.reused, 1);
  net.pins[1].x = 19;
  net.reference = nullptr;
  EXPECT_THROW(bank.evaluate({net}, policy, false, options), std::invalid_argument);
  net.pins[1].x = 13;
  EXPECT_EQ(bank.evaluate({net}, policy, false, options).stats.reused, 1);
}

TEST(StarRc, BranchGuardCatchesShortSinkMotionInsideLongNetExtrema) {
  Star_bank net_bank, branch_bank;
  auto      net = input();
  net.pins[1].x = 1;
  net.pins[2].x = 10000;
  Star_options coarse, local;
  coarse.max_pin_displacement_fraction = .1;
  local                                = coarse;
  local.branch_relative_displacement   = true;
  net_bank.evaluate({net}, policy, false, coarse);
  branch_bank.evaluate({net}, policy, false, local);
  net.pins[1].x = 2;
  const auto a  = net_bank.evaluate({net}, policy, false, coarse);
  const auto b  = branch_bank.evaluate({net}, policy, false, local);
  EXPECT_EQ(a.stats.reused, 1);
  EXPECT_EQ(b.stats.displacement_refreshed, 1);
  EXPECT_DOUBLE_EQ(a.stats.max_displacement_fraction, .0001);
  EXPECT_DOUBLE_EQ(b.stats.max_displacement_fraction, 1);
}
}  // namespace
}  // namespace livehd::placement::late
