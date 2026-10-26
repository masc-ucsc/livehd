// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tech_profile.hpp"

#include <set>

#include "gtest/gtest.h"

namespace livehd::physical {
TEST(TechProfile, ExplicitNamesAndStrategyAliasesSelectOneEntry) {
  const Technology empty;
  ASSERT_FALSE(tech_profiles().empty());
  for (const auto& profile : tech_profiles()) {
    EXPECT_EQ(&tech_profile(profile.name), &profile);
    EXPECT_EQ(&tech_profile(profile.pg.name), &profile);
    EXPECT_EQ(&resolve_tech_profile(empty, profile.name), &profile);
  }
  EXPECT_THROW(tech_profile("missing"), std::invalid_argument);
  EXPECT_THROW(tech_profile("auto"), std::invalid_argument);
  EXPECT_THROW(resolve_tech_profile(empty, "missing"), std::invalid_argument);
}
TEST(TechProfile, AutoDetectionRequiresAProfileSiteOrMacro) {
  Technology tech;
  EXPECT_THROW(resolve_tech_profile(tech), std::invalid_argument);
  tech.sites["unithd"];
  tech.macros["INVx1"];
  EXPECT_THROW(resolve_tech_profile(tech), std::invalid_argument);
  for (const auto& profile : tech_profiles()) {
    for (const auto& site : profile.detect_sites) {
      auto with_site = tech;
      with_site.sites[site];
      EXPECT_EQ(&resolve_tech_profile(with_site), &profile) << site;
    }
    for (const auto& macro : profile.detect_macros) {
      auto with_macro = tech;
      with_macro.macros[macro];
      EXPECT_EQ(&resolve_tech_profile(with_macro), &profile) << macro;
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
    EXPECT_FALSE(r.access.pin.empty() || r.access.escape.empty() || r.access.cut.empty());
    EXPECT_NE(r.access.pin, r.access.escape);
    EXPECT_FALSE(r.branch_layer.empty() || r.branch_cut.empty());
    EXPECT_EQ(std::set<std::string>(r.signal_layers.begin(), r.signal_layers.end()).size(), r.signal_layers.size());
    EXPECT_TRUE(r.allows(r.access.escape));
    EXPECT_TRUE(r.allows(r.branch_layer));
    EXPECT_FALSE(r.allows(r.access.pin));
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
    for (const auto& pairs : {pg.connect, pg.macro_connect}) {
      for (const auto& [lower, upper] : pairs) {
        EXPECT_FALSE(lower.empty() || upper.empty());
        EXPECT_NE(lower, upper);
      }
    }
    EXPECT_GE(pg.macro_halo, 0);
    EXPECT_NE(pg.nets[0], pg.nets[1]);
    for (size_t i = 0; i < pg.nets.size(); ++i) {
      EXPECT_FALSE(pg.nets[i].empty());
      EXPECT_FALSE(pg.pin_names[i].empty());
    }
    EXPECT_TRUE(pg.unsupported.empty());
  }
}
}  // namespace livehd::physical
