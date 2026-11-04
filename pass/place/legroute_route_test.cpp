// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <set>

#include "gtest/gtest.h"
#include "inou/lefdef/routing_rules.hpp"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
struct Track_fixture {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  Design               design{tech, fp, options};
  Grid                 grid;
  Routing              routes;

  explicit Track_fixture(bool same_net) {
    options.reserved_tracks = 0;
    grid.x0 = grid.y0 = 100;
    grid.cols         = 1;
    grid.rows         = 3;
    grid.tile_w = grid.tile_h = 100;
    grid.die = fp.die = {0, 0, 1000, 1000};
    grid.layers       = {
        {"M3", "", false, 100, 50, 10},
        {"M4", "",  true, 100, 50, 10}
    };
    grid.obstacle_band.resize(2);
    grid.obstacle_band[0].resize(1);
    grid.obstacle_band[1].resize(3);
    grid.obstacles.resize(2);
    for (const auto& layer : grid.layers) {
      auto& l     = tech.layers[layer.name];
      l.direction = layer.horizontal ? "HORIZONTAL" : "VERTICAL";
      l.width     = .01;
      l.pitch_x = l.pitch_y = .1;
      l.rules.loaded        = true;
      l.rules.spacing       = {
          {.01, {}, {}}
      };
      tech.routing_layers.push_back(layer.name);
    }
    routes.trees.resize(same_net ? 1 : 2);
    design.nets.resize(routes.trees.size());
    for (auto& tree : routes.trees) {
      tree.nodes.resize(3);
      for (int i = 0; i < 3; ++i) {
        tree.nodes[i].tile = i;
      }
    }
    for (int i = 0; i < 2; ++i) {
      Segment s;
      s.net     = same_net ? 0 : i;
      s.layer   = 0;
      s.band    = 0;
      s.lo      = i;
      s.hi      = i + 1;
      s.node_lo = i;
      s.node_hi = i + 1;
      routes.segments.push_back(s);
    }
  }
};
}  // namespace

TEST(LegrouteRoute, AdjacentBranchesOfTheSameNetShareTheirTrack) {
  Track_fixture f(true);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(f.routes.unassigned, 0u);
  ASSERT_GE(f.routes.segments[0].track, 0);
  EXPECT_EQ(f.routes.segments[0].track, f.routes.segments[1].track);
}

TEST(LegrouteRoute, DensityReservationMatchesPhysicalTrackSelectionAndLayerMask) {
  for (bool reserve_vertical : {false, true}) {
    Track_fixture f(false);
    f.options.reserved_tracks_per_um = 1;
    f.options.reserved_layers        = reserve_vertical ? "" : "M4";
    f.grid.x0                        = 900;
    f.grid.tile_w                    = 200;  // the two tracks950 and1050 straddle the absolute1um boundary
    f.grid.die = f.fp.die = {0, 0, 2000, 1000};
    const Track_reservations policy(f.options, f.tech, f.grid.dbu);
    const auto [a, b] = f.grid.tracks(0, 0);
    ASSERT_EQ(b - a + 1, 2);
    int available = 0;
    for (auto k = a; k <= b; ++k) {
      const auto x  = f.grid.track_coord(0, k);
      available    += !policy.selected("M3", x, x - f.grid.layers[0].pitch, k - a);
    }
    EXPECT_EQ(available, reserve_vertical ? 1 : 2);
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned, reserve_vertical ? 1u : 0u);
    std::set<int64_t> assigned;
    for (const auto& segment : f.routes.segments) {
      // An unassigned segment may retain a least-overlap candidate. Even that
      // candidate must never use a hard-reserved physical track.
      if (segment.track < 0) {
        continue;
      }
      const auto x = f.grid.track_coord(segment.layer, segment.track);
      EXPECT_FALSE(policy.selected("M3", x, x - f.grid.layers[0].pitch, segment.track - a));
      assigned.insert(x);
    }
    EXPECT_EQ(assigned.size(), size_t(available));
  }
}

TEST(LegrouteRoute, DifferentNetsCannotShareTheOverlappingTrack) {
  Track_fixture f(false);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_GT(f.routes.unassigned, 0u);
}

TEST(LegrouteRoute, SharingDoesNotBypassAHardObstacle) {
  Track_fixture f(true);
  f.grid.obstacles[0].push_back({140, 250, 160, 350});
  f.grid.obstacle_band[0][0].push_back(0);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_GT(f.routes.unassigned, 0u);
  for (const auto& segment : f.routes.segments) {
    EXPECT_EQ(segment.track, -1);
  }
}

TEST(LegrouteRoute, AShorterSharedSpanCannotEraseALongerReservation) {
  Track_fixture f(true);
  f.routes.segments[0].hi      = 2;
  f.routes.segments[0].node_hi = 2;
  f.routes.segments[1].lo      = 0;
  f.routes.segments[1].hi      = 1;
  f.routes.segments[1].node_lo = 0;
  f.routes.segments[1].node_hi = 1;
  f.routes.trees.push_back(f.routes.trees[0]);
  f.design.nets.resize(2);
  auto other    = f.routes.segments[1];
  other.net     = 1;
  other.lo      = 1;
  other.hi      = 2;
  other.node_lo = 1;
  other.node_hi = 2;
  f.routes.segments.push_back(other);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(f.routes.unassigned, 1u);
}

TEST(LegrouteRoute, FixedVerticalArrivalsDefineTheirNodeRisers) {
  Track_fixture f(true);
  for (auto& segment : f.routes.segments) {
    segment.track = 1;
  }
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(f.routes.unassigned, 0u);
  for (const auto& node : f.routes.trees[0].nodes) {
    EXPECT_EQ(node.riser_layer, 0);
    EXPECT_EQ(node.riser_track, 1);
  }
}
TEST(LegrouteRoute, WideWireQueryRadiusDoesNotRejectLegalThinParallelWires) {
  Track_fixture f(false);
  f.grid.tile_w = 200;  // Two tracks, 100 DBU apart, for ten-DBU wires.
  f.tech.layers["M3"].rules.parallel = {{{0, .1}, {0}, {{.01}, {.25}}}};
  const physical::Signal_metal_rules rules(f.tech, f.fp);
  ASSERT_EQ(rules.clearance("M3"), 250);
  ASSERT_TRUE(rules.spacing({"M3", {145, 90, 155, 310}}, {"M3", {245, 190, 255, 410}}).legal);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(f.routes.unassigned, 0u);
  EXPECT_NE(f.routes.segments[0].track, f.routes.segments[1].track);
}

TEST(LegrouteRoute, EqualParallelEndsStillRespectEndOfLineKeepouts) {
  Track_fixture f(false);
  f.grid.tile_w = 200;
  f.routes.segments[1].lo = f.routes.segments[0].lo;
  f.routes.segments[1].hi = f.routes.segments[0].hi;
  f.tech.layers["M3"].rules.properties["LEF58_EOLKEEPOUT"] = {
      "EOLKEEPOUT .020 EXTENSION .020 .100 .020 CORNERONLY ;"};
  const physical::Signal_metal_rules rules(f.tech, f.fp);
  ASSERT_FALSE(rules.spacing({"M3", {145, 90, 155, 310}}, {"M3", {245, 90, 255, 310}}).legal);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_GT(f.routes.unassigned, 0u);
}
TEST(LegrouteRoute, FallbackShadowSeparatesFallbackBlockersAndKeepsAssignment) {
  // One M3 track over five rows. Clock X (net 0, row 0) is checked; clock Y (net 1, rows 0..3) overlaps it and lands
  // on the track as FALLBACK; signal Z (net 2, row 4) is then blocked only by Y's fallback metal.
  const auto build = [](Track_fixture& f, bool shadow) {
    f.options.l5_fallback_shadow = shadow;
    f.grid.rows                  = 5;
    f.grid.obstacle_band[1].resize(5);
    f.routes.trees.resize(3);
    f.design.nets.resize(3);
    for (auto& tree : f.routes.trees) {
      tree.nodes.resize(5);
      for (int i = 0; i < 5; ++i) {
        tree.nodes[i].tile = i;
      }
    }
    f.routes.segments.clear();
    const auto add = [&](int net, int lo, int hi, bool clock) {
      Segment s;
      s.net = net, s.layer = 0, s.band = 0, s.lo = lo, s.hi = hi, s.node_lo = lo, s.node_hi = hi, s.clock = clock;
      f.routes.segments.push_back(s);
    };
    add(0, 0, 0, true);
    add(1, 0, 3, true);
    add(2, 4, 4, false);
  };
  Track_fixture off(false), on(false);
  build(off, false);
  build(on, true);
  assign_tracks(off.design, off.grid, off.routes, 1);
  assign_tracks(on.design, on.grid, on.routes, 1);
  ASSERT_EQ(on.routes.segments.size(), off.routes.segments.size());
  for (size_t i = 0; i < on.routes.segments.size(); ++i) {  // the diagnostic never changes the assignment
    EXPECT_EQ(on.routes.segments[i].track, off.routes.segments[i].track);
    EXPECT_EQ(on.routes.segments[i].track_state, off.routes.segments[i].track_state);
  }
  EXPECT_EQ(on.routes.unassigned, off.routes.unassigned);
  EXPECT_TRUE(off.routes.track_fallback_shadow.empty());
  EXPECT_EQ(on.routes.segments[0].track_state, uint8_t(Track_state::checked));
  EXPECT_EQ(on.routes.segments[1].track_state, uint8_t(Track_state::fallback));
  auto& c = on.routes.track_fallback_shadow;
  EXPECT_EQ(c["ledger:mismatched"], 0u);
  EXPECT_GT(c["ledger:bands"], 0u);
  EXPECT_EQ(c["M3:fixed:failed"], 1u);  // Y is blocked by checked X: the shadow keeps X
  EXPECT_EQ(c["M3:fixed:shadow_free"], 0u);
  EXPECT_EQ(c["M3:segment_first:failed"], 1u);  // Z is blocked only by Y's fallback
  EXPECT_EQ(c["M3:segment_first:shadow_free"], 1u);
  EXPECT_EQ(c["M3:segment_final:failed"], 1u);
  EXPECT_EQ(c["M3:segment_final:shadow_free"], 1u);
}
TEST(LegrouteRoute, FallbackShadowKeepsTheCheckedSpanOfASameNetFallback) {
  // One M3 track over four rows. Net 0 has checked clock X (row 0) and later FALLBACK clock Y (rows 0..3, blocked by
  // net 1's checked W on row 3); the real track merges X and Y into one net-0 owner span. The shadow must still
  // hold X, so signal Z (net 2, row 0) stays blocked there; dropping the merged owner would wrongly free it.
  Track_fixture f(false);
  f.options.l5_fallback_shadow = true;
  f.grid.rows                  = 4;
  f.grid.obstacle_band[1].resize(4);
  f.routes.trees.resize(3);
  f.design.nets.resize(3);
  for (auto& tree : f.routes.trees) {
    tree.nodes.resize(4);
    for (int i = 0; i < 4; ++i) {
      tree.nodes[i].tile = i;
    }
  }
  f.routes.segments.clear();
  const auto add = [&](int net, int lo, int hi, bool clock) {
    Segment s;
    s.net = net, s.layer = 0, s.band = 0, s.lo = lo, s.hi = hi, s.node_lo = lo, s.node_hi = hi, s.clock = clock;
    f.routes.segments.push_back(s);
  };
  add(0, 0, 0, true);   // X
  add(1, 3, 3, true);   // W
  add(0, 0, 3, true);   // Y
  add(2, 0, 0, false);  // Z
  assign_tracks(f.design, f.grid, f.routes, 1);
  EXPECT_EQ(f.routes.segments[0].track_state, uint8_t(Track_state::checked));
  EXPECT_EQ(f.routes.segments[1].track_state, uint8_t(Track_state::checked));
  EXPECT_EQ(f.routes.segments[2].track_state, uint8_t(Track_state::fallback));
  auto& c = f.routes.track_fallback_shadow;
  EXPECT_EQ(c["ledger:mismatched"], 0u);
  EXPECT_EQ(c["M3:fixed:failed"], 1u);
  EXPECT_EQ(c["M3:fixed:shadow_free"], 0u);
  EXPECT_EQ(c["M3:segment_first:failed"], 1u);
  EXPECT_EQ(c["M3:segment_first:shadow_free"], 0u);  // X's checked span survives Y's fallback merge
  // the sampled blocker of Z names net 0's span and both of its contributions' provenance, checked first
  ASSERT_EQ(f.routes.track_failure_samples.size(), 1u);
  const auto& why = f.routes.track_failure_samples[0];
  EXPECT_NE(why.find("M3 segment_final net 2"), std::string::npos) << why;
  EXPECT_NE(why.find("span 0 [100,200] checked"), std::string::npos) << why;
}
TEST(LegrouteRoute, RuleReachLimitsTheEndEnvelopeToOffsetsTheExactRulesCanViolate) {
  // A keepout with a long forward reach (query radius 260 DBU) but a 20-DBU side extension: the envelope scans the
  // adjacent track (90-DBU gap), yet no nominal pair there is illegal. With l5_rule_reach the measured reach is 0 and
  // two equal-span nets share the two tracks; by default the second one stays unassigned.
  for (bool reach : {false, true}) {
    Track_fixture f(false);
    f.options.l5_rule_reach = reach;
    f.grid.tile_w           = 200;
    f.routes.segments[1].lo = f.routes.segments[0].lo;
    f.routes.segments[1].hi = f.routes.segments[0].hi;
    f.tech.layers["M3"].rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .020 EXTENSION .020 .020 .200 CORNERONLY ;"};
    const physical::Signal_metal_rules rules(f.tech, f.fp);
    ASSERT_EQ(rules.clearance("M3"), 260);
    assign_tracks(f.design, f.grid, f.routes, 1);
    if (reach) {
      EXPECT_EQ(f.routes.track_fallback_shadow["rule_reach:M3"], 0u);
      EXPECT_EQ(f.routes.unassigned, 0u);
      EXPECT_NE(f.routes.segments[0].track, f.routes.segments[1].track);
    } else {
      EXPECT_GT(f.routes.unassigned, 0u);
    }
  }
}
TEST(LegrouteRoute, RuleReachKeepsALongParallelRunSpacingThatShortSamplesMiss) {
  // Advisor counterexample: a PRL row requiring 120 DBU once two wires run 2000 DBU side by side. The sampled
  // lengths never overlap that far, but the analytic lateral reach (120) keeps the adjacent track (gap 90): two
  // equal 2000+ DBU spans cannot share the two tracks.
  Track_fixture f(false);
  f.options.l5_rule_reach = true;
  f.grid.tile_w           = 200;
  f.grid.tile_h           = 3000;
  f.routes.segments[1].lo = f.routes.segments[0].lo;
  f.routes.segments[1].hi = f.routes.segments[0].hi;
  f.tech.layers["M3"].rules.parallel = {{{0}, {0, 2}, {{.01, .12}}}};
  f.tech.layers["M3"].rules.properties["LEF58_RECTONLY"] = {"RECTONLY ;"};
  const physical::Signal_metal_rules rules(f.tech, f.fp);
  ASSERT_TRUE(rules.clearance("M3"));
  EXPECT_EQ(rules.lateral_reach("M3"), 120);
  ASSERT_FALSE(rules.spacing({"M3", {145, 0, 155, 2100}}, {"M3", {245, 0, 255, 2100}}).legal);
  assign_tracks(f.design, f.grid, f.routes, 1);
  EXPECT_EQ(f.routes.track_fallback_shadow["rule_reach:M3"], 1u);
  EXPECT_GT(f.routes.unassigned, 0u);
}
}  // namespace livehd::placement::lr
