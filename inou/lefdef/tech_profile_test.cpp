// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tech_profile.hpp"

#include "gtest/gtest.h"

namespace livehd::physical {
TEST(TechProfile, ExplicitNamesAndStrategyAliasesSelectOneEntry) {
  const Technology empty;
  const auto&      asap7 = tech_profile("asap7");
  EXPECT_EQ(&tech_profile(asap7.pg.name), &asap7);
  EXPECT_EQ(&resolve_tech_profile(empty, "asap7"), &asap7);
  EXPECT_THROW(tech_profile("missing"), std::invalid_argument);
  EXPECT_THROW(tech_profile("auto"), std::invalid_argument);
  EXPECT_THROW(resolve_tech_profile(empty, "missing"), std::invalid_argument);
}
TEST(TechProfile, AutoDetectionRequiresAProfileSiteOrMacro) {
  const auto& asap7 = tech_profile("asap7");
  Technology  tech;
  ASSERT_FALSE(asap7.detect_sites.empty() || asap7.detect_macros.empty());
  EXPECT_THROW(resolve_tech_profile(tech), std::invalid_argument);
  tech.sites["unithd"];
  tech.macros["INVx1"];
  EXPECT_THROW(resolve_tech_profile(tech), std::invalid_argument);
  for (const auto& site : asap7.detect_sites) {
    auto with_site = tech;
    with_site.sites[site];
    EXPECT_EQ(&resolve_tech_profile(with_site), &asap7);
  }
  for (const auto& macro : asap7.detect_macros) {
    auto with_macro = tech;
    with_macro.macros[macro];
    EXPECT_EQ(&resolve_tech_profile(with_macro), &asap7);
  }
}
}  // namespace livehd::physical
