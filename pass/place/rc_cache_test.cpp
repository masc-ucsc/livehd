// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "rc_cache.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
Rc_geometry net(std::string driver = "d") {
  return {
      driver,
      {{driver, {0, 0}}, {driver + "_inside", {4, 0}}, {driver + "_end", {10, 0}}},
      "load1"
  };
}
Rc_cache::Built star(const Rc_geometry& geometry, const Rc_policy& policy) {
  Rc_cache::Built result;
  result.rc.driver = geometry.driver;
  for (const auto& pin : geometry.pins) {
    result.rc.nodes.push_back({pin.first, 0});
  }
  const auto root = geometry.pins.front().second;
  for (unsigned i = 1; i < geometry.pins.size(); ++i) {
    const auto xy = geometry.pins[i].second;
    const auto dx = std::abs(root.first - xy.first) / policy.dbu, dy = std::abs(root.second - xy.second) / policy.dbu;
    const auto cap                     = dx * policy.per_um[1] + dy * policy.per_um[3];
    result.rc.nodes[0].capacitance_ff += cap / 2;
    result.rc.nodes[i].capacitance_ff += cap / 2;
    result.rc.edges.push_back({0, i, dx * policy.per_um[0] + dy * policy.per_um[2]});
  }
  return result;
}
const Rc_policy policy{
    {2, 3, 5, 7},
    1, "test technology"
};

TEST(RcCache, IndividualPinMotionWithinUnchangedExtremaRebuilds) {
  Rc_cache   cache;
  auto       geometry = net();
  unsigned   calls    = 0;
  const auto build    = [&](const auto& g, const auto& p) {
    ++calls;
    return star(g, p);
  };
  const auto first = cache.update({geometry}, policy, build);
  EXPECT_EQ(first.stats.rebuilt, 1);
  EXPECT_EQ(first.stats.pins_inspected, 3);
  EXPECT_EQ(first.nets[0]->built->rc.nodes[0].capacitance_ff, 21);
  const auto repeated = cache.update({geometry}, policy, build);
  EXPECT_EQ(repeated.stats.reused, 1);
  EXPECT_EQ(repeated.stats.timing_dirty, 0);
  EXPECT_EQ(repeated.nets[0], first.nets[0]);
  geometry.pins[1].second.first = 6;
  const auto moved              = cache.update({geometry}, policy, build);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(moved.stats.rebuilt, 1);
  EXPECT_EQ(moved.timing_dirty_drivers, std::vector<std::string>{"d"});
  EXPECT_EQ(moved.nets[0]->built->rc.nodes[0].capacitance_ff, 24);
  Rc_cache fresh;
  EXPECT_EQ(moved.nets[0]->built->rc, fresh.update({geometry}, policy, star).nets[0]->built->rc);
  EXPECT_EQ(first.nets[0]->built->rc.nodes[0].capacitance_ff, 21);  // old snapshot survives
}

TEST(RcCache, ElectricalRevisionRetainsWireButDirtiesTiming) {
  Rc_cache   cache;
  auto       geometry    = net();
  const auto before      = cache.update({geometry}, policy, star);
  geometry.load_identity = "different master and pin loads";
  auto after             = cache.update({geometry}, policy, star);
  EXPECT_EQ(after.stats.rebuilt, 0);
  EXPECT_EQ(after.stats.reused, 1);
  EXPECT_EQ(after.stats.timing_dirty, 1);
  EXPECT_EQ(after.nets[0]->built, before.nets[0]->built);
  auto changed = policy;
  ++changed.timing_generation;
  after = cache.update({geometry}, changed, star);
  EXPECT_EQ(after.stats.rebuilt, 0);
  EXPECT_EQ(after.stats.timing_dirty, 1);
  EXPECT_EQ(after.nets[0]->built, before.nets[0]->built);
  ++changed.geometry_generation;
  EXPECT_EQ(cache.update({geometry}, changed, star).stats.rebuilt, 1);
}

TEST(RcCache, MembershipOrderingRemovalAndRollback) {
  Rc_cache cache;
  auto     a = net("a"), b = net("b");
  auto     original  = cache.update({a, b}, policy, star);
  auto     reordered = cache.update({b, a}, policy, star);
  EXPECT_EQ(reordered.stats.reused, 2);
  EXPECT_EQ(reordered.nets[0], original.nets[1]);
  a.pins.erase(a.pins.begin() + 1);
  const auto edited = cache.update({a}, policy, star);
  EXPECT_EQ(edited.stats.rebuilt, 1);
  EXPECT_EQ(edited.stats.removed, 1);
  EXPECT_EQ(cache.size(), 1);
  const auto restored = cache.update({net("a"), b}, policy, star);
  EXPECT_EQ(restored.stats.rebuilt, 2);
  EXPECT_EQ(restored.nets[0]->built->rc, original.nets[0]->built->rc);
  EXPECT_EQ(restored.nets[1]->built->rc, original.nets[1]->built->rc);
  EXPECT_EQ(cache.update({}, policy, star).stats.removed, 2);
  EXPECT_EQ(cache.size(), 0);
}

TEST(RcCache, FailedTrialPreservesPriorGeneration) {
  Rc_cache   cache;
  auto       a = net("a"), b = net("b");
  const auto old          = cache.update({a, b}, policy, star);
  a.pins[1].second.second = 3;
  const auto fail         = [](const auto& g, const auto& p) {
    if (g.driver == "c") {
      throw std::runtime_error("trial construction failure");
    }
    return star(g, p);
  };
  EXPECT_THROW(cache.update({a, net("c")}, policy, fail), std::runtime_error);
  EXPECT_EQ(cache.size(), 2);
  const auto restored = cache.update({net("a"), b}, policy, star);
  EXPECT_EQ(restored.stats.reused, 2);
  EXPECT_EQ(restored.nets[0], old.nets[0]);
  EXPECT_EQ(restored.nets[1], old.nets[1]);
}

TEST(RcCache, PolicyAndPinOffsetChangesInvalidateWireGeometry) {
  Rc_cache cache;
  auto     geometry = net();
  auto     changed  = policy;
  cache.update({geometry}, changed, star);
  for (auto& value : changed.per_um) {
    value *= 2;
    EXPECT_EQ(cache.update({geometry}, changed, star).stats.rebuilt, 1);
  }
  changed.dbu = 2;
  EXPECT_EQ(cache.update({geometry}, changed, star).stats.rebuilt, 1);
  changed.identity = "new routing setup";
  EXPECT_EQ(cache.update({geometry}, changed, star).stats.rebuilt, 1);
  geometry.pins[1].second.second += .25;  // orientation/pin offset, no cell-origin change
  EXPECT_EQ(cache.update({geometry}, changed, star).stats.rebuilt, 1);
}

TEST(RcCache, CoincidentPinsAndZeroWirePolicyStayFinite) {
  Rc_cache cache;
  auto     geometry = net();
  for (auto& pin : geometry.pins) {
    pin.second = {0, 0};
  }
  const auto output = cache.update({geometry}, Rc_policy{}, star);
  EXPECT_EQ(output.stats.rebuilt, 1);
  for (const auto& node : output.nets[0]->built->rc.nodes) {
    EXPECT_EQ(node.capacitance_ff, 0);
  }
  for (const auto& edge : output.nets[0]->built->rc.edges) {
    EXPECT_EQ(edge.resistance_ohm, 0);
  }
  EXPECT_EQ(cache.update({geometry}, Rc_policy{}, star).stats.reused, 1);
}

TEST(RcCache, RejectsInvalidFramesAndMalformedBuilderWithoutReplacingCache) {
  Rc_cache   cache;
  const auto before = cache.update({net()}, policy, star);
  EXPECT_THROW(cache.update({net(), net()}, policy, star), std::invalid_argument);
  auto invalid          = net();
  invalid.pins[1].first = invalid.pins[0].first;
  EXPECT_THROW(cache.update({invalid}, policy, star), std::invalid_argument);
  invalid        = net();
  invalid.driver = "missing";
  EXPECT_THROW(cache.update({invalid}, policy, star), std::invalid_argument);
  invalid                      = net();
  invalid.pins[1].second.first = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(cache.update({invalid}, policy, star), std::invalid_argument);
  auto bad_policy      = policy;
  bad_policy.per_um[3] = -1;
  EXPECT_THROW(cache.update({net()}, bad_policy, star), std::invalid_argument);
  const auto malformed = [](const auto& g, const auto& p) {
    auto built            = star(g, p);
    built.rc.nodes[1].pin = "wrong pin";
    return built;
  };
  EXPECT_THROW(cache.update({net("new")}, policy, malformed), std::invalid_argument);
  const auto restored = cache.update({net()}, policy, star);
  EXPECT_EQ(restored.stats.reused, 1);
  EXPECT_EQ(restored.nets[0], before.nets[0]);
}
}  // namespace
}  // namespace livehd::placement
