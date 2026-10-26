// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pin_access.hpp"

#include <algorithm>

#include "gtest/gtest.h"
#include "routing_occupancy.hpp"

namespace livehd::physical {
namespace {
Technology technology() {
  Technology t;
  t.manufacturing_grid = .001;
  t.routing_layers     = {"M1", "M2"};
  for (const auto& name : t.routing_layers) {
    auto& layer         = t.layers[name];
    layer.width         = .018;
    layer.direction     = name == "M1" ? "VERTICAL" : "HORIZONTAL";
    layer.rules.loaded  = true;
    layer.rules.area    = .000666;
    layer.rules.spacing = {{.018, {}, {}}};
  }
  t.layers["M2"].rules.min_size                               = {{.037, .018}};
  t.layers["M2"].rules.properties["LEF58_RIGHTWAYONGRIDONLY"] = {"RIGHTWAYONGRIDONLY ;"};
  t.cut_layers["V1"].width                                    = .018;
  t.cut_layers["V1"].spacing                                  = {.018};
  t.vias["VIA12"].shapes
      = {{"M1", {-.009, -.011, .009, .011}}, {"M2", {-.014, -.009, .014, .009}}, {"V1", {-.009, -.009, .009, .009}}};
  auto& cell     = t.macros["cell"];
  cell.klass     = "CORE";
  cell.width     = .162;
  cell.height    = .270;
  cell.pins["A"] = {"INPUT", "SIGNAL", {{"M1", {.018, .027, .036, .243}}}};
  return t;
}
Floorplan frame() {
  Floorplan f;
  f.die    = {0, 0, 3000, 3000};
  f.tracks = {{"Y", "M2", 1045, 6, 36}};
  Ann_place place(1000, 1000, 162, 270);
  place.status = Status::PLACED;
  f.components.push_back({"u0", "cell", place});
  return f;
}
const Pin_access_layers& asap7_access() { return tech_profile("asap7").route.access; }
}  // namespace
TEST(PinAccess, FitsLowerPinAndBuildsMinimumAreaOnTrackEscape) {
  const auto tech = technology();
  const auto fp   = frame();
  const auto r    = enumerate_pin_access(tech, fp, fp.components[0], "A", {}, Signal_metal_rules(tech, fp), 1000, asap7_access());
  ASSERT_EQ(r.points.size(), 6);
  EXPECT_FALSE(r.exhausted);
  for (const auto& p : r.points) {
    EXPECT_EQ(p.x, 1027);
    EXPECT_EQ(p.via, "VIA12");
    EXPECT_EQ(p.escape.rect.xh - p.escape.rect.xl, 38);
    EXPECT_TRUE(Signal_metal_rules(tech, fp).rectangle(p.escape).legal);
  }
}
TEST(PinAccess, ForeignMetalAndCutsRemoveOnlyAffectedCandidates) {
  const auto               tech      = technology();
  const auto               fp        = frame();
  const std::vector<Shape> obstacles = {{"M2", {1050, 1036, 1100, 1054}}, {"V1", {1018, 1072, 1036, 1090}}};
  const auto r
      = enumerate_pin_access(tech, fp, fp.components[0], "A", obstacles, Signal_metal_rules(tech, fp), 1000, asap7_access());
  ASSERT_EQ(r.points.size(), 4);
  for (const auto& p : r.points) {
    EXPECT_NE(p.y, 1045);
    EXPECT_NE(p.y, 1081);
  }
}
TEST(PinAccess, PinUnionDoesNotCreateFalseGapsOrFillRealHoles) {
  auto       tech                      = technology();
  const auto fp                        = frame();
  tech.macros["cell"].pins["A"].shapes = {{"M1", {.018, .027, .036, .134}}, {"M1", {.018, .134, .036, .243}}};
  auto r = enumerate_pin_access(tech, fp, fp.components[0], "A", {}, Signal_metal_rules(tech, fp), 1000, asap7_access());
  EXPECT_EQ(r.points.size(), 6);
  tech.macros["cell"].pins["A"].shapes = {{"M1", {.018, .027, .036, .110}}, {"M1", {.018, .112, .036, .243}}};
  r = enumerate_pin_access(tech, fp, fp.components[0], "A", {}, Signal_metal_rules(tech, fp), 1000, asap7_access());
  EXPECT_EQ(r.points.size(), 5);
  EXPECT_TRUE(std::none_of(r.points.begin(), r.points.end(), [](const auto& p) { return p.y == 1117; }));
}
TEST(PinAccess, MirroringAndUnsupportedInputs) {
  auto tech                            = technology();
  auto fp                              = frame();
  tech.macros["cell"].pins["A"].shapes = {{"M1", {.018, .027, .036, .207}}};
  fp.components[0].place.orientation   = Orientation::FS;
  auto r = enumerate_pin_access(tech, fp, fp.components[0], "A", {}, Signal_metal_rules(tech, fp), 1000, asap7_access());
  ASSERT_EQ(r.points.size(), 5);
  EXPECT_EQ(r.points.front().y, 1081);
  tech.cut_layers["V1"].unsupported.insert("qualified SPACING");
  r = enumerate_pin_access(tech, fp, fp.components[0], "A", {}, Signal_metal_rules(tech, fp), 1000, asap7_access());
  EXPECT_TRUE(r.points.empty());
  EXPECT_TRUE(r.rejected.contains("unsupported V1 cut rules"));
}
TEST(PinAccess, BudgetBoundsEnumeration) {
  const auto tech = technology();
  const auto fp   = frame();
  const auto r    = enumerate_pin_access(tech, fp, fp.components[0], "A", {}, Signal_metal_rules(tech, fp), 6, asap7_access());
  EXPECT_LE(r.examined, 6);
  const auto limited = enumerate_pin_access(tech, fp, fp.components[0], "A", {}, Signal_metal_rules(tech, fp), 5, asap7_access());
  EXPECT_TRUE(limited.exhausted);
  EXPECT_LE(limited.examined, 5);
}
TEST(PinAccess, WidePinUsesTrackCrossingsAndCenter) {
  auto tech                            = technology();
  auto fp                              = frame();
  tech.macros["cell"].pins["A"].shapes = {{"M1", {.018, .027, .090, .243}}};
  fp.tracks.push_back({"X", "M1", 1027, 2, 36});
  const auto r = enumerate_pin_access(tech, fp, fp.components[0], "A", {}, Signal_metal_rules(tech, fp), 1000, asap7_access());
  ASSERT_EQ(r.points.size(), 18);
  EXPECT_EQ(r.examined, 18);
  for (const auto& p : r.points) {
    EXPECT_TRUE(p.x == 1027 || p.x == 1054 || p.x == 1063);
  }
  fp.tracks.back().step = 0;
  const auto bad
      = enumerate_pin_access(tech, fp, fp.components[0], "A", {}, Signal_metal_rules(tech, fp), 1000, asap7_access());
  EXPECT_TRUE(bad.points.empty());
  EXPECT_TRUE(bad.rejected.contains("unsupported M1 tracks"));
}
TEST(PinAccess, IndexedAndFullScanChecksAgreeAndSameNetCutsStillBlock) {
  const auto               tech = technology();
  const auto               fp   = frame();
  const Signal_metal_rules rules(tech, fp);
  const std::vector<Shape> obstacles = {{"M2", {1050, 1036, 1100, 1054}}, {"V1", {1018, 1072, 1036, 1090}}};
  Routing_occupancy        index(64);
  index.insert({obstacles[0], "foreign"});
  index.insert({obstacles[1], "self"});
  const auto flat    = enumerate_pin_access(tech, fp, fp.components[0], "A", obstacles, rules, 1000, asap7_access());
  const auto indexed = enumerate_pin_access(tech, fp, fp.components[0], "A", index, rules, 1000, "self", asap7_access());
  ASSERT_EQ(indexed.points.size(), flat.points.size());
  EXPECT_EQ(indexed.rejected, flat.rejected);
  for (size_t i = 0; i < flat.points.size(); ++i) {
    EXPECT_EQ(indexed.points[i].x, flat.points[i].x);
    EXPECT_EQ(indexed.points[i].y, flat.points[i].y);
    EXPECT_EQ(indexed.points[i].escape, flat.points[i].escape);
  }
}
TEST(PinAccess, DefViaCannotShadowValidatedLefTemplate) {
  const auto tech = technology();
  auto       fp   = frame();
  fp.vias.push_back({"VIA12", {{"M1", {-100, -100, 100, 100}}, {"M2", {-100, -100, 100, 100}}, {"V1", {-50, -50, 50, 50}}}, {}});
  const auto r = enumerate_pin_access(tech, fp, fp.components[0], "A", {}, Signal_metal_rules(tech, fp), 1000, asap7_access());
  EXPECT_TRUE(r.points.empty());
  EXPECT_EQ(r.rejected.at("DEF overrides LEF access via"), 1);
}
}  // namespace livehd::physical
