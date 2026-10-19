// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "technology_rc.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

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
}  // namespace
}  // namespace livehd::placement
