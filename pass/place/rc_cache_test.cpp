// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "rc_cache.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
const auto layout = std::make_shared<const Rc_layout>(Rc_layout{32, 123});

Rc_geometry net(Rc_pin_id driver = 0) {
  return {
      driver,
      {{driver, {0, 0}}, {driver + 1, {4, 0}}, {driver + 2, {10, 0}}},
      1
  };
}
Rc_cache::Built star(const Rc_geometry& geometry, const Rc_policy& policy) {
  Rc_cache::Built result;
  result.rc.node_cap_ff.resize(geometry.pins.size());
  const auto root = geometry.pins.front().second;
  for (unsigned i = 1; i < geometry.pins.size(); ++i) {
    const auto xy = geometry.pins[i].second;
    const auto dx = std::abs(root.first - xy.first) / policy.dbu, dy = std::abs(root.second - xy.second) / policy.dbu;
    const auto cap            = dx * policy.per_um[1] + dy * policy.per_um[3];
    result.rc.node_cap_ff[0] += cap / 2;
    result.rc.node_cap_ff[i] += cap / 2;
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
  const auto first = cache.update(layout, {geometry}, policy, build);
  EXPECT_EQ(first.stats.rebuilt, 1);
  EXPECT_EQ(first.stats.pins_inspected, 3);
  EXPECT_EQ(first.nets[0]->built->rc.node_cap_ff[0], 21);
  const auto repeated = cache.update(layout, {geometry}, policy, build);
  EXPECT_EQ(repeated.stats.reused, 1);
  EXPECT_EQ(repeated.stats.timing_dirty, 0);
  EXPECT_EQ(repeated.nets[0], first.nets[0]);
  geometry.pins[1].second.first = 6;
  const auto moved              = cache.update(layout, {geometry}, policy, build);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(moved.stats.rebuilt, 1);
  EXPECT_EQ(moved.timing_dirty_drivers, std::vector<Rc_pin_id>{0});
  EXPECT_EQ(moved.nets[0]->built->rc.node_cap_ff[0], 24);
  Rc_cache fresh;
  EXPECT_EQ(moved.nets[0]->built->rc, fresh.update(layout, {geometry}, policy, star).nets[0]->built->rc);
  EXPECT_EQ(first.nets[0]->built->rc.node_cap_ff[0], 21);  // old snapshot survives
}

TEST(RcCache, ElectricalRevisionRetainsWireButDirtiesTiming) {
  Rc_cache   cache;
  auto       geometry    = net();
  const auto before      = cache.update(layout, {geometry}, policy, star);
  geometry.load_identity = 2;
  auto after             = cache.update(layout, {geometry}, policy, star);
  EXPECT_EQ(after.stats.rebuilt, 0);
  EXPECT_EQ(after.stats.reused, 1);
  EXPECT_EQ(after.stats.timing_dirty, 1);
  EXPECT_EQ(after.nets[0]->built, before.nets[0]->built);
  auto changed = policy;
  ++changed.timing_generation;
  after = cache.update(layout, {geometry}, changed, star);
  EXPECT_EQ(after.stats.rebuilt, 0);
  EXPECT_EQ(after.stats.timing_dirty, 1);
  EXPECT_EQ(after.nets[0]->built, before.nets[0]->built);
  ++changed.geometry_generation;
  EXPECT_EQ(cache.update(layout, {geometry}, changed, star).stats.rebuilt, 1);
}

TEST(RcCache, MembershipOrderingRemovalAndRollback) {
  Rc_cache cache;
  auto     a = net(3), b = net(6);
  auto     original  = cache.update(layout, {a, b}, policy, star);
  auto     reordered = cache.update(layout, {b, a}, policy, star);
  EXPECT_EQ(reordered.stats.reused, 2);
  EXPECT_EQ(reordered.nets[0], original.nets[1]);
  a.pins.erase(a.pins.begin() + 1);
  const auto edited = cache.update(layout, {a}, policy, star);
  EXPECT_EQ(edited.stats.rebuilt, 1);
  EXPECT_EQ(edited.stats.removed, 1);
  EXPECT_EQ(cache.size(), 1);
  const auto restored = cache.update(layout, {net(3), b}, policy, star);
  EXPECT_EQ(restored.stats.rebuilt, 2);
  EXPECT_EQ(restored.nets[0]->built->rc, original.nets[0]->built->rc);
  EXPECT_EQ(restored.nets[1]->built->rc, original.nets[1]->built->rc);
  EXPECT_EQ(cache.update(layout, {}, policy, star).stats.removed, 2);
  EXPECT_EQ(cache.size(), 0);
}

TEST(RcCache, FailedTrialPreservesPriorGeneration) {
  Rc_cache   cache;
  auto       a = net(3), b = net(6);
  const auto old          = cache.update(layout, {a, b}, policy, star);
  a.pins[1].second.second = 3;
  const auto fail         = [](const auto& g, const auto& p) {
    if (g.driver == 9) {
      throw std::runtime_error("trial construction failure");
    }
    return star(g, p);
  };
  EXPECT_THROW(cache.update(layout, {a, net(9)}, policy, fail), std::runtime_error);
  EXPECT_EQ(cache.size(), 2);
  const auto restored = cache.update(layout, {net(3), b}, policy, star);
  EXPECT_EQ(restored.stats.reused, 2);
  EXPECT_EQ(restored.nets[0], old.nets[0]);
  EXPECT_EQ(restored.nets[1], old.nets[1]);
}

TEST(RcCache, PolicyAndPinOffsetChangesInvalidateWireGeometry) {
  Rc_cache cache;
  auto     geometry = net();
  auto     changed  = policy;
  cache.update(layout, {geometry}, changed, star);
  for (auto& value : changed.per_um) {
    value *= 2;
    EXPECT_EQ(cache.update(layout, {geometry}, changed, star).stats.rebuilt, 1);
  }
  changed.dbu = 2;
  EXPECT_EQ(cache.update(layout, {geometry}, changed, star).stats.rebuilt, 1);
  changed.identity = "new routing setup";
  EXPECT_EQ(cache.update(layout, {geometry}, changed, star).stats.rebuilt, 1);
  geometry.pins[1].second.second += .25;  // orientation/pin offset, no cell-origin change
  EXPECT_EQ(cache.update(layout, {geometry}, changed, star).stats.rebuilt, 1);
}

TEST(RcCache, CoincidentPinsAndZeroWirePolicyStayFinite) {
  Rc_cache cache;
  auto     geometry = net();
  for (auto& pin : geometry.pins) {
    pin.second = {0, 0};
  }
  const auto output = cache.update(layout, {geometry}, Rc_policy{}, star);
  EXPECT_EQ(output.stats.rebuilt, 1);
  for (const auto cap : output.nets[0]->built->rc.node_cap_ff) {
    EXPECT_EQ(cap, 0);
  }
  for (const auto& edge : output.nets[0]->built->rc.edges) {
    EXPECT_EQ(edge.resistance_ohm, 0);
  }
  EXPECT_EQ(cache.update(layout, {geometry}, Rc_policy{}, star).stats.reused, 1);
}

TEST(RcCache, RejectsInvalidFramesAndMalformedBuilderWithoutReplacingCache) {
  Rc_cache   cache;
  const auto before = cache.update(layout, {net()}, policy, star);
  EXPECT_THROW(cache.update(layout, {net(), net()}, policy, star), std::invalid_argument);
  auto invalid          = net();
  invalid.pins[1].first = invalid.pins[0].first;
  EXPECT_THROW(cache.update(layout, {invalid}, policy, star), std::invalid_argument);
  invalid        = net();
  invalid.driver = 31;
  EXPECT_THROW(cache.update(layout, {invalid}, policy, star), std::invalid_argument);
  invalid                      = net();
  invalid.pins[1].second.first = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(cache.update(layout, {invalid}, policy, star), std::invalid_argument);
  auto bad_policy      = policy;
  bad_policy.per_um[3] = -1;
  EXPECT_THROW(cache.update(layout, {net()}, bad_policy, star), std::invalid_argument);
  const auto malformed = [](const auto& g, const auto& p) {
    auto built           = star(g, p);
    built.rc.edges[0].to = 99;
    return built;
  };
  EXPECT_THROW(cache.update(layout, {net(12)}, policy, malformed), std::invalid_argument);
  const auto restored = cache.update(layout, {net()}, policy, star);
  EXPECT_EQ(restored.stats.reused, 1);
  EXPECT_EQ(restored.nets[0], before.nets[0]);
}
TEST(RcCache, ReusedDenseIdsInAnotherLayoutCannotReuseOldTrees) {
  Rc_cache   cache;
  const auto before  = cache.update(layout, {net()}, policy, star);
  const auto another = std::make_shared<const Rc_layout>(Rc_layout{32, 456});
  const auto changed = cache.update(another, {net()}, policy, star);
  EXPECT_EQ(changed.stats.rebuilt, 1);
  EXPECT_NE(changed.nets[0]->built, before.nets[0]->built);
  EXPECT_EQ(before.nets[0]->layout, layout);  // old snapshots own their binding
  const auto repeated = cache.update(another, {net()}, policy, star);
  EXPECT_EQ(repeated.stats.reused, 1);
  // Even equal content fingerprints are not permission to alias a new generation.
  const auto recreated = std::make_shared<const Rc_layout>(*another);
  EXPECT_EQ(cache.update(recreated, {net()}, policy, star).stats.rebuilt, 1);
}

TEST(RcCache, InvalidPinIdCannotReplacePriorSnapshot) {
  Rc_cache   cache;
  const auto before         = cache.update(layout, {net()}, policy, star);
  auto       invalid        = net();
  invalid.pins.back().first = 1000;
  EXPECT_THROW(cache.update(layout, {invalid}, policy, star), std::invalid_argument);
  EXPECT_THROW(cache.update(nullptr, {net()}, policy, star), std::invalid_argument);
  const auto after = cache.update(layout, {net()}, policy, star);
  EXPECT_EQ(after.nets[0], before.nets[0]);
}

}  // namespace
}  // namespace livehd::placement
