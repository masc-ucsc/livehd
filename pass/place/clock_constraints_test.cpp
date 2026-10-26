// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_constraints.hpp"

#include <stdexcept>

#include "gtest/gtest.h"

namespace late = livehd::placement::late;
TEST(ClockConstraints, RelatedClocksUseTheClosestPositiveSetupEdge) {
  std::map<std::string, late::Ideal_clock> models{
      {"a", {.period_ps = 100, .roots = {"ca"}}},
      {"b", {.period_ps = 150, .roots = {"cb"}}}
  };
  late::Clock_system clocks({}, models, {}, {});
  EXPECT_EQ(clocks.separation(clocks.find("a"), clocks.find("a")), 100);
  EXPECT_EQ(clocks.separation(clocks.find("a"), clocks.find("b")), 50);
  EXPECT_EQ(clocks.separation(clocks.find("b"), clocks.find("a")), 50);
  models.at("a").phase_ps = 10;
  models.at("b").phase_ps = 25;
  clocks                  = late::Clock_system({}, models, {}, {});
  EXPECT_EQ(clocks.separation(clocks.find("a"), clocks.find("b")), 15);
  EXPECT_EQ(clocks.separation(clocks.find("b"), clocks.find("a")), 35);
}
TEST(ClockConstraints, AliasesInheritAsynchronousGroupsAndDoNotCreateNewWaveforms) {
  late::Clock_system clocks(
      {
  },
      {{"a", {.period_ps = 100, .roots = {"ca"}}}, {"b", {.period_ps = 200, .roots = {"cb"}}}},
      {{"fwd", {"a", {"port_clk"}}}},
      {{"fwd", "b"}});
  EXPECT_EQ(clocks.clocks.size(), 2);
  EXPECT_EQ(clocks.find("a"), clocks.find("fwd"));
  EXPECT_FALSE(clocks.separation(clocks.find("fwd"), clocks.find("b")));
  EXPECT_EQ(clocks.alias_roots.at("port_clk"), clocks.find("a"));
}
TEST(ClockConstraints, RejectsAmbiguousOrUnsupportedClockSpecifications) {
  const late::Ideal_clock model{.period_ps = 100, .roots = {"clk"}};
  EXPECT_THROW((late::Clock_system(model,
                                   {
                                       {"a", model}
  },
                                   {},
                                   {})),
               std::invalid_argument);
  EXPECT_THROW((late::Clock_system(
                   {
  },
                   {{"a", model}},
                   {{"x", {"y", {"cx"}}}, {"y", {"x", {"cy"}}}},
                   {})),
               std::invalid_argument);
  EXPECT_THROW((late::Clock_system(
                   {
  },
                   {{"a", model}},
                   {},
                   {{"a", "missing"}})),
               std::invalid_argument);
  EXPECT_THROW((late::Clock_system(
                   {
  },
                   {{"a", model}},
                   {{"fwd", {"a", {"co"}}}},
                   {{"a", "fwd"}})),
               std::invalid_argument);
  auto invalid     = model;
  invalid.phase_ps = 100;
  EXPECT_THROW((late::Clock_system(invalid, {}, {}, {})), std::invalid_argument);
}
