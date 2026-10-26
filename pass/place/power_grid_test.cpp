#include "power_grid.hpp"

#include <algorithm>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
namespace ph = physical;
ph::Technology technology() {
  ph::Technology t;
  t.sites.emplace("site", ph::Site{.054, .270, true});
  auto& m                  = t.macros["cell"];
  m.klass                  = "CORE";
  m.site                   = "site";
  m.width                  = .108;
  m.height                 = .270;
  m.pins["VDD"]            = {"INOUT", "POWER", {{"M1", {0, .261, .108, .279}}}};
  m.pins["VSS"]            = {"INOUT", "GROUND", {{"M1", {0, -.009, .108, .009}}}};
  t.layers["M1"].direction = "HORIZONTAL";
  t.layers["M2"].direction = "HORIZONTAL";
  t.layers["M5"].direction = "VERTICAL";
  t.layers["M6"].direction = "HORIZONTAL";
  return t;
}
ph::Floorplan frame() {
  ph::Floorplan f;
  f.die = {0, 0, 18252, 18360};
  for (int64_t i = 0; i < 48; ++i) {
    f.rows.push_back(
        {"r" + std::to_string(i), "site", 2700, 2700 + 270 * i, 238, 54, i % 2 ? ph::Orientation::FS : ph::Orientation::N});
  }
  return f;
}
}  // namespace
TEST(PowerGrid, Asap7RailAndStripePlanningBeforeVias) {
  const auto t    = technology();
  const auto plan = plan_power_grid(t, frame(), power_grid_profile("pg_asap7", t));
  EXPECT_EQ(plan.core, (ph::Rect{2700, 2700, 15552, 15660}));
  EXPECT_EQ(plan.rail_count.at("M1"), 49);
  EXPECT_EQ(plan.rail_count.at("M2"), 49);
  EXPECT_EQ(plan.stripe_count.at("M5"), 6);
  EXPECT_EQ(plan.stripe_count.at("M6"), 6);
  auto contains = [&](const std::string& net, const std::string& layer, const ph::Rect& rect) {
    return std::any_of(plan.metal.begin(), plan.metal.end(), [&](const auto& shape) {
      return shape.net == net && shape.layer == layer && shape.rect == rect;
    });
  };
  EXPECT_TRUE(contains("VSS", "M1", {2700, 2691, 15552, 2709}));
  EXPECT_TRUE(contains("VDD", "M2", {2700, 2961, 15552, 2979}));
  EXPECT_TRUE(contains("VSS", "M5", {2940, 2691, 3060, 15669}));
  EXPECT_TRUE(contains("VDD", "M5", {3132, 2691, 3252, 15669}));
  EXPECT_TRUE(contains("VDD", "M6", {2700, 3453, 15552, 3741}));
}
TEST(PowerGrid, ShortRowsReceiveSupplementalFeeders) {
  const auto tech = technology();
  auto       fp   = frame();
  for (size_t i = 1; i + 1 < fp.rows.size(); ++i) {
    fp.rows[i].x     = 6000;
    fp.rows[i].count = 20;
  }
  const auto profile  = power_grid_profile("pg_asap7", tech);
  const auto plan     = plan_power_grid(tech, fp, profile);
  const auto baseline = plan_power_grid(tech, frame(), profile);
  EXPECT_GT(plan.stripe_count.at("M5"), baseline.stripe_count.at("M5"));
  for (const auto& rail : plan.metal) {
    if (!rail.followpin || rail.layer != "M2") {
      continue;
    }
    EXPECT_TRUE(std::any_of(plan.metal.begin(), plan.metal.end(), [&](const auto& feeder) {
      return feeder.layer == "M5" && feeder.net == rail.net && feeder.rect.xl >= rail.rect.xl && feeder.rect.xh <= rail.rect.xh
             && feeder.rect.yl <= rail.rect.yl && feeder.rect.yh >= rail.rect.yh;
    }));
  }
  for (size_t i = 0; i < plan.metal.size(); ++i) {
    const auto& a = plan.metal[i];
    if (a.layer != "M5") {
      continue;
    }
    for (size_t j = i + 1; j < plan.metal.size(); ++j) {
      const auto& b = plan.metal[j];
      if (b.layer == "M5") {
        EXPECT_GE(std::max(a.rect.xl - b.rect.xh, b.rect.xl - a.rect.xh), 72);
      }
    }
  }
  // A rail shorter than the feeder width cannot acquire a legal landing.
  for (size_t i = 1; i + 1 < fp.rows.size(); ++i) {
    fp.rows[i].count = 1;
  }
  EXPECT_THROW(plan_power_grid(tech, fp, profile), std::invalid_argument);
}
TEST(PowerGrid, SplitRowsAndMacroHalosCutTheMetal) {
  auto t = technology();
  auto f = frame();
  f.rows.clear();
  f.rows.push_back({"left", "site", 2700, 2700, 20, 54, ph::Orientation::N});
  f.rows.push_back({"right", "site", 4860, 2700, 20, 54, ph::Orientation::N});
  auto p = power_grid_profile("pg_asap7", t);
  p.stripes.clear();
  auto plan = plan_power_grid(t, f, p);
  EXPECT_EQ(plan.rail_count.at("M1"), 4);
  for (const auto& s : plan.metal) {
    EXPECT_TRUE(s.rect.xh <= 3780 || s.rect.xl >= 4860);
  }
  f            = frame();
  auto& macro  = t.macros["ram"];
  macro.klass  = "BLOCK";
  macro.width  = 1;
  macro.height = 1;
  macro.obstructions.push_back({"M5", {0, 0, 1, 1}});
  Ann_place loc(2900, 8000, 1000, 1000);
  loc.status = ph::Status::LOCKED;
  f.components.push_back({"mem", "ram", loc});
  p    = power_grid_profile("pg_asap7", t);
  plan = plan_power_grid(t, f, p);
  const ph::Rect halo{900, 6000, 5900, 11000};
  for (const auto& s : plan.metal) {
    if (s.layer == "M5") {
      EXPECT_FALSE(s.rect.xl < halo.xh && halo.xl < s.rect.xh && s.rect.yl < halo.yh && halo.yl < s.rect.yh);
    }
  }
  EXPECT_TRUE(std::any_of(plan.metal.begin(), plan.metal.end(), [&](const auto& s) {
    return s.layer == "M6" && s.rect.xl < halo.xh && halo.xl < s.rect.xh && s.rect.yl < halo.yh && halo.yl < s.rect.yh;
  }));
  f.components[0].place.status = ph::Status::PLACED;
  EXPECT_THROW(plan_power_grid(t, f, p), std::invalid_argument);
}
TEST(PowerGrid, RejectsUnsupportedInputsAndBoundaryRailClipping) {
  auto t = technology();
  auto f = frame();
  auto p = power_grid_profile("pg_asap7", t);
  EXPECT_THROW(power_grid_profile("auto", t), std::invalid_argument);
  EXPECT_THROW(power_grid_profile("missing", t), std::invalid_argument);
  auto bad = p;
  bad.unsupported.push_back("rings");
  EXPECT_THROW(plan_power_grid(t, f, bad), std::invalid_argument);
  bad                  = p;
  bad.stripes[0].pitch = .1;
  EXPECT_THROW(plan_power_grid(t, f, bad), std::invalid_argument);
  f.die.yl = 2700;
  EXPECT_THROW(plan_power_grid(t, f, p), std::invalid_argument);
  f                     = frame();
  f.rows[1].orientation = ph::Orientation::N;
  EXPECT_THROW(plan_power_grid(t, f, p), std::invalid_argument);
  f = frame();
  f.rows.push_back(f.rows.front());
  EXPECT_THROW(plan_power_grid(t, f, p), std::invalid_argument);
  f                                = frame();
  t.macros["cell"].pins["VDD"].use = "SIGNAL";
  EXPECT_THROW(plan_power_grid(t, f, p), std::invalid_argument);
}
TEST(PowerGrid, ChannelSkipAndGapPreserveRailsAndRejectSlivers) {
  auto tech               = technology();
  tech.manufacturing_grid = .001;
  auto& rule              = tech.layers["M5"].rules;
  rule.loaded             = true;
  rule.area               = .01;
  rule.spacing            = {{.04, {}, {}}};
  auto       p            = power_grid_profile("pg_asap7", tech);
  const auto base         = plan_power_grid(tech, frame(), p);
  p.channels              = {{"M5", "skip", {2.94, 0, 3.06, 18}}};
  const auto skip         = plan_power_grid(tech, frame(), p);
  EXPECT_EQ(skip.stripe_count.at("M5"), 5);
  EXPECT_EQ(skip.rail_count, base.rail_count);
  EXPECT_NEAR(skip.channel_removed_length_um.at("M5"), 12.978, 1e-9);
  p.channels[0]  = {"M5", "gap", {2.94, 7, 3.06, 8}};
  const auto gap = plan_power_grid(tech, frame(), p);
  EXPECT_EQ(gap.stripe_count.at("M5"), 7);
  EXPECT_DOUBLE_EQ(gap.channel_removed_length_um.at("M5"), 1);
  for (const auto& metal : gap.metal) {
    if (metal.layer == "M5" && metal.rect.xl == 2940) {
      EXPECT_TRUE(metal.rect.yh <= 7000 || metal.rect.yl >= 8000);
    }
  }
  p.channels[0].style = "skip";
  EXPECT_THROW(plan_power_grid(tech, frame(), p), std::invalid_argument);
  p.channels[0] = {"M5", "gap", {2.94, 2.7, 3.06, 8}};  // 9 DBU remnant below minimum area.
  EXPECT_THROW(plan_power_grid(tech, frame(), p), std::invalid_argument);
  p.channels[0] = {"M5", "gap", {2.94, 7, 3.06, 7.01}};  // Too little end spacing.
  EXPECT_THROW(plan_power_grid(tech, frame(), p), std::invalid_argument);
  p.channels[0] = {"M5", "gap", {2.95, 7, 3.06, 8}};  // Partial stripe width.
  EXPECT_THROW(plan_power_grid(tech, frame(), p), std::invalid_argument);
  p.channels[0] = {"M2", "skip", {0, 0, 18, 18}};
  EXPECT_THROW(plan_power_grid(tech, frame(), p), std::invalid_argument);
}
TEST(PowerGrid, ChannelOptionsParseStrictlyAndKeepOrder) {
  EXPECT_TRUE(parse_pg_channels("").empty());
  EXPECT_TRUE(parse_pg_channels("  ").empty());
  const auto channels = parse_pg_channels("skip,M6,0,6.039,13,6.327; gap, M6,2,6.039,4,6.327");
  ASSERT_EQ(channels.size(), 2);
  EXPECT_EQ(channels[0].style, "skip");
  EXPECT_EQ(channels[1].style, "gap");
  EXPECT_EQ(channels[1].layer, "M6");
  EXPECT_EQ(channels[1].region, (ph::Rect{2, 6.039, 4, 6.327}));
  for (const auto* invalid : {";",
                              "skip,M6,0,0,1,1;",
                              "skip M6 0 0 1 1",
                              "skip,,0,0,1,1",
                              "other,M6,0,0,1,1",
                              "gap,M6,0,0,0,1",
                              "gap,M6,0,0,1,1,junk",
                              "gap,M6,0,0,1,1junk",
                              "gap,M6,0,0,nan,1",
                              "gap,M6,0,0,inf,1",
                              "gap,M6,0,0,1e999,1"}) {
    SCOPED_TRACE(invalid);
    EXPECT_THROW(parse_pg_channels(invalid), std::invalid_argument);
  }
}
}  // namespace livehd::placement
