// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "power_grid_rules.hpp"

#include <algorithm>

#include "gtest/gtest.h"
#include "power_grid.hpp"

namespace livehd::placement {
namespace {
namespace ph = physical;
// A NanGate45-like stack: metal1..metal7 alternating H/V, 0.27 um spacing on metal4..6, 0.4 on metal7, and a
// memory that blocks metal1..metal4 (PG pins on metal4).
ph::Technology ng45_like() {
  ph::Technology t;
  t.dbu = 2000;
  for (int i = 1; i <= 7; ++i) {
    const auto name = "metal" + std::to_string(i);
    t.routing_layers.push_back(name);
    auto& l        = t.layers[name];
    l.direction    = i % 2 ? "HORIZONTAL" : "VERTICAL";
    l.rules.loaded = true;
    l.rules.spacing.push_back({i == 7 ? .4 : (i >= 4 ? .27 : .07), {}, {}});
  }
  auto& m  = t.macros["ram"];
  m.klass  = "BLOCK";
  m.width  = 57.57;
  m.height = 133;
  for (int i = 1; i <= 3; ++i) {
    m.obstructions.push_back({
        "metal" + std::to_string(i),
        {0, 0, 57.57, 133}
    });
  }
  m.pins["VDD"] = {"INOUT", "POWER", {{"metal4", {3.64, 2.1, 3.92, 130.9}}}};
  m.pins["VSS"] = {"INOUT", "GROUND", {{"metal4", {1.96, 2.1, 2.24, 130.9}}}};
  auto& c       = t.macros["inv"];
  c.klass       = "CORE";
  return t;
}
}  // namespace

TEST(PowerGridRules, Nangate45StrategyNeedsAlignedMetal7AcrossStackedMacros) {
  const auto  t     = ng45_like();
  const auto& pg    = ph::tech_profile("nangate45").pg;
  const auto  rules = pg_macro_rules(t, pg, {"ram", "inv"});
  // Stacked macros (gap along y): the metal4 strap segment between them is fed only by a metal7 strap of its net.
  const auto& y     = rules.gap[1];
  EXPECT_EQ(y.kind, Pg_gap_rule::Kind::lattice);
  EXPECT_EQ(y.source, "metal4");
  EXPECT_EQ(y.feed, "metal7");
  EXPECT_DOUBLE_EQ(y.margin, 2 + .27);  // macro-grid halo + metal4..6 spacing
  EXPECT_DOUBLE_EQ(y.period, 30);
  // The two nets sit 15 um apart: one VSS+VDD pair spans 15 + 1.4.
  EXPECT_NEAR(y.aligned, 16.4 + 2 * 2.27, 1e-9);
  EXPECT_NEAR(y.any_phase, 31.4 + 2 * 2.27, 1e-9);
  ASSERT_EQ(y.groups.size(), 2u);
  EXPECT_NEAR(y.groups[0][0], 1.3, 1e-9);
  EXPECT_NEAR(y.groups[0][1], 17.7, 1e-9);
  // Side by side (gap along x): the rails crossing the channel get metal4 feeders.
  const auto& x = rules.gap[0];
  EXPECT_EQ(x.kind, Pg_gap_rule::Kind::feeder);
  EXPECT_EQ(x.source, "metal1");
  EXPECT_EQ(x.feed, "metal4");
  EXPECT_NEAR(x.any_phase, 2 * 2.27 + 3 * .48 + 3 * .27, 1e-9);
  EXPECT_DOUBLE_EQ(x.aligned, x.any_phase);
  // No hard macros: nothing to keep apart.
  EXPECT_EQ(pg_macro_rules(t, pg, {"inv"}).gap[1].kind, Pg_gap_rule::Kind::none);
  EXPECT_THROW(pg_macro_rules(t, pg, {"missing"}), std::invalid_argument);
}

TEST(PowerGridRules, Asap7StrategyStrapsPassOverMacrosSoStackedMacrosAreFree) {
  ph::Technology t;
  const char*    dirs[] = {"HORIZONTAL", "HORIZONTAL", "VERTICAL", "HORIZONTAL", "VERTICAL", "HORIZONTAL", "VERTICAL"};
  for (int i = 1; i <= 7; ++i) {
    const auto name = "M" + std::to_string(i);
    t.routing_layers.push_back(name);
    t.layers[name].direction = dirs[i - 1];
    t.layers[name].rules.spacing.push_back({.072, {}, {}});
  }
  auto& m  = t.macros["ram"];
  m.klass  = "BLOCK";
  m.width  = 10;
  m.height = 10;
  for (int i = 1; i <= 4; ++i) {
    m.obstructions.push_back({
        "M" + std::to_string(i),
        {0, 0, 10, 10}
    });
  }
  const auto rules = pg_macro_rules(t, ph::tech_profile("asap7").pg, {"ram"});
  // M5 straps run over the macros (M1-M4 blocked): no isolated strap segment in a horizontal channel.
  EXPECT_EQ(rules.gap[1].kind, Pg_gap_rule::Kind::none);
  // Rails (M1/M2) in a vertical channel need M5 feeders.
  EXPECT_EQ(rules.gap[0].kind, Pg_gap_rule::Kind::feeder);
  EXPECT_EQ(rules.gap[0].source, "M1");
  EXPECT_EQ(rules.gap[0].feed, "M5");
  EXPECT_NEAR(rules.gap[0].any_phase, 2 * (2 + .072) + 3 * .12 + 3 * .072, 1e-9);
  EXPECT_FALSE(pg_macro_alignment(rules, {1000, 1000}, {0, 0}, 1000).active());
}

TEST(PowerGridRules, EdgeLatticeAllowsOnlyEdgesWithAStrapGroupBelow) {
  const auto t     = ng45_like();
  const auto rules = pg_macro_rules(t, ph::tech_profile("nangate45").pg, {"ram"});
  // dbu 1000, channel 22.4 um, strap origin 0.
  const auto align = pg_macro_alignment(rules, {7000, 22400}, {0, 0}, 1000);
  ASSERT_FALSE(align.axis[0].has_value());
  ASSERT_TRUE(align.axis[1].has_value());
  const auto& l = *align.axis[1];
  EXPECT_EQ(l.period, 30000);
  EXPECT_EQ(l.margin, 2270);
  // Group [1.3, 17.7]: e - 2.27 >= 17.7 and e - 22.4 + 2.27 <= 1.3, i.e. e in [19.97, 21.43] (+ k * 15).
  const int64_t lo = -1000000, hi = 10000000;
  EXPECT_FALSE(l.allows(19960 + 30000, 19960 + 30000 + 133000, lo, hi));
  EXPECT_TRUE(l.allows(19970 + 30000, 19970 + 30000 + 133000, lo, hi));
  EXPECT_TRUE(l.allows(21430 + 30000, 21430 + 30000 + 133000, lo, hi));
  EXPECT_FALSE(l.allows(21440 + 30000, 21440 + 30000 + 133000, lo, hi));
  // The other net order (VDD then VSS) gives the half-pitch windows.
  EXPECT_TRUE(l.allows(4970 + 30000, 4970 + 30000 + 133000, lo, hi));
  EXPECT_TRUE(l.allows(6430, 6430 + 133000, lo, hi));
  int allowed = 0;
  for (int64_t e = 100000; e < 130000; e += 10) {
    allowed += l.allows(e, e + 133000, lo, hi);
  }
  EXPECT_EQ(allowed, 2 * 147);  // two windows of 1.46 um per 30 um pitch, 10 DBU steps
  // Near the core bottom the band below stops at the core edge: a 10 um sliver holds no full VSS+VDD group.
  EXPECT_FALSE(l.allows(10000, 143000, 0, hi));
  EXPECT_TRUE(l.allows(2000, 135000, 0, hi));  // within the margin: no rails below
  // Near the core top the band above the macro must hold a group as well.
  EXPECT_FALSE(l.allows(19970, 152970, lo, 160000));
  EXPECT_TRUE(l.fits(1300, 17700));
  EXPECT_FALSE(l.fits(1310, 17700));
  EXPECT_TRUE(l.fits(16300 - 30000, 2700));
}

TEST(PowerGridRules, Nangate45MacroGridStrapsCoverEachMacro) {
  auto t = ng45_like();
  t.sites.emplace("core", ph::Site{.19, 1.4, true});
  auto& c       = t.macros["inv"];
  c.site        = "core";
  c.width       = .38;
  c.height      = 1.4;
  c.pins["VDD"] = {"INOUT", "POWER", {{"metal1", {0, 1.315, .38, 1.485}}}};
  c.pins["VSS"] = {"INOUT", "GROUND", {{"metal1", {0, -.085, .38, .085}}}};
  ph::Floorplan f;
  f.dbu = 2000;
  f.die = {0, 0, 1000000, 1000000};
  for (int64_t i = 0; i < 300; ++i) {
    f.rows.push_back(
        {"r" + std::to_string(i), "core", 2000, 2000 + 2800 * i, 1000, 380, i % 2 ? ph::Orientation::FS : ph::Orientation::N});
  }
  Ann_place loc(100000, 100000, 115140, 266000);
  loc.status = ph::Status::FIXED;
  f.components.push_back({"mem", "ram", loc});
  const auto     p    = power_grid_profile("nangate45", t);
  const auto     plan = plan_power_grid(t, f, p);
  // Macro-grid metal5/metal6 straps: optional, pitch origin at the macro corner, length over the PG pin box.
  const ph::Rect pins{100000 + 3920, 100000 + 4200, 100000 + 7840, 100000 + 261800};
  int            m6 = 0;
  for (const auto& s : plan.metal) {
    if (s.layer == "metal5" || s.layer == "metal6") {
      EXPECT_TRUE(s.optional);
      if (s.layer == "metal5") {
        EXPECT_EQ(s.rect.xl, pins.xl);
        EXPECT_EQ(s.rect.xh, pins.xh);
        EXPECT_GE(s.rect.yl, 100000 + 4000 - 930);
        EXPECT_LE(s.rect.yh, 100000 + 266000 + 930);
      } else {
        ++m6;
        EXPECT_EQ(s.rect.yl, pins.yl);
        EXPECT_EQ(s.rect.yh, pins.yh);
      }
    } else {
      EXPECT_FALSE(s.optional);
    }
  }
  EXPECT_EQ(m6, 12);  // 2 nets x 6 pitches of 10 um across 57.57 um
  EXPECT_TRUE(std::any_of(plan.metal.begin(), plan.metal.end(), [&](const auto& s) {
    // VSS (first net) metal6 strap at macro x + 2 um, 0.93 um wide.
    return s.layer == "metal6" && s.net == "VSS" && s.rect.xl == 100000 + 4000 - 930 && s.rect.xh == 100000 + 4000 + 930;
  }));
  // Core metal4 straps stop at the 2 um halo; metal7 crosses the macro.
  const ph::Rect halo{96000, 96000, 219140, 370000};
  for (const auto& s : plan.metal) {
    if (s.layer == "metal4") {
      EXPECT_FALSE(s.rect.overlaps(halo));
    }
  }
  EXPECT_TRUE(std::any_of(plan.metal.begin(), plan.metal.end(), [&](const auto& s) {
    return s.layer == "metal7" && s.rect.overlaps(halo);
  }));
  auto ringed = p;
  ringed.rings.push_back({
      {"metal7", "metal6"},
      2,
      2,
      2
  });
  EXPECT_THROW(plan_power_grid(t, f, ringed), std::invalid_argument);
  // A movable, pinless soft block is an area of cells: the grid ignores it.
  auto& soft          = t.macros["soft"];
  soft.klass          = "BLOCK SOFT";
  soft.width          = 50;
  soft.height         = 50;
  auto      with_soft = f;
  Ann_place soft_loc(600000, 600000, 700000, 700000);
  soft_loc.status = ph::Status::PLACED;
  with_soft.components.push_back({"blob", "soft", soft_loc});
  Power_grid_plan soft_plan;
  ASSERT_NO_THROW(soft_plan = plan_power_grid(t, with_soft, p));
  EXPECT_EQ(soft_plan.metal.size(), plan.metal.size());
}
}  // namespace livehd::placement
namespace livehd::placement {
TEST(PowerGridRules, FeederSpacingFollowsTheGeneratorWithoutLefRules) {
  // Without LEF spacing rules the generator keeps the metal4 strap family's spacing (27.52 um) between feeders; the
  // channel rule must reserve the same room, not zero spacing.
  auto t = ng45_like();
  for (auto& [name, layer] : t.layers) {
    layer.rules.spacing.clear();
  }
  const auto& pg     = ph::tech_profile("nangate45").pg;
  const auto  rules  = pg_macro_rules(t, pg, {"ram"});
  const auto& feeder = rules.gap[0];
  ASSERT_EQ(feeder.kind, Pg_gap_rule::Kind::feeder);
  EXPECT_DOUBLE_EQ(pg_feeder_spacing(t.layers.at("metal4"), pg.stripes[0]), 27.52);
  EXPECT_NEAR(feeder.margin, 2 + 27.52, 1e-9);
  EXPECT_NEAR(feeder.any_phase, 2 * (2 + 27.52) + 3 * .48 + 3 * 27.52, 1e-9);
  // With a LEF rule the tighter one wins, as in the generator.
  EXPECT_DOUBLE_EQ(pg_feeder_spacing(ng45_like().layers.at("metal4"), pg.stripes[0]), .27);
}

TEST(PowerGridRules, VirtualMemoryProxiesBlockEveryLayerAsInTheGenerator) {
  // The generator keeps every layer off a synthesized memory proxy; the rules must see the same blockage as for a
  // real macro obstructing the whole stack.
  auto  t             = ng45_like();
  auto& proxy         = t.macros["proxy"];
  proxy.klass         = "BLOCK";
  proxy.width         = 50;
  proxy.height        = 50;
  proxy.virtual_proxy = true;
  auto& full          = t.macros["full"];
  full                = proxy;
  full.virtual_proxy  = false;
  for (int i = 1; i <= 7; ++i) {
    full.obstructions.push_back({
        "metal" + std::to_string(i),
        {0, 0, 50, 50}
    });
  }
  const auto& pg         = ph::tech_profile("nangate45").pg;
  const auto  from_proxy = pg_macro_rules(t, pg, {"proxy"});
  const auto  from_full  = pg_macro_rules(t, pg, {"full"});
  for (int axis = 0; axis < 2; ++axis) {
    EXPECT_NE(from_proxy.gap[axis].kind, Pg_gap_rule::Kind::none) << axis;
    EXPECT_EQ(from_proxy.gap[axis].kind, from_full.gap[axis].kind) << axis;
    EXPECT_EQ(from_proxy.gap[axis].source, from_full.gap[axis].source) << axis;
    EXPECT_EQ(from_proxy.gap[axis].feed, from_full.gap[axis].feed) << axis;
    EXPECT_DOUBLE_EQ(from_proxy.gap[axis].any_phase, from_full.gap[axis].any_phase) << axis;
  }
}

TEST(PowerGridRules, StrategyForeignToTheLefStatesNoRule) {
  auto  t = physical::Technology{};
  auto& m = t.macros["ram"];
  m.klass = "BLOCK";
  m.obstructions.push_back({
      "M1",
      {0, 0, 1, 1}
  });
  t.layers["M1"].direction = "HORIZONTAL";  // M2..M6 missing
  t.routing_layers         = {"M1"};
  const auto rules         = pg_macro_rules(t, physical::tech_profile("asap7").pg, {"ram"});
  EXPECT_EQ(rules.gap[0].kind, Pg_gap_rule::Kind::none);
  EXPECT_EQ(rules.gap[1].kind, Pg_gap_rule::Kind::none);
}
}  // namespace livehd::placement

namespace livehd::placement {
// macro.pdn levels: gap keeps the any-phase spacing, align the aligned one plus a tenth of the feed pitch, off none.
TEST(PowerGridRules, MacroLevelsWeakenFromGapToAlignToOff) {
  const auto  rules = pg_macro_rules(ng45_like(), physical::tech_profile("nangate45").pg, {"ram"});
  const auto& y     = rules.gap[1];
  EXPECT_NEAR(pg_macro_requirement(y, Pg_macro_level::gap), y.any_phase, 1e-9);
  EXPECT_NEAR(pg_macro_requirement(y, Pg_macro_level::align), y.aligned + y.period / 10, 1e-9);
  EXPECT_EQ(pg_macro_requirement(y, Pg_macro_level::off), 0);
  EXPECT_GT(pg_macro_requirement(y, Pg_macro_level::gap), pg_macro_requirement(y, Pg_macro_level::align));
  // A feeder rule has no lattice: aligned or not, the same spacing.
  const auto& x = rules.gap[0];
  EXPECT_EQ(pg_macro_requirement(x, Pg_macro_level::gap), pg_macro_requirement(x, Pg_macro_level::align));
  EXPECT_EQ(pg_macro_requirement(x, Pg_macro_level::off), 0);
  EXPECT_STREQ(pg_macro_level_name(Pg_macro_level::gap), "gap");
  EXPECT_STREQ(pg_macro_level_name(Pg_macro_level::align), "align");
  EXPECT_STREQ(pg_macro_level_name(Pg_macro_level::off), "off");
}

// The level feasibility check is a necessary condition: per axis, a line across the core crosses at most
// floor(extent / smallest box) boxes.
TEST(PowerGridRules, MacroBoxesFitIsANecessaryPackingCondition) {
  EXPECT_TRUE(pg_macro_boxes_fit({}, 0, 0));
  EXPECT_FALSE(pg_macro_boxes_fit(
      {
          {10, 10}
  },
      0,
      10));
  EXPECT_FALSE(pg_macro_boxes_fit(
      {
          {11, 5}
  },
      10,
      100));  // wider than the core
  // Two 10x30 boxes in a 15x50 core: area fits (600 <= 750) but only one column exists and 60 > 50 high.
  EXPECT_FALSE(pg_macro_boxes_fit(
      {
          {10, 30},
          {10, 30}
  },
      15,
      50));
  EXPECT_TRUE(pg_macro_boxes_fit(
      {
          {10, 30},
          {10, 30}
  },
      15,
      60));
  // Side by side the same way.
  EXPECT_FALSE(pg_macro_boxes_fit(
      {
          {30, 10},
          {30, 10}
  },
      50,
      15));
  EXPECT_TRUE(pg_macro_boxes_fit(
      {
          {30, 10},
          {30, 10}
  },
      60,
      15));
  // Area alone: four 10x10 in 19x19 (each axis holds one per line, 40 > 19).
  EXPECT_FALSE(pg_macro_boxes_fit(
      {
          {10, 10},
          {10, 10},
          {10, 10},
          {10, 10}
  },
      19,
      19));
  EXPECT_TRUE(pg_macro_boxes_fit(
      {
          {10, 10},
          {10, 10},
          {10, 10},
          {10, 10}
  },
      20,
      20));
  // NanGate45 ariane136 at the gap level (DBU 2000): 136 fakeram45_256x16 with the native footprint 64.03 x 166.6 um in
  // the 1436.2 x 1436.4 um core: 22 columns x 8 tiers can hold them, so the check passes (the heuristic legalizer
  // may still fail, which is when macro.pdn=auto degrades by retrying).
  std::vector<Pg_macro_box> ariane(136, Pg_macro_box{128060, 333200});
  EXPECT_TRUE(pg_macro_boxes_fit(ariane, 2872420, 2872800));
  // ... 180 of them cannot (180 x 64.03 um of widths > 8 tiers x 1436.2 um).
  ariane.resize(180, Pg_macro_box{128060, 333200});
  EXPECT_FALSE(pg_macro_boxes_fit(ariane, 2872420, 2872800));
}
}  // namespace livehd::placement
namespace livehd::placement {
// synth.place.pg=auto (the documented way to enable the native grid) resolves the macro.pdn strategy from the LEF,
// like no pg; a named profile is used as given. Default macro.pdn=auto used to look "auto" up as a profile name and
// throw "unknown technology profile: auto" in every pg=auto placement.
TEST(PowerGridRules, MacroStrategyResolvesPgAutoFromTheLef) {
  auto t                                      = ng45_like();
  t.sites["FreePDK45_38x28_10R_NP_162NW_34O"] = {.19, 1.4, true};
  const auto* ng45                            = &ph::tech_profile("nangate45").pg;
  EXPECT_EQ(pg_macro_strategy(t, "auto"), ng45);
  EXPECT_EQ(pg_macro_strategy(t, "none"), ng45);
  EXPECT_EQ(pg_macro_strategy(t, "nangate45"), ng45);
  EXPECT_THROW(pg_macro_strategy(t, "bogus"), std::invalid_argument);
  EXPECT_EQ(pg_macro_strategy(ph::Technology{}, "auto"), nullptr);  // nothing detected
}
}  // namespace livehd::placement
