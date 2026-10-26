// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "legalization.hpp"

#include <limits>
#include <stdexcept>

#include "gtest/gtest.h"

namespace tc = livehd::placement::timeclosure;
namespace {
tc::Legalization_binding binding() {
  return {
      4,
      3,
      {{"d:Y", 0, 0}, {"near:A", 1, 0}, {"side:A", 2, 0}, {"io", 3, 1}, {"side:B", 2, 1}},
      {0, 3},
      {2, 1, 3, 4},
      {2, 0}
  };
}
tc::Pin_weights timing(double scale = 1) {
  tc::Pin_weights result;
  result.pins["near:A"] = {.driver = "d:Y", .raw = 4 * scale, .weight = .01};
  result.pins["side:A"] = {.driver = "d:Y", .raw = scale, .weight = 1e5};
  result.pins["side:B"] = {.driver = "io", .raw = 2 * scale};
  return result;
}
}  // namespace

TEST(LegalizationPriorities, ProtectsBothMovableOwnersAndKeepsFixedAndDisabledCosts) {
  const auto result = tc::legalization_priorities(binding(), timing(), {9, 4});
  EXPECT_EQ(result.node_criticality, (std::vector<double>{1, 1, .5, 0}));
  EXPECT_EQ(result.net_criticality, (std::vector<double>{1, .5}));
  EXPECT_EQ(result.node_weights, (std::vector<double>{20, 10, 16.5, 4}));
  EXPECT_EQ(result.net_weights, (std::vector<double>{10, 0}));
  EXPECT_EQ(result.weighted_pins, 3);
  EXPECT_EQ(result.protected_movable, 3);
  EXPECT_EQ(result.protected_nets, 1);
}
TEST(LegalizationPriorities, RawNormalizationIsUnitAndFanoutInvariant) {
  const auto original = tc::legalization_priorities(binding(), timing(), {9, 4});
  const auto scaled   = tc::legalization_priorities(binding(), timing(1000), {9, 4});
  EXPECT_EQ(original.node_weights, scaled.node_weights);
  EXPECT_EQ(original.net_weights, scaled.net_weights);
  auto pins = binding();
  pins.pins.push_back({"replica:A", 2, 0});
  auto weights              = timing();
  weights.pins["replica:A"] = {.driver = "d:Y", .raw = 1};
  const auto duplicate      = tc::legalization_priorities(pins, weights, {9, 4});
  EXPECT_EQ(original.node_weights, duplicate.node_weights);
  EXPECT_EQ(original.net_weights, duplicate.net_weights);
}
TEST(LegalizationPriorities, ZeroStrengthPreservesEmptyAndExistingRepresentations) {
  auto       pins     = binding();
  const auto existing = tc::legalization_priorities(pins, timing(), {});
  EXPECT_EQ(existing.node_weights, pins.node_weights);
  EXPECT_EQ(existing.net_weights, pins.net_weights);
  EXPECT_EQ(existing.protected_movable, 0);
  EXPECT_EQ(existing.protected_nets, 0);
  pins.node_weights.clear();
  pins.net_weights.clear();
  const auto empty = tc::legalization_priorities(pins, timing(), {});
  EXPECT_TRUE(empty.node_weights.empty());
  EXPECT_TRUE(empty.net_weights.empty());
}
TEST(LegalizationPriorities, RejectsInvalidIdentityCostsAndOverflowWithoutChangingInput) {
  const auto original           = binding();
  auto       pins               = original;
  auto       weights            = timing();
  weights.pins["near:A"].driver = "io";
  EXPECT_THROW(tc::legalization_priorities(pins, weights, {9, 4}), std::invalid_argument);
  weights                    = timing();
  weights.pins["near:A"].raw = std::numeric_limits<double>::infinity();
  EXPECT_THROW(tc::legalization_priorities(pins, weights, {9, 4}), std::invalid_argument);
  pins.pins[1].name = pins.pins[0].name;
  EXPECT_THROW(tc::legalization_priorities(pins, timing(), {9, 4}), std::invalid_argument);
  pins                 = original;
  pins.node_weights[0] = .5;
  EXPECT_THROW(tc::legalization_priorities(pins, timing(), {9, 4}), std::invalid_argument);
  pins                = original;
  pins.net_weights[0] = std::numeric_limits<double>::max();
  EXPECT_THROW(tc::legalization_priorities(pins, timing(), {9, 4}), std::invalid_argument);
  EXPECT_EQ(pins.node_weights, original.node_weights);
  EXPECT_THROW(tc::legalization_priorities(original, timing(), {100, 0}), std::invalid_argument);
}
TEST(LegalizationPriorities, FixedOnlyBindingCannotAcquireDisplacementPriority) {
  auto pins         = binding();
  pins.movable      = 0;
  const auto result = tc::legalization_priorities(pins, timing(), {99, 0});
  EXPECT_EQ(result.node_weights, pins.node_weights);
  EXPECT_EQ(result.protected_movable, 0);
  EXPECT_EQ(result.net_weights, pins.net_weights);
}
