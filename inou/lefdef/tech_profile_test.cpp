// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tech_profile.hpp"

#include <algorithm>
#include <set>

#include "gtest/gtest.h"

namespace livehd::physical {
TEST(TechProfile, ExplicitNamesAndStrategyAliasesSelectOneEntry) {
  const Technology empty;
  ASSERT_FALSE(tech_profiles().empty());
  for (const auto& profile : tech_profiles()) {
    EXPECT_EQ(&tech_profile(profile.name), &profile);
    EXPECT_EQ(&tech_profile(profile.pg.name), &profile);
    EXPECT_EQ(&resolve_tech_profile(empty, profile.name, Tech_policy::power_grid), &profile);
    if (profile.provides(Tech_policy::route)) {
      EXPECT_EQ(&resolve_tech_profile(empty, profile.name), &profile);
    } else {
      EXPECT_THROW(resolve_tech_profile(empty, profile.name), std::invalid_argument);
    }
  }
  EXPECT_THROW(tech_profile("missing"), std::invalid_argument);
  EXPECT_THROW(tech_profile("auto"), std::invalid_argument);
  EXPECT_THROW(resolve_tech_profile(empty, "missing"), std::invalid_argument);
}
TEST(TechProfile, AutoDetectionRequiresAProfileSiteOrMacro) {
  Technology tech;
  EXPECT_EQ(detect_tech_profile(tech), nullptr);
  EXPECT_THROW(resolve_tech_profile(tech), std::invalid_argument);
  tech.sites["unithd"];
  tech.macros["INVx1"];
  EXPECT_THROW(resolve_tech_profile(tech), std::invalid_argument);
  for (const auto& profile : tech_profiles()) {
    const bool routes = profile.provides(Tech_policy::route);
    for (const auto& site : profile.detect_sites) {
      auto with_site = tech;
      with_site.sites[site];
      EXPECT_EQ(&resolve_tech_profile(with_site, "auto", Tech_policy::power_grid), &profile) << site;
      EXPECT_EQ(detect_tech_profile(with_site, Tech_policy::power_grid), &profile) << site;
      // A routing consumer never sees a profile without a routing policy.
      EXPECT_EQ(detect_tech_profile(with_site), routes ? &profile : nullptr) << site;
    }
    for (const auto& macro : profile.detect_macros) {
      auto with_macro = tech;
      with_macro.macros[macro];
      EXPECT_EQ(&resolve_tech_profile(with_macro, "auto", Tech_policy::power_grid), &profile) << macro;
      EXPECT_EQ(detect_tech_profile(with_macro), routes ? &profile : nullptr) << macro;
    }
  }
}
TEST(TechProfile, EveryRegistryEntryIsSelfConsistent) {
  std::set<std::string> names;
  for (const auto& p : tech_profiles()) {
    SCOPED_TRACE(p.name);
    EXPECT_FALSE(p.name.empty() || p.name == "auto");
    EXPECT_TRUE(names.insert(p.name).second);
    EXPECT_TRUE(p.pg.name == p.name || names.insert(p.pg.name).second);
    EXPECT_FALSE(p.detect_sites.empty() && p.detect_macros.empty());
    const auto& r = p.route;
    if (p.provides(Tech_policy::route)) {
      EXPECT_FALSE(r.access.pin.empty() || r.access.escape.empty() || r.access.cut.empty());
      EXPECT_NE(r.access.pin, r.access.escape);
      EXPECT_FALSE(r.branch_layer.empty() || r.branch_cut.empty());
      EXPECT_EQ(std::set<std::string>(r.signal_layers.begin(), r.signal_layers.end()).size(), r.signal_layers.size());
      EXPECT_TRUE(r.allows(r.access.escape));
      EXPECT_TRUE(r.allows(r.branch_layer));
      EXPECT_FALSE(r.allows(r.access.pin));
    } else {
      EXPECT_TRUE(r.access.escape.empty() && r.branch_layer.empty() && r.signal_layers.empty());
    }
    EXPECT_FALSE(p.io_pin_layers[0].empty() || p.io_pin_layers[1].empty());
    EXPECT_NE(p.io_pin_layers[0], p.io_pin_layers[1]);
    EXPECT_FALSE(p.tap_master.empty());
    EXPECT_GT(p.tap_distance, 0);
    EXPECT_GE(p.tap_row_halo, 0);
    EXPECT_FALSE(p.hold_buffer.empty());
    const auto& pg = p.pg;
    EXPECT_FALSE(pg.name.empty() || pg.reference_revision.empty() || pg.reference_file.empty() || pg.pin_layer.empty());
    EXPECT_FALSE(pg.followpin_layers.empty());
    for (const auto& stripe : pg.stripes) {
      EXPECT_FALSE(stripe.layer.empty());
      EXPECT_GT(stripe.width, 0);
      EXPECT_GT(stripe.pitch, 0);
    }
    for (const auto& [lower, upper] : pg.connect) {
      EXPECT_FALSE(lower.empty() || upper.empty());
      EXPECT_NE(lower, upper);
    }
    // Every orientation has exactly one macro grid.
    for (auto o : {Orientation::N,
                   Orientation::S,
                   Orientation::FN,
                   Orientation::FS,
                   Orientation::E,
                   Orientation::W,
                   Orientation::FE,
                   Orientation::FW}) {
      unsigned matches = 0;
      for (const auto& grid : pg.macro_grids) {
        matches += grid.orientations.empty()
                   || std::find(grid.orientations.begin(), grid.orientations.end(), o) != grid.orientations.end();
      }
      EXPECT_EQ(matches, 1u);
      EXPECT_NE(pg.macro_grid(o), nullptr);
    }
    for (const auto& grid : pg.macro_grids) {
      EXPECT_GE(grid.halo, 0);
      EXPECT_FALSE(grid.connect.empty());
      for (const auto& [lower, upper] : grid.connect) {
        EXPECT_FALSE(lower.empty() || upper.empty());
        EXPECT_NE(lower, upper);
      }
      for (const auto& stripe : grid.stripes) {
        EXPECT_GT(stripe.width, 0);
        EXPECT_GE(stripe.pitch, 2 * stripe.width + stripe.spacing);
      }
    }
    EXPECT_TRUE(pg.rings.empty());
    EXPECT_NE(pg.nets[0], pg.nets[1]);
    for (size_t i = 0; i < pg.nets.size(); ++i) {
      EXPECT_FALSE(pg.nets[i].empty());
      EXPECT_FALSE(pg.pin_names[i].empty());
    }
    EXPECT_TRUE(pg.unsupported.empty());
  }
}
// The ASAP7 strategy keeps the transcription of grid_strategy-M1-M2-M5-M6.tcl.
TEST(TechProfile, Asap7StrategyMatchesOrfsScript) {
  const auto& pg = tech_profile("asap7").pg;
  EXPECT_EQ(pg.followpin_layers, (std::vector<std::string>{"M1", "M2"}));
  ASSERT_EQ(pg.stripes.size(), 2u);
  EXPECT_EQ(pg.stripes[0].layer, "M5");
  EXPECT_DOUBLE_EQ(pg.stripes[0].width, .12);
  EXPECT_DOUBLE_EQ(pg.stripes[0].spacing, .072);
  EXPECT_DOUBLE_EQ(pg.stripes[0].pitch, 5.4);
  EXPECT_DOUBLE_EQ(pg.stripes[0].offset, .3);
  EXPECT_EQ(pg.stripes[1].layer, "M6");
  EXPECT_DOUBLE_EQ(pg.stripes[1].offset, .513);
  EXPECT_EQ(pg.pin_layer, "M6");
  ASSERT_EQ(pg.macro_grids.size(), 2u);
  for (const auto& grid : pg.macro_grids) {
    EXPECT_DOUBLE_EQ(grid.halo, 2);
    EXPECT_TRUE(grid.stripes.empty());
    EXPECT_EQ(grid.connect,
              (std::vector<std::array<std::string, 2>>{
                  {"M4", "M5"}
    }));
  }
  EXPECT_TRUE(tech_profile("asap7").provides(Tech_policy::route));
}
// NanGate45 follows flow/platforms/nangate45/grid_strategy-M1-M4-M7.tcl; it provides no routing policy.
TEST(TechProfile, Nangate45StrategyMatchesOrfsScript) {
  const auto& p = tech_profile("pg_nangate45");
  EXPECT_EQ(p.name, "nangate45");
  EXPECT_FALSE(p.provides(Tech_policy::route));
  EXPECT_TRUE(p.provides(Tech_policy::power_grid));
  const auto& pg = p.pg;
  EXPECT_EQ(pg.followpin_layers, (std::vector<std::string>{"metal1"}));
  ASSERT_EQ(pg.stripes.size(), 2u);
  EXPECT_EQ(pg.stripes[0].layer, "metal4");
  EXPECT_DOUBLE_EQ(pg.stripes[0].pitch, 56);
  // pdngen's default spacing: pitch / nets - width, so the two nets alternate at half pitch.
  EXPECT_DOUBLE_EQ(pg.stripes[0].width + pg.stripes[0].spacing, 28);
  EXPECT_EQ(pg.stripes[1].layer, "metal7");
  EXPECT_DOUBLE_EQ(pg.stripes[1].pitch, 30);
  EXPECT_DOUBLE_EQ(pg.stripes[1].width + pg.stripes[1].spacing, 15);
  EXPECT_EQ(pg.connect,
            (std::vector<std::array<std::string, 2>>{
                {"metal1", "metal4"},
                {"metal4", "metal7"}
  }));
  EXPECT_FALSE(pg.stripes_start_with_power);  // pdngen -starts_with GROUND
  const auto* upright = pg.macro_grid(Orientation::FS);
  ASSERT_NE(upright, nullptr);
  EXPECT_DOUBLE_EQ(upright->halo, 2);
  ASSERT_EQ(upright->stripes.size(), 2u);
  EXPECT_EQ(upright->stripes[0].layer, "metal5");
  EXPECT_EQ(upright->stripes[1].layer, "metal6");
  EXPECT_DOUBLE_EQ(upright->stripes[1].pitch, 10);
  const auto* rotated = pg.macro_grid(Orientation::E);
  ASSERT_NE(rotated, nullptr);
  ASSERT_EQ(rotated->stripes.size(), 1u);
  EXPECT_DOUBLE_EQ(rotated->stripes[0].pitch, 40);
  EXPECT_EQ(rotated->connect.front(), (std::array<std::string, 2>{"metal4", "metal6"}));
  Technology tech;
  tech.sites["FreePDK45_38x28_10R_NP_162NW_34O"];
  EXPECT_EQ(detect_tech_profile(tech, Tech_policy::power_grid), &p);
  EXPECT_EQ(detect_tech_profile(tech), nullptr);
  EXPECT_THROW(resolve_tech_profile(tech), std::invalid_argument);
}
TEST(TechProfile, OverlappingMacroGridOrientationsAreRejected) {
  // Two macro grids that both claim N: the first match must not silently win.
  auto pg = tech_profile("nangate45").pg;
  ASSERT_EQ(pg.macro_grids.size(), 2u);
  EXPECT_NO_THROW(pg.macro_grid(Orientation::N));
  pg.macro_grids[1].orientations.push_back(Orientation::N);
  EXPECT_THROW(pg.macro_grid(Orientation::N), std::invalid_argument);
  EXPECT_NO_THROW(pg.macro_grid(Orientation::FS));  // still claimed by one grid
  pg.macro_grids[1].orientations.clear();           // empty: every orientation
  EXPECT_THROW(pg.macro_grid(Orientation::FS), std::invalid_argument);
}
}  // namespace livehd::physical
