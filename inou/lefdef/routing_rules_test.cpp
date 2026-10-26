// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "routing_rules.hpp"

#include "gtest/gtest.h"

namespace livehd::physical {
namespace {
Technology technology() {
  Technology tech;
  tech.manufacturing_grid = .001;
  auto& layer             = tech.layers["M2"];
  layer.width             = .018;
  layer.rules.loaded      = true;
  layer.rules.area        = .000666;
  layer.rules.min_size    = {{.037, .018}};
  layer.rules.spacing.push_back({.018, {}, {}});
  return tech;
}
Shape metal(double xl, double yl, double xh, double yh) { return {"M2", {xl, yl, xh, yh}}; }
}  // namespace
TEST(RoutingRules, RectangleWidthAreaSizeAndManufacturingGrid) {
  auto tech = technology();
  EXPECT_TRUE(check_metal_rectangle(tech, 1000, metal(0, 0, 37, 18)).legal);
  EXPECT_TRUE(check_metal_rectangle(tech, 1000, metal(0, 0, 18, 37)).legal);
  EXPECT_EQ(check_metal_rectangle(tech, 1000, metal(0, 0, 100, 17)).reason, "minimum width");
  EXPECT_EQ(check_metal_rectangle(tech, 1000, metal(0, 0, 36, 18)).reason, "minimum area");
  EXPECT_EQ(check_metal_rectangle(tech, 1000, metal(0, 0, 30, 30)).reason, "minimum size");
  tech.layers["M2"].rules.max_width = .036;
  EXPECT_EQ(check_metal_rectangle(tech, 1000, metal(0, 0, 50, 37)).reason, "maximum width");
  tech.manufacturing_grid = .002;
  EXPECT_EQ(check_metal_rectangle(tech, 1000, metal(0, 0, 37, 18)).reason, "off manufacturing grid");
  EXPECT_TRUE(check_metal_rectangle(tech, 1000, metal(0, 0, 38, 18)).legal);
}
TEST(RoutingRules, SpacingTouchRangeAndConservativeCorners) {
  auto       tech = technology();
  const auto a    = metal(0, 0, 100, 18);
  EXPECT_FALSE(check_metal_spacing(tech, 1000, a, metal(0, 18, 100, 36)).legal);
  EXPECT_FALSE(check_metal_spacing(tech, 1000, a, metal(0, 35, 100, 53)).legal);
  EXPECT_TRUE(check_metal_spacing(tech, 1000, a, metal(0, 36, 100, 54)).legal);
  EXPECT_FALSE(check_metal_spacing(tech, 1000, a, metal(117, 35, 200, 53)).legal);
  EXPECT_TRUE(check_metal_spacing(tech, 1000, a, metal(118, 35, 200, 53)).legal);
  tech.layers["M2"].rules.spacing.push_back({.030, std::array{.036, .100}, {}});
  EXPECT_TRUE(check_metal_spacing(tech, 1000, a, metal(0, 36, 100, 54)).legal);
  EXPECT_FALSE(check_metal_spacing(tech, 1000, a, metal(0, 36, 100, 72)).legal);
  EXPECT_TRUE(check_metal_spacing(tech, 1000, a, metal(0, 48, 100, 84)).legal);
}
TEST(RoutingRules, ParallelRunTableAndConditionalEolBound) {
  auto  tech  = technology();
  auto& rules = tech.layers["M2"].rules;
  rules.parallel.push_back({{0, .036}, {0, .100}, {{.018, .020}, {.024, .032}}});
  EXPECT_TRUE(check_metal_spacing(tech, 1000, metal(0, 0, 99, 36), metal(0, 60, 99, 96)).legal);
  EXPECT_FALSE(check_metal_spacing(tech, 1000, metal(0, 0, 100, 36), metal(0, 60, 100, 96)).legal);
  EXPECT_TRUE(check_metal_spacing(tech, 1000, metal(0, 0, 100, 36), metal(0, 68, 100, 104)).legal);
  rules.spacing.push_back({.040, {}, Eol_spacing{.025, .020, std::array{.045, .020}, false}});
  EXPECT_FALSE(check_metal_spacing(tech, 1000, metal(0, 0, 100, 18), metal(0, 62, 100, 80)).legal);
  EXPECT_TRUE(check_metal_spacing(tech, 1000, metal(0, 0, 100, 18), metal(0, 63, 100, 81)).legal);
}
TEST(RoutingRules, EolWithinDistanceCannotBeIgnoredAtDiagonalSeparation) {
  auto tech = technology();
  tech.layers["M2"].rules.spacing.push_back({.018, {}, Eol_spacing{.025, .040, {}, false}});
  const auto a = metal(0, 0, 100, 18);
  EXPECT_FALSE(check_metal_spacing(tech, 1000, a, metal(130, 35, 230, 53)).legal);
  EXPECT_TRUE(check_metal_spacing(tech, 1000, a, metal(140, 35, 240, 53)).legal);
}
TEST(RoutingRules, UnsupportedOrMalformedRulesFailClosed) {
  auto       tech = technology();
  const auto a = metal(0, 0, 100, 18), b = metal(0, 100, 100, 118);
  tech.layers["M2"].rules.properties["LEF58_SPACING"] = {"SPACING .031 ENDOFLINE .025 ;"};
  EXPECT_FALSE(check_metal_rectangle(tech, 1000, a).legal);
  EXPECT_FALSE(check_metal_spacing(tech, 1000, a, b).legal);
  tech = technology();
  tech.layers["M2"].rules.min_step.push_back({.010});
  EXPECT_FALSE(check_metal_rectangle(tech, 1000, a).legal);
  tech = technology();
  tech.layers["M2"].rules.parallel.push_back({{0}, {0, .1}, {{.018}}});
  EXPECT_FALSE(check_metal_spacing(tech, 1000, a, b).legal);
  EXPECT_FALSE(check_metal_rectangle(tech, 0, a).legal);
  EXPECT_FALSE(check_metal_rectangle(tech, 1000, metal(.5, 0, 100, 18)).legal);
  EXPECT_FALSE(check_metal_spacing(tech, 1000, a, {"M3", b.rect}).legal);
}
TEST(RoutingRules, CompiledAsap7ExtensionsRequireExplicitTracksAndWidths) {
  auto  tech      = technology();
  auto& layer     = tech.layers["M2"];
  layer.direction = "HORIZONTAL";
  layer.rules.properties
      = {{"LEF58_SPACING", {"SPACING .018 ENDOFLINE .025 WITHIN .02 ENDTOEND .031 PARALLELEDGE .025 WITHIN .02 ;"}},
         {"LEF58_EOLKEEPOUT", {"EOLKEEPOUT .025 EXTENSION 0 .0125 .031 CORNERONLY ;"}},
         {"LEF58_CORNERSPACING", {"CORNERSPACING CONVEXCORNER WIDTH 0 SPACING .02 ;"}},
         {"LEF58_WIDTHTABLE", {"WIDTHTABLE .018 .09 .162 ;"}},
         {"LEF58_PITCH", {"PITCH .036 FIRSTLASTPITCH .045 ;"}},
         {"LEF58_RIGHTWAYONGRIDONLY", {"RIGHTWAYONGRIDONLY ;"}},
         {"LEF58_RECTONLY", {"RECTONLY ;"}}};
  for (const auto& [name, text] : layer.rules.properties) {
    layer.rules.unsupported.insert("PROPERTY " + name);
  }
  Floorplan fp;
  fp.tracks = {{"Y", "M2", 9, 2, 270}, {"Y", "M2", 45, 2, 270}};
  const Signal_metal_rules compiled(tech, fp);
  EXPECT_TRUE(compiled.rectangle(metal(0, 0, 100, 18)).legal);
  EXPECT_TRUE(compiled.rectangle(metal(0, 36, 100, 54)).legal);
  EXPECT_FALSE(compiled.rectangle(metal(0, 72, 100, 90)).legal);
  EXPECT_FALSE(compiled.rectangle(metal(0, 0, 100, 20)).legal);
  EXPECT_FALSE(compiled.rectangle(metal(0, 0, 18, 100)).legal);
  EXPECT_TRUE(compiled.spacing(metal(0, 0, 100, 18), metal(0, 36, 100, 54)).legal);
  EXPECT_FALSE(compiled.spacing(metal(0, 0, 100, 18), metal(130, 0, 230, 18)).legal);
  EXPECT_TRUE(compiled.spacing(metal(0, 0, 100, 18), metal(131, 0, 231, 18)).legal);
  EXPECT_FALSE(compiled.spacing(metal(0, 0, 100, 18), metal(130, 30, 230, 48)).legal);
  EXPECT_TRUE(compiled.spacing(metal(0, 0, 100, 18), metal(131, 30, 231, 48)).legal);
  EXPECT_FALSE(compiled.spacing(metal(0, 0, 100, 18), metal(119, 37, 219, 55)).legal);
  EXPECT_TRUE(compiled.spacing(metal(0, 0, 100, 18), metal(120, 37, 220, 55)).legal);
  EXPECT_FALSE(compiled.spacing(metal(0, 0, 100, 18), metal(0, 87.5, 100, 105.5)).legal);
  EXPECT_TRUE(compiled.spacing(metal(0, 0, 100, 18), metal(0, 87, 100, 105)).legal);
  // Import retains the original rules and still rejects them without compiling.
  EXPECT_FALSE(check_metal_rectangle(tech, 1000, metal(0, 0, 100, 18)).legal);
  fp.tracks.clear();
  EXPECT_FALSE(Signal_metal_rules(tech, fp).rectangle(metal(0, 0, 100, 18)).legal);
  fp.tracks = {{"Y", "M2", 9, 2, 270, 1}};
  EXPECT_FALSE(Signal_metal_rules(tech, fp).rectangle(metal(0, 0, 100, 18)).legal);
}
TEST(RoutingRules, UnknownLef58SuffixCannotBeSilentlyAccepted) {
  auto        tech  = technology();
  auto&       rules = tech.layers["M2"].rules;
  const Shape shape = metal(0, 0, 100, 18);
  for (const auto& text : {"RECTONLY EXCEPTNONCOREPINS ;", "RECTONLY ; RECTONLY ;", "RECTONLY"}) {
    rules.properties["LEF58_RECTONLY"] = {text};
    EXPECT_FALSE(Signal_metal_rules(tech, {}).rectangle(shape).legal);
  }
  rules.properties = {{"LEF58_WIDTHTABLE", {"WIDTHTABLE .018 .018 ;"}}};
  EXPECT_FALSE(Signal_metal_rules(tech, {}).rectangle(shape).legal);
}
TEST(RoutingRules, WidthCandidatesComeFromValidatedTechnology) {
  auto tech = technology();
  EXPECT_EQ(Signal_metal_rules(tech, {}).candidate_widths("M2"), (std::vector<int64_t>{18}));
  tech.layers["M2"].rules.properties["LEF58_WIDTHTABLE"] = {"WIDTHTABLE .018 .090 .162 ;"};
  EXPECT_EQ(Signal_metal_rules(tech, {}).candidate_widths("M2"), (std::vector<int64_t>{18, 90, 162}));
  EXPECT_TRUE(Signal_metal_rules(tech, {}).candidate_widths("absent").empty());
  tech.layers["M2"].rules.properties["LEF58_WIDTHTABLE"] = {"WIDTHTABLE .018 .018 ;"};
  EXPECT_TRUE(Signal_metal_rules(tech, {}).candidate_widths("M2").empty());
}
TEST(RoutingRules, QueryClearanceIncludesAllTableEntriesAndFailsForUnknownRules) {
  auto tech                        = technology();
  tech.layers["M2"].rules.parallel = {{{0, .036}, {0, .100}, {{.018, .020}, {.024, .032}}}};
  EXPECT_EQ(Signal_metal_rules(tech, {}).clearance("M2"), 32);
  tech.layers["M2"].rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .025 EXTENSION 0 .0125 .031 CORNERONLY ;"};
  EXPECT_EQ(Signal_metal_rules(tech, {}).clearance("M2"), 69);
  tech.layers["M2"].rules.properties["LEF58_NEW_RULE"] = {"UNKNOWN ;"};
  EXPECT_FALSE(Signal_metal_rules(tech, {}).clearance("M2"));
}
TEST(RoutingRules, EolKeepoutUsesBackwardSideForwardOrderAndBothDirections) {
  auto tech                                              = technology();
  tech.layers["M2"].rules.spacing                        = {{.001, {}, {}}};
  tech.layers["M2"].rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .025 EXTENSION .010 .005 .020 CORNERONLY ;"};
  const Signal_metal_rules rules(tech, {});
  const auto               a = metal(0, 0, 100, 18), b = metal(95, 22, 130, 40), c = metal(95, 23, 130, 41);
  EXPECT_EQ(rules.spacing(a, b).reason, "LEF58 EOL keepout");
  EXPECT_EQ(rules.spacing(b, a).reason, "LEF58 EOL keepout");
  EXPECT_TRUE(rules.spacing(a, c).legal);
  // Rotation preserves the geometric rule; the spacing primitive does not
  // presume that an existing obstacle follows the preferred routing direction.
  EXPECT_FALSE(rules.spacing(metal(0, 0, 18, 100), metal(22, 95, 40, 130)).legal);
}
}  // namespace livehd::physical
