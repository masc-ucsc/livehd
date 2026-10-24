// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "technology_rc.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <queue>
#include <vector>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
struct Technology_rc : ::testing::Test {
  physical::Technology  tech;
  std::filesystem::path root;
  void                  SetUp() override {
    root = std::filesystem::path(std::getenv("TEST_TMPDIR")) / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::create_directories(root);
    tech.routing_layers         = {"M2", "M3"};
    tech.layers["M2"].direction = "HORIZONTAL";
    tech.layers["M3"].direction = "VERTICAL";
  }
  std::string write(const char* name, const std::string& data) {
    const auto path = root / name;
    std::ofstream(path) << data;
    return path.string();
  }
};
TEST_F(Technology_rc, PessimismScalesWireProductAndPreservesDefault) {
  const auto nominal   = technology_wire_rc(tech, "", "", 3, 4);
  const auto unchanged = technology_wire_rc(tech, "", "", 3, 4, 1);
  EXPECT_DOUBLE_EQ(nominal.horizontal_r, unchanged.horizontal_r);
  EXPECT_DOUBLE_EQ(nominal.vertical_c, unchanged.vertical_c);
  for (double factor : {1.5, 2.}) {
    const auto wire = technology_wire_rc(tech, "", "", 3, 4, factor);
    EXPECT_DOUBLE_EQ(wire.horizontal_r, 3 * std::sqrt(factor));
    EXPECT_DOUBLE_EQ(wire.horizontal_c, 4 * std::sqrt(factor));
    EXPECT_NEAR(wire.horizontal_r * wire.horizontal_c, 12 * factor, 1e-12);
    EXPECT_NEAR(wire.vertical_r * wire.vertical_c, 12 * factor, 1e-12);
  }
  const auto file
      = write("rc.tcl", "set_cmd_units -resistance ohm -capacitance ff\nset_wire_rc -signal -resistance 3 -capacitance 4\n");
  const auto characterized = technology_wire_rc(tech, "", file, -1, -1, 2);
  EXPECT_DOUBLE_EQ(characterized.horizontal_r, 3 * std::sqrt(2.));
  EXPECT_DOUBLE_EQ(characterized.vertical_c, 4 * std::sqrt(2.));
  for (double invalid : {0., -1., .5, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_THROW(technology_wire_rc(tech, "", "", 3, 4, invalid), std::invalid_argument);
  }
}
TEST_F(Technology_rc, ExplicitOverrideAndLefFallbackKeepTheirUnits) {
  const auto explicit_rc = technology_wire_rc(tech, "", "missing", 3, 4);
  EXPECT_EQ(explicit_rc.horizontal_r, 3);
  EXPECT_EQ(explicit_rc.vertical_c, 4);
  auto& h       = tech.layers.at("M2");
  h.width       = .2;
  h.resistance  = 2;
  h.capacitance = .3;
  h.edge_cap    = .01;
  auto& v       = tech.layers.at("M3");
  v.width       = .1;
  v.resistance  = 3;
  v.capacitance = .4;
  v.edge_cap    = .02;
  const auto rc = technology_wire_rc(tech, "", "");
  EXPECT_DOUBLE_EQ(rc.horizontal_r, 10);
  EXPECT_DOUBLE_EQ(rc.horizontal_c, 80);
  EXPECT_DOUBLE_EQ(rc.vertical_r, 30);
  EXPECT_DOUBLE_EQ(rc.vertical_c, 80);
}
TEST_F(Technology_rc, CharacterizedSignalUsesMainLibraryUnitsAndDoesNotExecuteTcl) {
  const auto lib  = write("cells.lib", R"(library(main) {
 pulling_resistance_unit : "1kohm";
 capacitive_load_unit(1,ff);
 cell(inv) { }
}
library(memory) { pulling_resistance_unit : "1ohm"; capacitive_load_unit(1,pf); }
)");
  const auto file = write("setRC.tcl", R"(# Technology data, not an executable input.
set_layer_rc -layer M2 -resistance .02 -capacitance .18
set_layer_rc -via V1 -resistance .0172
set_wire_rc -signal -resistance 3.23151E-02 \
 -capacitance 1.73323E-01
set_wire_rc -clock -resistance .05 -capacitance .14
)");
  const auto rc   = technology_wire_rc(tech, lib, file);
  EXPECT_DOUBLE_EQ(rc.horizontal_r, 32.3151);
  EXPECT_DOUBLE_EQ(rc.vertical_r, 32.3151);
  EXPECT_DOUBLE_EQ(rc.horizontal_c, .173323);
  EXPECT_EQ(rc.source, file);
}
TEST_F(Technology_rc, ExplicitFileUnitsAndLayerSelectionAreTechIndependent) {
  const auto file = write("rc.tcl", R"(set_cmd_units -time ns -resistance ohm -capacitance pf
set_layer_rc -layer M2 -resistance 12 -capacitance .001
set_layer_rc -layer M3 -resistance 13 -capacitance .002
)");
  const auto rc   = technology_wire_rc(tech, "", file);
  EXPECT_EQ(rc.horizontal_r, 12);
  EXPECT_EQ(rc.horizontal_c, 1);
  EXPECT_EQ(rc.vertical_r, 13);
  EXPECT_EQ(rc.vertical_c, 2);
  const auto selected = technology_wire_rc(tech, "", write("layer.tcl", R"(set_cmd_units -resistance kohm -capacitance ff
set_layer_rc -layer M3 -resistance .013 -capacitance 2
set_wire_rc -signal -layer M3
)"));
  EXPECT_DOUBLE_EQ(selected.horizontal_r, 13);
  EXPECT_EQ(selected.horizontal_c, 2);
  EXPECT_EQ(selected.vertical_r, selected.horizontal_r);
}
TEST_F(Technology_rc, MissingOrAmbiguousTechnologyDataFailsInsteadOfGuessing) {
  EXPECT_THROW(technology_wire_rc(tech, "", ""), std::runtime_error);
  EXPECT_THROW(technology_wire_rc(tech, "", write("no_units.tcl", "set_wire_rc -signal -resistance 1 -capacitance 2\n")),
               std::runtime_error);
  for (const auto* value : {"nan", "inf", "-1", "$r", "[expr"}) {
    EXPECT_THROW(
        technology_wire_rc(tech,
                           "",
                           write("bad.tcl",
                                 std::string("set_cmd_units -resistance ohm -capacitance ff\nset_wire_rc -signal -resistance ")
                                     + value + " -capacitance 2\n")),
        std::runtime_error);
  }
  EXPECT_THROW(technology_wire_rc(tech, "", write("code.tcl", "exec touch sentinel\n")), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(root / "sentinel"));
  EXPECT_THROW(technology_wire_rc(tech,
                                  "",
                                  write("unknown.tcl",
                                        "set_cmd_units -resistance ohm -capacitance ff\nset_wire_rc -signal -resistance 1 "
                                        "-capacitance 2 -corner arbitrary\n")),
               std::runtime_error);
}
TEST_F(Technology_rc, DistanceUnitsNormalizePerLengthValues) {
  const auto rc = technology_wire_rc(
      tech,
      "",
      write("distance.tcl",
            "set_cmd_units -resistance kohm -capacitance ff -distance nm\nset_wire_rc -signal -resistance .03 -capacitance .2\n"));
  EXPECT_DOUBLE_EQ(rc.horizontal_r, 30000);
  EXPECT_DOUBLE_EQ(rc.horizontal_c, 200);
}
TEST(Steiner_rc, NetsBeyondFluteDegreeAreTreesNotStars) {
  // 1500 sinks on a 1 um pitch line, driver at one end: a Steiner tree has
  // about 1500 um of wire; a star from the driver would have about 1.1 m.
  std::vector<std::pair<std::string, std::pair<double, double>>> pins{
      {"drv:Y", {0, 0}}
  };
  for (int i = 1; i <= 1500; ++i) {
    pins.emplace_back("s" + std::to_string(i) + ":A", std::pair{1000.0 * i, 1000.0 * (i % 3)});
  }
  Wire_rc wire;
  wire.horizontal_r = wire.vertical_r = 1;
  wire.horizontal_c = wire.vertical_c = 1;
  bool       approximate              = false;
  const auto rc                       = steiner_rc("drv:Y", pins, wire, 1000, &approximate);
  EXPECT_TRUE(approximate);
  double cap = 0;
  for (const auto& node : rc.nodes) {
    cap += node.capacitance_ff;
  }
  EXPECT_LT(cap, 3 * 1500.0 + 3 * 1500);  // tree length (with the 0..2 um zigzag), not a star
  EXPECT_GT(cap, 1400);
  // Every pin is connected to the driver.
  std::vector<std::vector<unsigned>> adjacent(rc.nodes.size());
  for (const auto& e : rc.edges) {
    adjacent[e.from].push_back(e.to);
    adjacent[e.to].push_back(e.from);
  }
  std::vector<bool>    seen(rc.nodes.size(), false);
  std::queue<unsigned> todo;
  todo.push(0);
  seen[0] = true;
  while (!todo.empty()) {
    const auto n = todo.front();
    todo.pop();
    for (const auto m : adjacent[n]) {
      if (!seen[m]) {
        seen[m] = true;
        todo.push(m);
      }
    }
  }
  for (size_t i = 0; i < rc.nodes.size(); ++i) {
    if (!rc.nodes[i].pin.empty()) {
      EXPECT_TRUE(seen[i]) << rc.nodes[i].pin;
    }
  }
}
}  // namespace
}  // namespace livehd::placement
