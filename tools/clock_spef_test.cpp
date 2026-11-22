// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_spef.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

namespace livehd::physical {
TEST(ClockSpef, BindsExtractedTreesToTheDefBeforeTiming) {
  const auto        path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "pin-clock.spef";
  const std::string text = R"spef(*SPEF "IEEE 1481-1998"
*DESIGN "top"
*DATE "test"
*VENDOR "test"
*PROGRAM "test"
*VERSION "1"
*DESIGN_FLOW "NAME_SCOPE LOCAL" "PIN_CAP NONE"
*DIVIDER /
*DELIMITER :
*BUS_DELIMITER []
*T_UNIT 1 NS
*C_UNIT 1 PF
*R_UNIT 1 KOHM
*L_UNIT 1 UH
*NAME_MAP
*1 clk
*2 other
*3 ff\[0\]
*PORTS
clk I
*2 I
*D_NET *1 0.006
*CONN
*P clk I
*I *3:CK I
*CAP
1 clk 0.001
2 *3:CK 0.002
3 *3:CK *2:1 0.003
*RES
1 clk *3:CK 0.004
*END
*D_NET *2 0.003
*CONN
*P *2 I
*I *3:D I
*CAP
1 *2:1 *3:CK 0.003
*RES
1 *2 *2:1 0.002
2 *2:1 *3:D 0.001
*END
)spef";
  std::ofstream(path) << text;
  Technology tech;
  tech.macros["DFF"].pins["CK"].direction = "INPUT";
  tech.macros["DFF"].pins["D"].direction  = "INPUT";
  Floorplan fp;
  fp.components.push_back({"ff[0]", "DFF", {}});
  for (const auto* name : {"clk", "other"}) {
    Terminal port;
    port.name      = name;
    port.direction = "INPUT";
    fp.terminals.push_back(port);
  }
  fp.nets = {
      {  "clk",  {{"PIN", "clk"}, {"ff[0]", "CK"}}},
      {"other", {{"PIN", "other"}, {"ff[0]", "D"}}}
  };
  const auto rc = clock_report_spef(path.string(), fp, tech);
  ASSERT_EQ(rc.nets.size(), 2u);
  const auto& clock = rc.nets[0];
  EXPECT_EQ(clock.driver.str(), "clk");
  ASSERT_EQ(clock.nodes.size(), 2u);
  EXPECT_EQ(clock.nodes[1].pin.str(), "ff[0]:CK");
  EXPECT_NEAR(clock.nodes[0].capacitance_ff, 1, 1e-5);
  EXPECT_NEAR(clock.nodes[1].capacitance_ff, 5, 1e-5);
  EXPECT_NEAR(clock.edges.at(0).resistance_ohm, 4, 1e-5);
  EXPECT_NEAR(rc.nets[1].nodes.at(2).capacitance_ff, 3, 1e-5);
  auto broken = text;
  broken.erase(broken.find("1 clk *3:CK 0.004"), std::string("1 clk *3:CK 0.004").size());
  std::ofstream(path) << broken;
  EXPECT_THROW(clock_report_spef(path.string(), fp, tech), std::invalid_argument);
  std::ofstream(path) << text;
  tech.macros["DFF"].pins["CK"].direction = "OUTPUT";
  EXPECT_THROW(clock_report_spef(path.string(), fp, tech), std::invalid_argument);
  tech.macros["DFF"].pins["CK"].direction = "INPUT";
  fp.nets[0].connections[1].pin           = "missing";
  EXPECT_THROW(clock_report_spef(path.string(), fp, tech), std::invalid_argument);
}
}  // namespace livehd::physical
