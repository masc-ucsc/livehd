// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "signal_via.hpp"

#include "gtest/gtest.h"

namespace livehd::physical {
namespace {
struct Fixture {
  Technology           tech;
  Floorplan            fp;
  Routing_occupancy    occupancy{64};
  std::array<Shape, 2> landings{{{"M2", {981, 991, 1019, 1009}}, {"M3", {991, 981, 1009, 1019}}}};
  Fixture() {
    tech.manufacturing_grid = .001;
    tech.routing_layers     = {"M1", "M2", "M3"};
    for (const auto& name : tech.routing_layers) {
      auto& layer                                        = tech.layers[name];
      layer.width                                        = .018;
      layer.direction                                    = name == "M2" ? "HORIZONTAL" : "VERTICAL";
      layer.rules.loaded                                 = true;
      layer.rules.area                                   = .000666;
      layer.rules.spacing                                = {{.018, {}, {}}};
      layer.rules.properties["LEF58_RIGHTWAYONGRIDONLY"] = {"RIGHTWAYONGRIDONLY ;"};
    }
    tech.cut_layers["V2"].width   = .018;
    tech.cut_layers["V2"].spacing = {.018};
    tech.vias["VIA23"].shapes
        = {{"M2", {-.014, -.009, .014, .009}}, {"M3", {-.009, -.014, .009, .014}}, {"V2", {-.009, -.009, .009, .009}}};
    fp.die    = {0, 0, 2000, 2000};
    fp.tracks = {{"Y", "M2", 28, 55, 36}, {"X", "M3", 28, 55, 36}};
  }
  Rule_check check() const {
    return check_signal_via(tech, fp, Signal_metal_rules(tech, fp), occupancy, {"VIA23"}, 1000, 1000, landings, "self");
  }
};
}  // namespace
TEST(SignalVia, RequiresLegalCompleteLandings) {
  Fixture f;
  ASSERT_TRUE(f.check().legal) << f.check().reason;
  f.landings[0].rect = {986, 991, 1014, 1009};
  EXPECT_FALSE(f.check().legal);  // Bare LEF pad is below min area.
  f.landings[0].rect = {982, 992, 1020, 1010};
  EXPECT_FALSE(f.check().legal);  // Shifted metal no longer covers the via.
}
TEST(SignalVia, ChecksBothMetalsAndSameNetCuts) {
  for (const auto& obstacle :
       std::vector<Shape>{{"M2", {1020, 991, 1050, 1009}}, {"M3", {991, 1020, 1009, 1050}}, {"V2", {1010, 991, 1028, 1009}}}) {
    Fixture f;
    f.occupancy.insert({obstacle, obstacle.layer == "V2" ? "self" : "other"});
    EXPECT_FALSE(f.check().legal) << obstacle.layer;
  }
  Fixture f;
  f.occupancy.insert({{"V2", {1027, 991, 1045, 1009}}, "other"});  // Exactly 18 DBU cut gap.
  EXPECT_TRUE(f.check().legal) << f.check().reason;
}
TEST(SignalVia, NewCutSetKeepsSpacingBetweenProposalCuts) {
  Fixture        f;
  Signal_cut_set cuts(f.tech, f.fp);
  EXPECT_TRUE(cuts.add({"VIA23"}, 1000, 1000));
  EXPECT_TRUE(cuts.add({"VIA23"}, 1036, 1000));   // Exactly 18 DBU cut gap.
  EXPECT_FALSE(cuts.add({"VIA23"}, 1000, 1030));  // 12 DBU gap, even on one net.
}
TEST(SignalVia, RejectsUnsupportedCutRulesAndShadowing) {
  Fixture f;
  f.tech.cut_layers["V2"].properties["LEF58_SPACING"] = {"SPACING 0.018 ;"};
  EXPECT_FALSE(f.check().legal);
  f.tech.cut_layers["V2"].properties.clear();
  f.fp.vias.push_back({"VIA23", {}, {}});
  EXPECT_FALSE(f.check().legal);
}
TEST(SignalVia, RejectsBadTemplatesTracksAndDie) {
  Fixture f;
  f.tech.vias["VIA23"].shapes.back().rect = {-.010, -.009, .010, .009};
  EXPECT_FALSE(f.check().legal);
  f.tech.vias["VIA23"].shapes.back().rect = {-.009, -.009, .009, .009};
  f.fp.tracks.pop_back();
  EXPECT_FALSE(f.check().legal);
  f.fp.tracks.push_back({"X", "M3", 28, 55, 36});
  f.fp.die.xh = 1010;
  EXPECT_FALSE(f.check().legal);
}
TEST(SignalVia, RectangularCutClassSpacingAndEnclosureAreEnforced) {
  Fixture f;
  auto&   cut = f.tech.cut_layers["V2"];
  cut.width   = 0;
  cut.spacing.clear();
  cut.properties["LEF58_CUTCLASS"]     = {"CUTCLASS rect WIDTH 0.018 LENGTH 0.024 CUTS 1 ;"};
  cut.properties["LEF58_SPACINGTABLE"] = {"SPACINGTABLE DEFAULT 0.034 CUTCLASS rect rect - - ;"};
  cut.properties["LEF58_ENCLOSURE"]
      = {"ENCLOSURE CUTCLASS rect BELOW EOL 0.019 0.005 0.0 ; ENCLOSURE CUTCLASS rect ABOVE EOL 0.025 0.011 0.0 ;"};
  cut.unsupported = {"PROPERTY LEF58_CUTCLASS", "PROPERTY LEF58_SPACINGTABLE", "PROPERTY LEF58_ENCLOSURE"};
  f.tech.vias["VIA23"].shapes
      = {{"M2", {-.009, -.017, .009, .017}}, {"M3", {-.020, -.012, .020, .012}}, {"V2", {-.009, -.012, .009, .012}}};
  f.tech.layers["M2"].direction = "VERTICAL";
  f.tech.layers["M3"].direction = "HORIZONTAL";
  f.tech.layers["M3"].width     = .024;
  f.fp.tracks                   = {{"X", "M2", 28, 55, 36}, {"Y", "M3", 28, 55, 36}};
  f.landings                    = {{{"M2", {991, 981, 1009, 1019}}, {"M3", {958, 988, 1042, 1012}}}};
  ASSERT_TRUE(f.check().legal) << f.check().reason;
  // A table-only cut layer must not accidentally use zero cut spacing.
  f.occupancy.insert({{"V2", {1042, 988, 1060, 1012}}, "self"});
  EXPECT_FALSE(f.check().legal);
  f.occupancy = Routing_occupancy(64);
  f.occupancy.insert({{"V2", {1043, 988, 1061, 1012}}, "other"});
  EXPECT_TRUE(f.check().legal) << f.check().reason;
  cut.properties["LEF58_ENCLOSURE"] = {"ENCLOSURE CUTCLASS rect ABOVE EOL 0.025 0.040 0.0 ;"};
  EXPECT_FALSE(f.check().legal);
  cut.properties["LEF58_ENCLOSURE"].clear();
  cut.properties["LEF58_SPACINGTABLE"] = {"SPACINGTABLE DEFAULT 0.034 CUTCLASS rect rect 0.01 - ;"};
  EXPECT_FALSE(f.check().legal);
  cut.properties["LEF58_SPACINGTABLE"] = {"SPACINGTABLE DEFAULT 0.034 CUTCLASS rect rect - - ;"};
  cut.properties["LEF58_CUTCLASS"]     = {"CUTCLASS rect WIDTH 0.018 LENGTH 0.024 CUTS 4 ;"};
  EXPECT_FALSE(f.check().legal);
}
TEST(SignalVia, EolEnclosureUsesStrictWidthThreshold) {
  Fixture f;
  auto&   cut                       = f.tech.cut_layers["V2"];
  cut.properties["LEF58_CUTCLASS"]  = {"CUTCLASS square WIDTH 0.018 ;"};
  // Both 18 nm-wide landings have zero side enclosure. Requiring 1 nm
  // rejects them, but only when their width is strictly below the threshold.
  cut.properties["LEF58_ENCLOSURE"] = {"ENCLOSURE CUTCLASS square EOL 0.01825 0.005 0.001 ;"};
  EXPECT_FALSE(f.check().legal);
  cut.properties["LEF58_ENCLOSURE"] = {"ENCLOSURE CUTCLASS square EOL 0.018 0.005 0.001 ;"};
  EXPECT_TRUE(f.check().legal) << f.check().reason;
  cut.properties["LEF58_ENCLOSURE"] = {"ENCLOSURE CUTCLASS square EOL 0.01775 0.005 0.001 ;"};
  EXPECT_TRUE(f.check().legal) << f.check().reason;
  // An inapplicable EOL alternative must not suppress an ordinary rule.
  cut.properties["LEF58_ENCLOSURE"].push_back("ENCLOSURE CUTCLASS square 0.005 0.001 ;");
  EXPECT_FALSE(f.check().legal);
  // Parse and reject unsupported syntax even when the width would not match.
  cut.properties["LEF58_ENCLOSURE"] = {"ENCLOSURE CUTCLASS square EOL 0.001 0.005 0.001 EOLONLY ;"};
  EXPECT_FALSE(f.check().legal);
}
TEST(SignalVia, RejectsUnsupportedEnclosureOnOtherCutClass) {
  Fixture f;
  auto&   cut                       = f.tech.cut_layers["V2"];
  cut.properties["LEF58_CUTCLASS"]  = {"CUTCLASS square WIDTH 0.018 ; CUTCLASS other WIDTH 0.020 ;"};
  cut.properties["LEF58_ENCLOSURE"] = {"ENCLOSURE CUTCLASS other 0.005 0.001 ;"};
  ASSERT_TRUE(f.check().legal) << f.check().reason;
  cut.properties["LEF58_ENCLOSURE"] = {"ENCLOSURE CUTCLASS other 0.005 0.001 EXTRA ;"};
  EXPECT_FALSE(f.check().legal);
}
}  // namespace livehd::physical
