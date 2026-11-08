// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "signal_via.hpp"

#include "gtest/gtest.h"
#include "routing_geometry.hpp"

namespace livehd::physical {
namespace {
struct Fixture {
  Technology           tech;
  Floorplan            fp;
  Routing_occupancy    occupancy{64};
  std::array<Shape, 2> landings{
      {{"M2", {981, 991, 1019, 1009}}, {"M3", {991, 981, 1009, 1019}}}
  };
  Fixture() {
    tech.manufacturing_grid = .001;
    tech.routing_layers     = {"M1", "M2", "M3"};
    for (const auto& name : tech.routing_layers) {
      auto& layer         = tech.layers[name];
      layer.width         = .018;
      layer.direction     = name == "M2" ? "HORIZONTAL" : "VERTICAL";
      layer.rules.loaded  = true;
      layer.rules.area    = .000666;
      layer.rules.spacing = {
          {.018, {}, {}}
      };
      layer.rules.properties["LEF58_RIGHTWAYONGRIDONLY"] = {"RIGHTWAYONGRIDONLY ;"};
    }
    tech.cut_layers["V2"].width   = .018;
    tech.cut_layers["V2"].spacing = {.018};
    tech.vias["VIA23"].shapes     = {
        {"M2", {-.014, -.009, .014, .009}},
        {"M3", {-.009, -.014, .009, .014}},
        {"V2", {-.009, -.009, .009, .009}}
    };
    fp.die    = {0, 0, 2000, 2000};
    fp.tracks = {
        {"Y", "M2", 28, 55, 36},
        {"X", "M3", 28, 55, 36}
    };
  }
  Rule_check check() const {
    return check_signal_via(tech, fp, Signal_metal_rules(tech, fp), occupancy, {"VIA23"}, 1000, 1000, landings, "self");
  }
  std::array<Shape, 2> relative_landings() const {
    auto relative = landings;
    for (auto& landing : relative) {
      landing.rect = landing.rect.translated(-1000, -1000);
    }
    return relative;
  }
  Signal_via_intrinsic_check intrinsic() const {
    return check_signal_via_intrinsics(tech, fp.dbu, via_geometry(tech, fp, {"VIA23"}), relative_landings());
  }
};
}  // namespace
TEST(SignalVia, RequiresLegalCompleteLandings) {
  Fixture f;
  ASSERT_TRUE(f.check().legal) << f.check().reason;
  f.landings[0].rect = {986, 991, 1014, 1009};
  EXPECT_TRUE(f.intrinsic().check.legal);  // Enclosure alone cannot certify metal minimum area.
  EXPECT_FALSE(f.check().legal);           // Bare LEF pad is below min area.
  f.landings[0].rect = {982, 992, 1020, 1010};
  EXPECT_FALSE(f.check().legal);  // Shifted metal no longer covers the via.
}
TEST(SignalVia, ChecksBothMetalsAndSameNetCuts) {
  for (const auto& obstacle : std::vector<Shape>{
           {"M2", {1020, 991, 1050, 1009}},
           {"M3", {991, 1020, 1009, 1050}},
           {"V2", {1010, 991, 1028, 1009}}
  }) {
    Fixture f;
    f.occupancy.insert({obstacle, obstacle.layer == "V2" ? "self" : "other"});
    EXPECT_FALSE(f.check().legal) << obstacle.layer;
  }
  Fixture f;
  f.occupancy.insert({
      {"V2", {1027, 991, 1045, 1009}},
      "other"
  });  // Exactly 18 DBU cut gap.
  EXPECT_TRUE(f.check().legal) << f.check().reason;
}
TEST(SignalVia, ExactOwnerContourRemovesOnlyCoveredLandingEol) {
  Fixture f;
  f.tech.layers["M3"].rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .01825 EXTENSION 0 0 .031 ;"};
  f.landings[1].rect                                       = {991, 891, 1009, 1109};
  const Shape cap{
      "M3",
      {959, 1091, 1009, 1109}
  };
  const Shape rail{
      "M3",
      {900, 1127, 1200, 1145}
  };
  f.occupancy.insert({rail, "VDD"});
  const std::vector<Shape> owner{f.landings[0], f.landings[1], cap};
  const auto               check = [&](std::span<const Shape> context) {
    return check_signal_via(f.tech,
                            f.fp,
                            Signal_metal_rules(f.tech, f.fp),
                            f.occupancy,
                            {"VIA23"},
                            1000,
                            1000,
                            f.landings,
                            "self",
                            context);
  };
  ASSERT_EQ(f.check().reason, "LEF58 EOL keepout");
  EXPECT_TRUE(check(owner).legal) << check(owner).reason;
  EXPECT_FALSE(check(f.landings).legal);  // Exposed source end, no admitted cap.
  auto uncovered       = owner;
  uncovered[1].rect.yl = 900;
  EXPECT_EQ(check(uncovered).reason, "invalid native metal union context");
  f.occupancy.insert({
      {{"M3"}, {991, 1080, 1009, 1100}},
      "foreign"
  });
  EXPECT_FALSE(check(owner).legal);  // The context cannot waive a real short.
  f.occupancy = Routing_occupancy(64);
  f.occupancy.insert({rail, "VDD"});
  f.occupancy.insert({
      {{"V2"}, {991, 1010, 1009, 1028}},
      "self"
  });
  EXPECT_EQ(check(owner).reason, "signal cut spacing");
}
TEST(SignalVia, ExplicitOwnerContextRequiresCoverageEvenWithoutNearbyMetal) {
  Fixture f;
  ASSERT_EQ(f.occupancy.size(), 0u);
  const auto check = [&](std::span<const Shape> context) {
    return check_signal_via(f.tech,
                            f.fp,
                            Signal_metal_rules(f.tech, f.fp),
                            f.occupancy,
                            {"VIA23"},
                            1000,
                            1000,
                            f.landings,
                            "self",
                            context);
  };
  EXPECT_TRUE(check({}).legal);  // No explicit context preserves the default.
  EXPECT_TRUE(check(f.landings).legal);
  auto uncovered        = f.landings;
  uncovered[1].rect.yl += 1;
  EXPECT_FALSE(check(uncovered).legal);
  auto fractional        = f.landings;
  fractional[1].rect.yl -= .5;
  EXPECT_FALSE(check(fractional).legal);
  EXPECT_FALSE(check(std::span<const Shape>(f.landings.data(), 1)).legal);
  std::vector<Shape> split{f.landings[0], f.landings[1], f.landings[1]};
  split[1].rect.yh = split[2].rect.yl = 1000;
  EXPECT_TRUE(check(split).legal);  // Exact union coverage, no hull shortcut.
  split[2].rect.yl = 1001;
  EXPECT_FALSE(check(split).legal);
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
  cut.unsupported             = {"PROPERTY LEF58_CUTCLASS", "PROPERTY LEF58_SPACINGTABLE", "PROPERTY LEF58_ENCLOSURE"};
  f.tech.vias["VIA23"].shapes = {
      {"M2", {-.009, -.017, .009, .017}},
      {"M3", {-.020, -.012, .020, .012}},
      {"V2", {-.009, -.012, .009, .012}}
  };
  f.tech.layers["M2"].direction = "VERTICAL";
  f.tech.layers["M3"].direction = "HORIZONTAL";
  f.tech.layers["M3"].width     = .024;
  f.fp.tracks                   = {
      {"X", "M2", 28, 55, 36},
      {"Y", "M3", 28, 55, 36}
  };
  f.landings = {
      {{"M2", {991, 981, 1009, 1019}}, {"M3", {958, 988, 1042, 1012}}}
  };
  ASSERT_TRUE(f.check().legal) << f.check().reason;
  const auto intrinsic = f.intrinsic();
  ASSERT_TRUE(intrinsic.check.legal) << intrinsic.check.reason;
  EXPECT_EQ(intrinsic.cut_index, 2);
  EXPECT_EQ(intrinsic.cut_spacing, 34);
  auto reversed = f.relative_landings();
  std::swap(reversed[0], reversed[1]);
  EXPECT_TRUE(check_signal_via_intrinsics(f.tech, f.fp.dbu, via_geometry(f.tech, f.fp, {"VIA23"}), reversed).check.legal);
  // Rotating a rectangular cut changes its class dimensions. The intrinsic
  // result must agree with the full checker, even though both landings rotate.
  auto rotated = f.relative_landings();
  for (auto& landing : rotated) {
    landing.rect = orient_rect(landing.rect, 0, 0, Orientation::W);
  }
  const Route_via rotated_via{"VIA23", Orientation::W};
  const auto rotated_intrinsic = check_signal_via_intrinsics(f.tech, f.fp.dbu, via_geometry(f.tech, f.fp, rotated_via), rotated);
  EXPECT_FALSE(rotated_intrinsic.check.legal);
  EXPECT_EQ(rotated_intrinsic.check.reason, "signal via does not match a single-cut class");
  for (auto& landing : rotated) {
    landing.rect = landing.rect.translated(1000, 1000);
  }
  const auto rotated_full
      = check_signal_via(f.tech, f.fp, Signal_metal_rules(f.tech, f.fp), f.occupancy, rotated_via, 1000, 1000, rotated, "self");
  EXPECT_EQ(rotated_full.legal, rotated_intrinsic.check.legal);
  EXPECT_EQ(rotated_full.reason, rotated_intrinsic.check.reason);
  // A table-only cut layer must not accidentally use zero cut spacing.
  f.occupancy.insert({
      {"V2", {1042, 988, 1060, 1012}},
      "self"
  });
  EXPECT_FALSE(f.check().legal);
  f.occupancy = Routing_occupancy(64);
  f.occupancy.insert({
      {"V2", {1043, 988, 1061, 1012}},
      "other"
  });
  EXPECT_TRUE(f.check().legal) << f.check().reason;
  cut.properties["LEF58_ENCLOSURE"] = {"ENCLOSURE CUTCLASS rect ABOVE EOL 0.025 0.040 0.0 ;"};
  EXPECT_EQ(f.intrinsic().check.reason, "signal cut enclosure");
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
  EXPECT_FALSE(f.intrinsic().check.legal);
  EXPECT_FALSE(f.check().legal);
}
TEST(SignalVia, IntrinsicWidthPairsRespectStrictEolThresholdAcrossTranslations) {
  Fixture f;
  auto&   cut                      = f.tech.cut_layers["V2"];
  cut.properties["LEF58_CUTCLASS"] = {"CUTCLASS square WIDTH 0.018 ;"};
  const auto shapes                = via_geometry(f.tech, f.fp, {"VIA23"});
  for (const auto threshold : {"0.018", "0.01825", "0.020", "0.02025"}) {
    cut.properties["LEF58_ENCLOSURE"] = {std::string("ENCLOSURE CUTCLASS square EOL ") + threshold + " 0.005 0.002 ;"};
    for (const double width : {18, 20, 22, 24}) {
      const std::array<Shape, 2> relative{
          {{"M2", {-19, -width / 2, 19, width / 2}}, {"M3", {-width / 2, -19, width / 2, 19}}}
      };
      const auto intrinsic = check_signal_via_intrinsics(f.tech, f.fp.dbu, shapes, relative);
      // At/above the strict EOL threshold the side enclosure is inapplicable;
      // otherwise at least 2 DBU of side metal must surround the 18 DBU cut.
      const bool expected  = width >= std::stod(threshold) * f.fp.dbu || width >= 22;
      EXPECT_EQ(intrinsic.check.legal, expected) << threshold << " width=" << width;
      for (const int delta : {-72, -36, 0, 36, 72}) {
        const int x = 1000 + delta, y = 1000 - delta;
        auto      placed = relative;
        for (auto& landing : placed) {
          landing.rect = landing.rect.translated(x, y);
        }
        const auto full
            = check_signal_via(f.tech, f.fp, Signal_metal_rules(f.tech, f.fp), f.occupancy, {"VIA23"}, x, y, placed, "self");
        if (full.legal) {
          EXPECT_TRUE(intrinsic.check.legal) << "intrinsic pruning must never discard a full-check accepted site";
        }
        EXPECT_EQ(full.legal, expected) << threshold << " width=" << width << " delta=" << delta << " " << full.reason;
        EXPECT_EQ(full.reason, intrinsic.check.reason);
      }
    }
  }
}
TEST(SignalVia, IntrinsicSuccessStillRequiresEverySiteCheck) {
  const Fixture reference;
  const auto    shapes                = via_geometry(reference.tech, reference.fp, {"VIA23"});
  const auto    relative              = reference.relative_landings();
  const auto    expect_site_rejection = [&](const Fixture& f, const std::string& reason) {
    ASSERT_TRUE(check_signal_via_intrinsics(f.tech, f.fp.dbu, shapes, relative).check.legal);
    EXPECT_FALSE(f.check().legal);
    EXPECT_EQ(f.check().reason, reason);
  };
  Fixture off_track;
  off_track.fp.tracks.front().start += 1;
  expect_site_rejection(off_track, "not on an explicit preferred-direction track");
  Fixture off_grid;
  off_grid.tech.manufacturing_grid = .002;
  expect_site_rejection(off_grid, "off-grid via geometry");
  auto shifted = off_grid.landings;
  for (auto& landing : shifted) {
    landing.rect = landing.rect.translated(1, 1);
  }
  EXPECT_EQ(check_signal_via(off_grid.tech,
                             off_grid.fp,
                             Signal_metal_rules(off_grid.tech, off_grid.fp),
                             off_grid.occupancy,
                             {"VIA23"},
                             1001,
                             1001,
                             shifted,
                             "self")
                .reason,
            "off-grid via origin");
  Fixture outside_via;
  outside_via.fp.die.xh = 1010;
  expect_site_rejection(outside_via, "via outside die");
  Fixture outside_landing;
  outside_landing.fp.die.xh = 1015;
  expect_site_rejection(outside_landing, "landing outside die");
  Fixture shadow;
  shadow.fp.vias.push_back({"VIA23", {}, {}});
  expect_site_rejection(shadow, "DEF overrides LEF signal via");
  Fixture same_net_cut;
  same_net_cut.occupancy.insert({
      {"V2", {1010, 991, 1028, 1009}},
      "self"
  });
  expect_site_rejection(same_net_cut, "signal cut spacing");
  Fixture pg;
  pg.occupancy.insert({
      {"M2", {1000, 991, 1040, 1009}},
      "VDD"
  });
  expect_site_rejection(pg, "different-net metal touches or overlaps");
  Fixture eol;
  eol.tech.layers["M2"].rules.properties["LEF58_SPACING"]
      = {"SPACING .018 ENDOFLINE .025 WITHIN .02 ENDTOEND .031 PARALLELEDGE .025 WITHIN .02 ;"};
  eol.occupancy.insert({
      {"M2", {1049, 991, 1087, 1009}},
      "VDD"
  });  // 30 DBU gap passes ordinary 18 DBU spacing.
  ASSERT_TRUE(check_signal_via_intrinsics(eol.tech, eol.fp.dbu, shapes, relative).check.legal);
  EXPECT_FALSE(eol.check().legal);
  EXPECT_EQ(eol.check().reason, "LEF58 facing EOL spacing");
}
TEST(SignalVia, FullCheckerPreservesCompetingFailureOrder) {
  Fixture f;
  // Failure on the first metal precedes the second metal's die violation.
  f.landings[0].rect = {987, 991, 1013, 1009};
  f.fp.die.yh        = 1010;
  EXPECT_EQ(f.check().reason, "via metal not enclosed by route landing");
  // Its own position is checked before containment.
  f.fp.die.xh = 1010;
  EXPECT_EQ(f.check().reason, "via outside die");
  // Shadowing is rejected before any shape or occupancy inspection.
  f.fp.vias.push_back({"VIA23", {}, {}});
  EXPECT_EQ(f.check().reason, "DEF overrides LEF signal via");
  f.landings[1].layer = f.landings[0].layer;
  EXPECT_EQ(f.check().reason, "via needs two distinct landing layers");
}
}  // namespace livehd::physical
