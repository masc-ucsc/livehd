// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "macro_anchor.hpp"

#include <cmath>
#include <stdexcept>

#include "gtest/gtest.h"

namespace pl = livehd::placement;
namespace dp = DreamPlace::native;

namespace {
// Nodes: movable macros first, then movable cells, then fixed zero-size pins.
// Each net lists node indexes; every pin sits at its node origin + (0.5, 0.5).
dp::Design make(const std::vector<std::array<double, 4>>& nodes, int movable, const std::vector<int>& macros,
                const std::vector<std::vector<int>>& nets, double size = 100) {
  dp::Design d;
  d.layout      = {0, 0, size, size, 1, 1};
  d.num_movable = movable;
  for (const auto& n : nodes) {
    d.node_x.push_back(n[0]);
    d.node_y.push_back(n[1]);
    d.node_size_x.push_back(n[2]);
    d.node_size_y.push_back(n[3]);
  }
  d.node_macro.assign(nodes.size(), 0);
  for (int m : macros) {
    d.node_macro[m] = 1;
  }
  for (const auto& net : nets) {
    for (int v : net) {
      d.pin2node.push_back(v);
      d.pin_offset_x.push_back(.5);
      d.pin_offset_y.push_back(.5);
      d.flat_net2pin.push_back(int(d.flat_net2pin.size()));
    }
    d.flat_net2pin_start.push_back(int(d.flat_net2pin.size()));
  }
  d.build_maps();
  return d;
}
}  // namespace

TEST(MacroAnchor, WireGradientIsBboxAware) {
  // Macro 0 at x=40; net A ties it to a pin at the right (macro is the left extreme); net B has pins on both sides
  // (macro strictly inside the bbox: no pull); net C is a 3-pin net where the macro is the top extreme.
  auto d = make(
      {
          {40, 40, 10, 10},
          {80, 40,  0,  0},
          {10, 45,  0,  0},
          {90, 45,  0,  0},
          {45, 10,  0,  0},
          {44, 12,  0,  0}
  },
      1,
      {0},
      {{0, 1}, {2, 0, 3}, {0, 4, 5}});
  const auto g = pl::macro_wire_gradients(d, {0});
  ASSERT_EQ(g.size(), 1u);
  // x: left extreme of net A (-1) and of net C (-1/2); strictly inside net B.
  EXPECT_DOUBLE_EQ(g[0].x, -1.5);
  // y: tie with net A's pin (no strict extreme), bottom of net B (-1/2), top of net C (+1/2).
  EXPECT_DOUBLE_EQ(g[0].y, 0.0);
}

TEST(MacroAnchor, GatedWeightOnlyReducesUphillPulls) {
  const pl::Macro_wire_gradient g{1, 0};
  EXPECT_DOUBLE_EQ(pl::gated_anchor_weight(.5, 1, g, -10, 0, 1), .5);      // toward the target shortens the nets
  EXPECT_DOUBLE_EQ(pl::gated_anchor_weight(.5, 1, g, 10, 0, 1), .25);      // uphill by one reference slope
  EXPECT_DOUBLE_EQ(pl::gated_anchor_weight(.5, 0, g, 10, 0, 1), .5);       // gating disabled
  EXPECT_DOUBLE_EQ(pl::gated_anchor_weight(.5, 1, {0, 0}, 10, 0, 1), .5);  // inside every bbox: free to move
}

TEST(MacroAnchor, SwapGainIsExactIncidentHpwl) {
  // Two 10x10 macros; macro 0 is tied to a pin near macro 1 and vice versa.
  auto d = make(
      {
          {10, 40, 10, 10},
          {70, 40, 10, 10},
          {85, 45,  0,  0},
          { 5, 45,  0,  0}
  },
      2,
      {0, 1},
      {{0, 2}, {1, 3}});
  const std::pair<double, double> c0{15, 45}, c1{75, 45};
  // Before: |85-10.5| + |5-70.5| = 74.5 + 65.5; after: |85-70.5| + |5-10.5| = 14.5 + 5.5.
  EXPECT_DOUBLE_EQ(pl::macro_swap_gain(d, 0, 1, c0, c1), (14.5 + 5.5) - (74.5 + 65.5));
}

TEST(MacroAnchor, ScheduleRefreshesAndStabilizes) {
  auto d = make(
      {
          {10, 10, 10, 10},
          {60, 60, 10, 10},
          {50, 50,  1,  1}
  },
      3,
      {0, 1},
      {{0, 2}, {1, 2}});
  pl::Macro_anchor_options o;
  o.enabled              = true;
  o.start_overflow       = 1;  // any overflow starts
  o.milestones           = {};
  o.refresh_interval     = 10;
  o.ramp                 = 2;
  o.gate                 = 0;
  o.settle               = 5;
  o.final_weight         = 0;      // plain schedule: stop refreshing once the targets are stable
  o.select               = "all";  // round-4 schedule
  o.target_hysteresis    = 0;      // round-4 targets (the default 0.5 keeps moving them toward the previous ones)
  o.swaps                = false;
  o.final_targets        = false;
  o.spring_length        = 0;
  int              calls = 0;
  // Fake legalization: macro 0 moves +3 in x, macro 1 stays.
  pl::Macro_anchor anchor(
      o,
      {0, 1},
      100,
      [&](dp::Design& p, bool) {
        ++calls;
        p.node_x[0] += 3;
      },
      "");
  EXPECT_FALSE(anchor.update(d, 1).has_value());  // no GP overflow sample yet
  anchor.observe_overflow(.5);
  auto first = anchor.update(d, 1);
  ASSERT_TRUE(first.has_value());
  ASSERT_EQ(first->size(), 2u);
  EXPECT_DOUBLE_EQ((*first)[0].x, 18);
  EXPECT_DOUBLE_EQ((*first)[0].y, 15);
  EXPECT_DOUBLE_EQ((*first)[0].weight, .25);      // ramp 1/2
  EXPECT_FALSE(anchor.update(d, 2).has_value());  // held between refreshes
  EXPECT_FALSE(anchor.update(d, 10).has_value());
  auto second = anchor.update(d, 11);
  ASSERT_TRUE(second.has_value());
  EXPECT_DOUBLE_EQ((*second)[0].weight, .5);
  // Targets did not change in the second refresh: periodic refreshes stop.
  EXPECT_FALSE(anchor.update(d, 21).has_value());
  EXPECT_FALSE(anchor.update(d, 40).has_value());
  EXPECT_EQ(calls, 2);
  // No refresh within `settle` of the iteration budget.
  pl::Macro_anchor late(o, {0, 1}, 100, [](dp::Design&, bool) {}, "");
  late.observe_overflow(0);
  EXPECT_FALSE(late.update(d, 96).has_value());
}

TEST(MacroAnchor, WeightZeroMeasuresWithoutPulling) {
  auto d = make(
      {
          {10, 10, 10, 10},
          {50, 50,  1,  1}
  },
      2,
      {0},
      {{0, 1}});
  pl::Macro_anchor_options o;
  o.enabled        = true;
  o.select         = "all";
  o.weight         = 0;
  o.start_overflow = 1;
  pl::Macro_anchor anchor(o, {0}, 100, [](dp::Design& p, bool) { p.node_x[0] += 2; }, "");
  anchor.observe_overflow(0);
  EXPECT_FALSE(anchor.update(d, 1).has_value());
  ASSERT_EQ(anchor.refreshes().size(), 1u);
  EXPECT_DOUBLE_EQ(anchor.refreshes()[0].mean_distance, 2);
}

TEST(MacroAnchor, SwapsCrossingTargets) {
  auto d = make(
      {
          {10, 40, 10, 10},
          {70, 40, 10, 10},
          {85, 45,  0,  0},
          { 5, 45,  0,  0}
  },
      2,
      {0, 1},
      {{0, 2}, {1, 3}});
  pl::Macro_anchor_options o;
  o.enabled        = true;
  o.start_overflow = 1;
  o.milestones     = {};
  o.swaps          = true;
  o.ramp           = 1;
  o.gate           = 0;
  pl::Macro_anchor anchor(o, {0, 1}, 200, [](dp::Design&, bool) {}, "");
  anchor.observe_overflow(0);
  auto pulls = anchor.update(d, 1);
  ASSERT_TRUE(pulls.has_value());
  EXPECT_EQ(anchor.swaps(), 1);
  EXPECT_DOUBLE_EQ((*pulls)[0].x, 75);  // macro 0 now anchored at macro 1's legal target
  EXPECT_DOUBLE_EQ((*pulls)[1].x, 15);
  // A locked pair keeps its exchanged targets on the next refresh even though GP has not moved it yet.
  auto held = anchor.update(d, 31);
  ASSERT_TRUE(held.has_value());
  EXPECT_DOUBLE_EQ((*held)[0].x, 75);
  EXPECT_EQ(anchor.swaps(), 1);
}

TEST(MacroAnchor, SwapJumpExchangesPositionsOnce) {
  auto d = make(
      {
          {10, 40, 10, 10},
          {70, 40, 10, 10},
          {85, 45,  0,  0},
          { 5, 45,  0,  0}
  },
      2,
      {0, 1},
      {{0, 2}, {1, 3}});
  pl::Macro_anchor_options o;
  o.enabled        = true;
  o.start_overflow = 1;
  o.milestones     = {};
  o.swaps          = true;
  o.swap_jump      = true;
  o.swap_lock      = 0;
  pl::Macro_anchor anchor(o, {0, 1}, 200, [](dp::Design&, bool) {}, "");
  anchor.observe_overflow(0);
  auto pulls = anchor.update(d, 1);
  ASSERT_TRUE(pulls.has_value());
  EXPECT_TRUE((*pulls)[0].jump);
  EXPECT_TRUE((*pulls)[1].jump);
  // Unchanged positions would repeat the same gain: the pair is never exchanged twice.
  auto again = anchor.update(d, 31);
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(anchor.swaps(), 1);
  EXPECT_FALSE((*again)[0].jump);
}

TEST(MacroAnchor, SwapRequiresCrossingGradientsAndSize) {
  // Different sizes: no swap even with a wirelength gain.
  auto d = make(
      {
          {10, 40, 10, 10},
          {70, 40, 20, 10},
          {85, 45,  0,  0},
          { 5, 45,  0,  0}
  },
      2,
      {0, 1},
      {{0, 2}, {1, 3}});
  pl::Macro_anchor_options o;
  o.enabled        = true;
  o.start_overflow = 1;
  o.swaps          = true;
  pl::Macro_anchor anchor(o, {0, 1}, 200, [](dp::Design&, bool) {}, "");
  anchor.observe_overflow(0);
  ASSERT_TRUE(anchor.update(d, 1).has_value());
  EXPECT_EQ(anchor.swaps(), 0);
  // Equal sizes but gradients pointing away from each other: no swap.
  auto e = make(
      {
          {10, 40, 10, 10},
          {70, 40, 10, 10},
          { 2, 45,  0,  0},
          {95, 45,  0,  0}
  },
      2,
      {0, 1},
      {{0, 2}, {1, 3}});
  pl::Macro_anchor other(o, {0, 1}, 200, [](dp::Design&, bool) {}, "");
  other.observe_overflow(0);
  ASSERT_TRUE(other.update(e, 1).has_value());
  EXPECT_EQ(other.swaps(), 0);
}

TEST(MacroAnchor, NativeTargetsAreLegalAndHonorHalo) {
  // Two overlapping 20x20 macros with a 2-unit halo; macro LP separates them by at least two halos.
  auto d = make(
      {
          {30, 30, 20, 20},
          {35, 32, 20, 20},
          { 5,  5,  1,  1}
  },
      3,
      {0, 1},
      {{0, 2}, {1, 2}});
  d.node_halo_x = {2, 2, 0};
  d.node_halo_y = {2, 2, 0};
  pl::Native_target_config config;
  config.bins_x = config.bins_y = 8;
  int after                     = 0;
  config.after_macro_legalize   = [&](dp::Design& physical, bool) {
    ++after;
    // The host chain sees physical (unpadded) sizes.
    EXPECT_DOUBLE_EQ(physical.node_size_x[0], 20);
  };
  auto       copy = d;
  const auto gen  = pl::native_target_generator(config);
  gen(copy, false);
  EXPECT_EQ(after, 1);
  EXPECT_DOUBLE_EQ(copy.node_size_x[0], 20);
  const double gx = std::max(copy.node_x[0], copy.node_x[1]) - std::min(copy.node_x[0], copy.node_x[1]) - 20;
  const double gy = std::max(copy.node_y[0], copy.node_y[1]) - std::min(copy.node_y[0], copy.node_y[1]) - 20;
  EXPECT_GE(std::max(gx, gy), 4 - 1e-9);
  for (int i : {0, 1}) {
    EXPECT_GE(copy.node_x[i], 2 - 1e-9);
    EXPECT_LE(copy.node_x[i] + 20, 98 + 1e-9);
  }
  // The original snapshot is untouched.
  EXPECT_DOUBLE_EQ(d.node_x[1], 35);
}

TEST(MacroAnchor, InflationRampsToZeroAtFirstRefresh) {
  auto d = make(
      {
          {10, 10, 10, 10},
          {50, 50,  1,  1}
  },
      2,
      {0},
      {{0, 1}});
  pl::Macro_anchor_options o;
  o.enabled        = true;
  o.inflate        = .04;
  o.start_overflow = .3;
  pl::Macro_anchor anchor(o, {0}, 500, [](dp::Design&, bool) {}, "");
  EXPECT_DOUBLE_EQ(anchor.density_margin(), .04);  // before any GP sample
  anchor.observe_overflow(.7);
  EXPECT_DOUBLE_EQ(anchor.density_margin(), .04);
  anchor.observe_overflow(.5);
  EXPECT_NEAR(anchor.density_margin(), .02, 1e-12);
  anchor.observe_overflow(.3);
  EXPECT_DOUBLE_EQ(anchor.density_margin(), 0);
  ASSERT_TRUE(anchor.update(d, 100).has_value());
  anchor.observe_overflow(.6);  // after the first refresh the margin stays off
  EXPECT_DOUBLE_EQ(anchor.density_margin(), 0);
}

TEST(MacroAnchor, LateRefreshesRunExpensivePasses) {
  auto d = make(
      {
          {10, 10, 10, 10},
          {50, 50,  1,  1}
  },
      2,
      {0},
      {{0, 1}});
  pl::Macro_anchor_options o;
  o.enabled              = true;
  o.start_overflow       = 1;
  o.milestones           = {};
  o.refresh_interval     = 10;
  o.settle               = 5;
  o.dead_space_refreshes = 2;
  std::vector<std::pair<int, bool>> calls;
  int                               now = 0;
  pl::Macro_anchor                  anchor(
      o,
      {0},
      100,
      [&](dp::Design& p, bool late) {
        calls.push_back({now, late});
        p.node_x[0] += now % 7;  // keep the targets moving so periodic refreshes continue
      },
      "");
  anchor.observe_overflow(0);
  for (now = 1; now < 100; ++now) {
    anchor.update(d, now);
  }
  ASSERT_GE(calls.size(), 3u);
  // Late = within the last two refresh windows before the cap minus settle (iterations >= 100 - 5 - 20).
  for (const auto& [iteration, late] : calls) {
    EXPECT_EQ(late, iteration >= 75) << iteration;
  }
  EXPECT_TRUE(calls.back().second);
}

TEST(MacroAnchor, RejectsInvalidPolicy) {
  pl::Macro_anchor_options o;
  o.weight = 9;
  EXPECT_THROW(pl::validate_macro_anchor_options(o), std::invalid_argument);
  o.weight  = 2;  // above one is allowed: the native pull cap follows the weight
  o.inflate = .5;
  EXPECT_THROW(pl::validate_macro_anchor_options(o), std::invalid_argument);
  o.inflate    = 0;
  o.weight     = .5;
  o.milestones = {.1, .2};
  EXPECT_THROW(pl::validate_macro_anchor_options(o), std::invalid_argument);
}

TEST(MacroAnchor, EdgeClassificationFollowsChains) {
  // Core 0..100, channel 2. Macro 0 touches the left edge, macro 1 sits 2 to its right (chained), macro 2 is 10 to
  // the right of macro 1 (free), macro 3 is near the top edge within channel+tolerance.
  auto d = make(
      {
          { 0, 40, 10, 10},
          {12, 40, 10, 10},
          {32, 40, 10, 10},
          {50, 87, 10, 10},
          {50, 50,  1,  1}
  },
      5,
      {0, 1, 2, 3},
      {{0, 4}});
  std::vector<std::pair<double, double>> t;
  for (int i = 0; i < 4; ++i) {
    t.push_back({d.node_x[i] + 5, d.node_y[i] + 5});
  }
  const auto edge = pl::edge_anchored_macros(d, {0, 1, 2, 3}, t, 2, 2);
  EXPECT_TRUE(edge[0]);
  EXPECT_TRUE(edge[1]);
  EXPECT_FALSE(edge[2]);
  EXPECT_TRUE(edge[3]);  // 3 from the top: channel 2 + one row
}

TEST(MacroAnchor, EdgeRandomSelects) {
  // Macro 0 at the left edge (edge anchor); macros 1..4 interior; random_fraction 0.5 anchors two of them.
  auto d = make(
      {
          { 0, 40, 10, 10},
          {30, 30, 10, 10},
          {50, 30, 10, 10},
          {30, 60, 20, 20},
          {60, 60, 10, 10},
          {50, 50,  1,  1}
  },
      6,
      {0, 1, 2, 3, 4},
      {{0, 5}});
  pl::Macro_anchor_options o;
  o.enabled         = true;
  o.start_overflow  = 1;
  o.milestones      = {};
  o.ramp            = 1;
  o.gate            = 0;
  o.select          = "edge+random";
  o.edge_weight     = 4;
  o.random_weight   = 1;
  o.random_fraction = .5;
  o.spring_length   = .1;
  o.channel_x = o.channel_y = 2;
  pl::Macro_anchor anchor(o, {0, 1, 2, 3, 4}, 1000, [](dp::Design& p, bool) { p.node_x[1] += 1; }, "");
  anchor.observe_overflow(.5);
  auto pulls = anchor.update(d, 1);
  ASSERT_TRUE(pulls.has_value());
  ASSERT_EQ(pulls->size(), 5u);
  EXPECT_DOUBLE_EQ((*pulls)[0].weight, 4);
  int random = 0;
  for (int k = 1; k < 5; ++k) {
    const double w = (*pulls)[k].weight;
    EXPECT_TRUE(w == 0 || w == 1);
    random += w > 0;
    EXPECT_DOUBLE_EQ((*pulls)[k].length, .1 * 12);  // mean max side (4*10 + 20) / 5
  }
  EXPECT_EQ(random, 2);
  EXPECT_EQ(anchor.refreshes()[0].edge, 1);
  EXPECT_EQ(anchor.refreshes()[0].random, 2);
  // Selective mode keeps refreshing (fresh sample) even when targets are stable.
  EXPECT_TRUE(anchor.update(d, 31).has_value());
}

TEST(MacroAnchor, EdgeChainsRunAwayFromTheirEdge) {
  // Core 0..100, channel 2. Macro 0 on the left edge; macro 1 chained to its right; macro 2 directly above macro 1
  // (adjacent, but a vertical neighbor of a left-chained macro is not chained to the left edge, and it is far from
  // the bottom/top edges); macro 3 right of macro 1 but in the right half (left chains stay in the left half).
  auto d = make(
      {
          { 0, 40, 10, 10},
          {12, 40, 10, 10},
          {12, 52, 10, 10},
          {48, 40, 10, 10},
          {50, 50,  1,  1}
  },
      5,
      {0, 1, 2, 3},
      {{0, 4}});
  std::vector<std::pair<double, double>> t;
  for (int i = 0; i < 4; ++i) {
    t.push_back({d.node_x[i] + 5, d.node_y[i] + 5});
  }
  auto e = pl::edge_anchored_macros(d, {0, 1, 2, 3}, t, 2, 2);
  EXPECT_TRUE(e[0]);
  EXPECT_TRUE(e[1]);
  EXPECT_FALSE(e[2]);
  EXPECT_FALSE(e[3]);
  t[3].first = 24 + 5 + 10;  // x 34..44: gap 12 from macro 1: free
  e          = pl::edge_anchored_macros(d, {0, 1, 2, 3}, t, 2, 2);
  EXPECT_FALSE(e[3]);
  t[3].first = 24 + 5;  // x 24..34: gap 2 from macro 1, center 29 in the left half: chained
  e          = pl::edge_anchored_macros(d, {0, 1, 2, 3}, t, 2, 2);
  EXPECT_TRUE(e[3]);
}

TEST(MacroAnchor, EdgeWithoutChains) {
  auto d = make(
      {
          { 0, 40, 10, 10},
          {12, 40, 10, 10},
          {50, 50,  1,  1}
  },
      3,
      {0, 1},
      {{0, 2}});
  const std::vector<std::pair<double, double>> t{
      { 5, 45},
      {17, 45}
  };
  const auto e = pl::edge_anchored_macros(d, {0, 1}, t, 2, 2, false);
  EXPECT_TRUE(e[0]);
  EXPECT_FALSE(e[1]);
}

TEST(MacroAnchor, FinalTargetsSeedTheHardPass) {
  auto d = make(
      {
          {10, 10, 10, 10},
          {50, 50,  1,  1}
  },
      2,
      {0},
      {{0, 1}});
  pl::Macro_anchor_options o;
  o.enabled        = true;
  o.start_overflow = 1;
  o.final_targets  = true;
  pl::Macro_anchor anchor(o, {0}, 100, [](dp::Design& p, bool) { p.node_x[0] += 4; }, "");
  anchor.observe_overflow(0);
  ASSERT_TRUE(anchor.update(d, 1).has_value());
  auto final      = d;
  final.node_x[0] = 12;  // GP drifted 2 short of the target (14)
  anchor.before_legalize(final);
  EXPECT_DOUBLE_EQ(final.node_x[0], 14);
  EXPECT_DOUBLE_EQ(final.node_y[0], 10);
  // Inside the hard pass the native legalizer moved it; the override restores the target once.
  final.node_x[0] = 30;
  EXPECT_TRUE(anchor.final_override(final));
  EXPECT_DOUBLE_EQ(final.node_x[0], 14);
  final.node_x[0] = 30;
  EXPECT_FALSE(anchor.final_override(final));
  EXPECT_DOUBLE_EQ(final.node_x[0], 30);
}
