#include "power_grid_vias.hpp"

#include "gtest/gtest.h"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/cut_rules.hpp"

namespace livehd::placement {
namespace {
namespace ph = physical;
ph::Technology technology() {
  ph::Technology t;
  for (int i = 1; i <= 6; ++i) {
    t.layers["M" + std::to_string(i)].direction = (i == 1 || i == 3 || i == 5) ? "VERTICAL" : "HORIZONTAL";
  }
  t.cut_layers["V1"].spacing       = {.018};
  t.cut_layers["V2"].spacing       = {.018};
  t.via_rules["M2_M1"]             = {true,
                                      true,
                                      {{"M1", "", std::array{0., 0.}},
                                       {"M2", "", std::array{.002, 0.}},
                                       {"V1", "", {}, {}, std::array{.036, .036}, ph::Rect{-.009, -.009, .009, .009}}}};
  t.via_rules["M6_M5widePWR1p152"] = {true,
                                      false,
                                      {{"M5", "", std::array{0., 0.}},
                                       {"M6", "", std::array{0., 0.}, std::array{.288, .288}},
                                       {"V5", "", {}, {}, std::array{.096, .382}, ph::Rect{-.012, -.144, .012, .144}}}};
  t.vias["VIA23"].shapes
      = {{"M2", {-.014, -.009, .014, .009}}, {"M3", {-.009, -.014, .009, .014}}, {"V2", {-.009, -.009, .009, .009}}};
  t.vias["VIA34"].shapes
      = {{"M3", {-.009, -.017, .009, .017}}, {"M4", {-.020, -.012, .020, .012}}, {"V3", {-.009, -.012, .009, .012}}};
  t.vias["VIA45"].shapes
      = {{"M4", {-.023, -.012, .023, .012}}, {"M5", {-.012, -.023, .012, .023}}, {"V4", {-.012, -.012, .012, .012}}};
  for (const auto& [layer, width, height] :
       std::vector<std::tuple<std::string, std::string, std::string>>{{"V3", ".018", ".024"},
                                                                      {"V4", ".024", ".024"},
                                                                      {"V5", ".024", ".288"}}) {
    auto& c                            = t.cut_layers[layer];
    c.properties["LEF58_CUTCLASS"]     = {"CUTCLASS cut WIDTH " + width + " LENGTH " + height + " ;"};
    c.properties["LEF58_SPACINGTABLE"] = {"SPACINGTABLE DEFAULT .034 CUTCLASS cut cut - - ;"};
    c.unsupported                      = {"PROPERTY LEF58_CUTCLASS", "PROPERTY LEF58_SPACINGTABLE"};
  }
  t.cut_layers["V3"].properties["LEF58_ENCLOSURE"]
      = {"ENCLOSURE CUTCLASS cut BELOW EOL 0.0 .005 0 ; ENCLOSURE CUTCLASS cut ABOVE EOL .02425 .011 0 ;"};
  return t;
}
std::vector<ph::Shape> geometry(const ph::Technology& tech, const Pg_via_array& array) {
  ph::Floorplan fp;
  fp.vias.push_back(array.via);
  return ph::via_geometry(tech, fp, {array.via.name});
}
}  // namespace
TEST(PowerGridVias, GeneratedRailArrayAndWideStripeViaUseLefRules) {
  const auto t = technology();
  auto       a = make_pg_via(t, 1000, {"M1", "M2", {12852, 18}, {12852, 18}});
  EXPECT_EQ(a.rows, 1);
  EXPECT_EQ(a.columns, 357);
  ASSERT_TRUE(a.via.generated);
  EXPECT_EQ(a.via.generated->enclosure, (std::array<int64_t, 4>{0, 0, 2, 0}));
  EXPECT_EQ(geometry(t, a).size(), 359);
  a = make_pg_via(t, 1000, {"M5", "M6", {120, 12960}, {12852, 288}});
  EXPECT_EQ(a.rows, 1);
  EXPECT_EQ(a.columns, 2);
  ASSERT_TRUE(a.via.generated);
  EXPECT_EQ(a.via.generated->cut_spacing, (std::array<int64_t, 2>{34, 34}));
  EXPECT_EQ(a.via.generated->enclosure, (std::array<int64_t, 4>{19, 0, 0, 0}));
}
TEST(PowerGridVias, FixedStackArraysRespectApplicableAsap7Enclosures) {
  const auto t = technology();
  auto       a = make_pg_via(t, 1000, {"M2", "M3", {12852, 18}, {120, 34}, false, true});
  EXPECT_FALSE(a.via.generated);
  EXPECT_EQ(a.rows, 1);
  EXPECT_EQ(a.columns, 3);
  auto shapes = geometry(t, a);
  EXPECT_EQ(shapes[0], (ph::Shape{"M2", {-50, -9, 50, 9}}));
  EXPECT_EQ(shapes[1], (ph::Shape{"M3", {-45, -14, 45, 14}}));
  a = make_pg_via(t, 1000, {"M3", "M4", {120, 34}, {120, 24}, true, true});
  EXPECT_EQ(a.columns, 2);
  shapes = geometry(t, a);
  // PDNGEN conservatively applies EOL enclosure even at threshold zero.
  // Preserve its PG geometry without changing signal-rule applicability.
  EXPECT_EQ(shapes[0], (ph::Shape{"M3", {-40, -17, 40, 17}}));
  EXPECT_EQ(shapes[1], (ph::Shape{"M4", {-46, -12, 46, 12}}));
  a = make_pg_via(t, 1000, {"M4", "M5", {120, 24}, {120, 12960}, true, false});
  EXPECT_EQ(a.columns, 2);
  shapes = geometry(t, a);
  EXPECT_EQ(shapes[0], (ph::Shape{"M4", {-52, -12, 52, 12}}));
  EXPECT_EQ(shapes[1], (ph::Shape{"M5", {-60, -23, 60, 23}}));
}
TEST(PowerGridVias, PdnEnclosurePolicyDoesNotChangeSignalRules) {
  const auto t = technology();
  const auto& cut = t.cut_layers.at("V3");
  const auto classes = ph::cut_classes(cut, 1000);
  EXPECT_EQ(ph::cut_enclosures(cut, 1000, classes, {18, 24}, false, t.layers.at("M3"), {120, 34}),
            (std::vector<std::array<int64_t, 2>>{{0, 0}}));
  EXPECT_EQ(ph::cut_enclosures(cut, 1000, classes, {18, 24}, false, t.layers.at("M3"), {120, 34},
                              ph::Cut_enclosure_policy::pdngen),
            (std::vector<std::array<int64_t, 2>>{{5, 0}}));
}
TEST(PowerGridVias, FixedMacroContactKeepsMinimumEnclosureAndFitsCrossing) {
  const auto t = technology();
  auto       a = make_pg_via(t, 1000, {"M4", "M5", {58320, 96}, {120, 10000}, false, false, true, false});
  EXPECT_EQ(a.columns, 2);
  EXPECT_EQ(a.rows, 1);
  const auto shapes = geometry(t, a);
  EXPECT_EQ(shapes[0], (ph::Shape{"M4", {-52, -12, 52, 12}}));
  EXPECT_EQ(shapes[1], (ph::Shape{"M5", {-60, -23, 60, 23}}));
  // A narrow contact at the edge of a pin must not inherit the full pin extent.
  EXPECT_THROW(make_pg_via(t, 1000, {"M4", "M5", {40, 96}, {120, 10000}, false, false, true, false}), std::invalid_argument);
}
TEST(PowerGridVias, RejectsUnsupportedTableAndUnbuildableRequests) {
  auto t = technology();
  EXPECT_THROW(make_pg_via(t, 1000, {"M1", "M2", {1, 1}, {1, 1}}), std::invalid_argument);
  EXPECT_THROW(make_pg_via(t, 1000, {"M1", "M6", {100, 100}, {100, 100}}), std::invalid_argument);
  t.cut_layers["V4"].properties["LEF58_SPACINGTABLE"] = {"SPACINGTABLE DEFAULT .034 CUTCLASS cut cut .04 - ;"};
  EXPECT_THROW(make_pg_via(t, 1000, {"M4", "M5", {120, 24}, {120, 12960}, true, false}), std::invalid_argument);
}
}  // namespace livehd::placement
