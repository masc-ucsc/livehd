#include "physical.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "routing_occupancy.hpp"
namespace livehd::physical {
namespace {
std::filesystem::path routing_lef() {
  const auto    path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "routing.lef";
  std::ofstream out(path);
  out << R"(VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
MANUFACTURINGGRID 0.001 ;
PROPERTYDEFINITIONS
 LAYER LEF58_RECTONLY STRING ;
END PROPERTYDEFINITIONS
LAYER M1
 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.036 ; WIDTH 0.018 ; SPACING 0.018 ;
 SPACING 0.04 SAMENET ;
END M1
LAYER V1
 TYPE CUT ; WIDTH 0.018 ; SPACING 0.018 ;
END V1
LAYER M2
 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.036 ; WIDTH 0.018 ; SPACING 0.018 ;
 AREA 0.000666 ; MINWIDTH 0.018 ; MAXWIDTH 0.5 ;
 SPACING 0.025 RANGE 0.036 0.1 ;
 SPACING 0.031 ENDOFLINE 0.025 WITHIN 0.02 PARALLELEDGE 0.025 WITHIN 0.02 TWOEDGES ;
 SPACINGTABLE PARALLELRUNLENGTH 0.0 0.2
  WIDTH 0.018 0.018 0.020
  WIDTH 0.1 0.030 0.040 ;
 MINSIZE 0.037 0.018 ;
 MINSTEP 0.01 MAXEDGES 2 ;
 PROPERTY LEF58_RECTONLY "RECTONLY ;" ;
END M2
LAYER V2
 TYPE CUT ; WIDTH 0.018 ; SPACING 0.036 CENTERTOCENTER ;
END V2
VIA VIA12 DEFAULT
 RESISTANCE 5 ;
 LAYER M1 ; RECT -0.009 -0.011 0.009 0.011 ;
 LAYER V1 ; RECT -0.009 -0.009 0.009 0.009 ;
 LAYER M2 ; RECT -0.014 -0.009 0.014 0.009 ;
END VIA12
VIARULE M2_M1 GENERATE DEFAULT
 LAYER M1 ; ENCLOSURE 0.0 0.0 ; WIDTH 0.018 TO 0.5 ;
 LAYER M2 ; ENCLOSURE 0.002 0.0 ;
 LAYER V1 ; RECT -0.009 -0.009 0.009 0.009 ; SPACING 0.036 BY 0.036 ;
END M2_M1
SITE unit
 CLASS CORE ; SIZE 0.054 BY 0.27 ; SYMMETRY Y ;
END unit
MACRO inv
 CLASS CORE ; ORIGIN 0 0 ; SIZE 0.108 BY 0.27 ; SITE unit ;
END inv
END LIBRARY
)";
  return path;
}
}  // namespace
TEST(Physical, LefViaGeometryAndGenerationRules) {
  const auto tech = read_lef({routing_lef().string()});
  EXPECT_EQ(tech.routing_layers, (std::vector<std::string>{"M1", "M2"}));
  EXPECT_FALSE(tech.layers.contains("V1"));
  ASSERT_EQ(tech.cut_layers.size(), 2);
  EXPECT_DOUBLE_EQ(tech.cut_layers.at("V1").width, .018);
  EXPECT_EQ(tech.cut_layers.at("V1").spacing, (std::vector<double>{.018}));
  EXPECT_TRUE(tech.cut_layers.at("V1").unsupported.empty());
  EXPECT_TRUE(tech.cut_layers.at("V2").spacing.empty());
  EXPECT_TRUE(tech.cut_layers.at("V2").unsupported.contains("qualified SPACING"));
  const auto& via = tech.vias.at("VIA12");
  EXPECT_TRUE(via.is_default);
  EXPECT_DOUBLE_EQ(via.resistance, 5);
  ASSERT_EQ(via.shapes.size(), 3);
  EXPECT_EQ(via.shapes[0], (Shape{"M1", {-.009, -.011, .009, .011}}));
  EXPECT_TRUE(via.unsupported.empty());
  const auto& rule = tech.via_rules.at("M2_M1");
  EXPECT_TRUE(rule.generate);
  EXPECT_TRUE(rule.is_default);
  EXPECT_TRUE(rule.unsupported.empty());
  ASSERT_EQ(rule.layers.size(), 3);
  EXPECT_EQ(rule.layers[0].width, (std::array<double, 2>{.018, .5}));
  EXPECT_EQ(rule.layers[1].enclosure, (std::array<double, 2>{.002, 0}));
  EXPECT_EQ(rule.layers[2].name, "V1");
  EXPECT_EQ(rule.layers[2].cut, (Rect{-.009, -.009, .009, .009}));
  // A 36 nm cut pitch is distinct from the 18 nm edge spacing of V1.
  EXPECT_EQ(rule.layers[2].spacing, (std::array<double, 2>{.036, .036}));
}
TEST(Physical, UnsupportedPinAndObstructionGeometryIsNotDropped) {
  const auto        path = routing_lef();
  std::ifstream     input(path);
  const std::string original((std::istreambuf_iterator<char>(input)), {});
  input.close();
  for (const bool pin : {false, true}) {
    for (const std::string shape : {"PATH 0.01 0.01 0.08 0.01 ;", "VIA 0.04 0.04 VIA12 ;"}) {
      auto       text     = original;
      const auto geometry = (pin ? "PIN A\n DIRECTION INPUT ;\n PORT\n" : "OBS\n")
                            + std::string(" LAYER M1 ;\n WIDTH 0.018 ;\n RECT 0.01 0.01 0.03 0.03 ;\n ") + shape + "\n END\n"
                            + (pin ? "END A\n" : "");
      text.insert(text.find("END inv"), geometry);
      {
        std::ofstream output(path);
        output << text;
      }
      try {
        (void)read_lef({path.string()});
        FAIL() << "unsupported geometry silently accepted: " << geometry;
      } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find(pin ? "unsupported LEF pin PATH/VIA" : "unsupported LEF obstruction PATH/VIA"),
                  std::string::npos)
            << error.what();
      }
    }
  }
}
TEST(Physical, PinAndObstructionGeometryRulesAreNotDiscarded) {
  const auto        path = routing_lef();
  std::ifstream     input(path);
  const std::string original((std::istreambuf_iterator<char>(input)), {});
  input.close();
  for (const bool pin : {false, true}) {
    for (const std::string rule : {"LAYER M1 SPACING 0.05 ; RECT 0 0 0.03 0.03 ;",
                                   "LAYER M1 DESIGNRULEWIDTH 0.05 ; RECT 0 0 0.03 0.03 ;",
                                   "LAYER M1 ; RECT MASK 1 0 0 0.03 0.03 ;",
                                   "LAYER M1 ; POLYGON MASK 1 0 0 0.03 0 0.03 0.03 0 0.03 ;"}) {
      auto text = original;
      text.insert(text.find("END inv"),
                  (pin ? "PIN A\n DIRECTION INPUT ;\n PORT\n" : "OBS\n") + rule + "\n END\n" + (pin ? "END A\n" : ""));
      {
        std::ofstream output(path);
        output << text;
      }
      try {
        (void)read_lef({path.string()});
        FAIL() << "geometry rule silently discarded: " << rule;
      } catch (const std::runtime_error& error) {
        EXPECT_NE(
            std::string(error.what()).find(pin ? "unsupported LEF pin geometry rule" : "unsupported LEF obstruction geometry rule"),
            std::string::npos)
            << error.what();
      }
    }
  }
}
TEST(Physical, PolygonExtensionsOutsideCellRemainBlocked) {
  const auto        path = routing_lef();
  std::ifstream     input(path);
  const std::string original((std::istreambuf_iterator<char>(input)), {});
  input.close();
  for (const bool pin : {false, true}) {
    auto text = original;
    text.insert(text.find("END inv"),
                std::string(pin ? "PIN A\n DIRECTION INPUT ;\n PORT\n" : "OBS\n")
                    + "LAYER M1 ; POLYGON -0.05 0.02 0.16 0.02 0.16 0.05 0.03 0.05 0.03 0.10 -0.05 0.10 ;\n END\n"
                    + (pin ? "END A\n" : ""));
    {
      std::ofstream output(path);
      output << text;
    }
    const auto  tech  = read_lef({path.string()});
    const auto& macro = tech.macros.at("inv");
    ASSERT_EQ(macro.obstructions.size(), 1);
    EXPECT_EQ(macro.obstructions.front(), (Shape{"M1", {-0.05, 0.02, 0.16, 0.10}}));
    if (pin) {
      const auto& access = macro.pins.at("A").shapes;
      ASSERT_EQ(access.size(), 1);
      EXPECT_EQ(access.front().rect.xl, access.front().rect.xh);
      EXPECT_EQ(access.front().rect.yl, access.front().rect.yh);
    }
    for (auto orientation : {Orientation::N, Orientation::FN}) {
      Floorplan fp;
      Ann_place location(100, 200, 108, 270);
      location.status      = Status::PLACED;
      location.orientation = orientation;
      fp.components.push_back({"u", "inv", location});
      const auto   index   = routing_occupancy(tech, fp, 64);
      const double x       = orientation == Orientation::N ? 250 : 50;
      const auto   blocked = index.query("M1", {x, 225, x + 1, 226}, 0);
      ASSERT_FALSE(blocked.empty());
      for (auto id : blocked) {
        EXPECT_TRUE(index.at(id).net.empty());
      }
    }
  }
}
TEST(Physical, LefConflictingViaCannotReplaceGeometry) {
  const auto base  = routing_lef();
  const auto extra = base.parent_path() / "extra_via.lef";
  const auto write = [&](double width) {
    std::ofstream out(extra);
    out << "VERSION 5.8 ;\nVIA VIA12 DEFAULT\n RESISTANCE 5 ;\n"
           " LAYER M1 ; RECT -0.009 -0.011 0.009 0.011 ;\n"
           " LAYER V1 ; RECT -0.009 -0.009 0.009 0.009 ;\n"
           " LAYER M2 ; RECT -0.014 -0.009 "
        << width
        << " 0.009 ;\n"
           "END VIA12\nEND LIBRARY\n";
  };
  write(.014);
  EXPECT_NO_THROW(read_lef({base.string(), extra.string()}));
  write(.015);
  EXPECT_THROW(read_lef({base.string(), extra.string()}), std::runtime_error);
}
TEST(Physical, LefMetalRulesRetainConditionsAndUnknownExtensions) {
  const auto  tech = read_lef({routing_lef().string()});
  const auto& r    = tech.layers.at("M2").rules;
  EXPECT_TRUE(r.loaded);
  EXPECT_DOUBLE_EQ(r.area, .000666);
  EXPECT_EQ(r.min_width, .018);
  EXPECT_EQ(r.max_width, .5);
  ASSERT_EQ(r.spacing.size(), 3);
  EXPECT_DOUBLE_EQ(r.spacing[0].distance, .018);
  EXPECT_EQ(r.spacing[1].range, (std::array<double, 2>{.036, .1}));
  ASSERT_TRUE(r.spacing[2].eol);
  EXPECT_DOUBLE_EQ(r.spacing[2].eol->width, .025);
  EXPECT_DOUBLE_EQ(r.spacing[2].eol->within, .02);
  EXPECT_EQ(r.spacing[2].eol->parallel_edge, (std::array<double, 2>{.025, .02}));
  EXPECT_TRUE(r.spacing[2].eol->two_edges);
  ASSERT_EQ(r.parallel.size(), 1);
  EXPECT_EQ(r.parallel[0].widths, (std::vector<double>{.018, .1}));
  EXPECT_EQ(r.parallel[0].lengths, (std::vector<double>{0, .2}));
  EXPECT_EQ(r.parallel[0].spacing, (std::vector<std::vector<double>>{{.018, .020}, {.030, .040}}));
  EXPECT_EQ(r.min_size, (std::vector<std::array<double, 2>>{{.037, .018}}));
  ASSERT_EQ(r.min_step.size(), 1);
  EXPECT_DOUBLE_EQ(r.min_step[0].distance, .01);
  EXPECT_EQ(r.min_step[0].max_edges, 2);
  EXPECT_EQ(r.properties.at("LEF58_RECTONLY"), (std::vector<std::string>{"RECTONLY ;"}));
  EXPECT_TRUE(r.unsupported.contains("PROPERTY LEF58_RECTONLY"));
  EXPECT_TRUE(tech.layers.at("M1").rules.unsupported.contains("qualified SPACING"));
  EXPECT_FALSE(Layer{}.rules.loaded);
}
TEST(Physical, Sky130GeometryAndRc) {
  const auto tech = read_lef({"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"});
  EXPECT_GT(tech.dbu, 0);
  EXPECT_NEAR(tech.manufacturing_grid, .005, 1e-12);
  EXPECT_NEAR(tech.macros.at("sky130_fd_sc_hd__inv_1").width, 1.38, 1e-9);
  EXPECT_FALSE(tech.macros.at("sky130_fd_sc_hd__inv_1").pins.at("Y").shapes.empty());
  EXPECT_GT(tech.layers.at("met1").resistance, 0);
  EXPECT_TRUE(tech.macros.at("sky130_fd_sc_hd__inv_1").blocked_layers.contains("li1"));
  EXPECT_FALSE(tech.macros.at("sky130_fd_sc_hd__inv_1").obstructions.empty());
  EXPECT_TRUE(tech.layers.at("li1").has_offset);
  EXPECT_NEAR(tech.layers.at("li1").offset_x, .23, 1e-9);
  EXPECT_NEAR(tech.layers.at("li1").offset_y, .17, 1e-9);
  EXPECT_NEAR(tech.layers.at("li1").pitch_x, 0.46, 1e-9);
  EXPECT_NEAR(tech.layers.at("li1").pitch_y, 0.34, 1e-9);
  EXPECT_NEAR(tech.layers.at("met1").pitch_y, 0.34, 1e-9);
}
TEST(Physical, DefIdentityRoundtrip) {
  Floorplan fp;
  fp.design = "top";
  fp.die    = {0, 0, 10000, 10000};
  fp.rows.push_back({"row0", "unithd", 0, 0, 10, 460, Orientation::FS});
  Ann_place place(460, 0, 1380, 2720);
  place.orientation = Orientation::FS;
  place.status      = Status::FIXED;
  fp.components.push_back({"block.gate[0]", "sky130_fd_sc_hd__inv_1", place});
  fp.terminals.push_back({"a[0]", "wire0", "INPUT", 0, 1360, Orientation::N, Status::FIXED, {{"met1", {0, -10, 10, 10}}}});
  fp.nets.push_back({"wire0", {{"PIN", "a[0]"}, {"block.gate[0]", "A"}}});
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "roundtrip.def";
  write_def(fp, path.string());
  EXPECT_NO_THROW(verify_def_roundtrip(fp, read_def(path.string())));
  auto broken = read_def(path.string());
  broken.terminals[0].x++;
  EXPECT_THROW(verify_def_roundtrip(fp, broken), std::runtime_error);
}
TEST(Physical, DefTracksAndMasksRoundtrip) {
  Floorplan fp;
  fp.design       = "tracks";
  fp.die          = {0, 0, 10000, 10000};
  fp.tracks       = {{"X", "met2", 230, 20, 460}, {"Y", "met1", 170, 30, 340, 1, true}};
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "tracks.def";
  write_def(fp, path.string());
  const auto restored = read_def(path.string());
  EXPECT_EQ(restored.tracks, fp.tracks);
  EXPECT_NO_THROW(verify_def_roundtrip(fp, restored));
  auto changed = restored;
  ++changed.tracks.front().start;
  EXPECT_THROW(verify_def_roundtrip(fp, changed), std::runtime_error);
}
TEST(Physical, DefNonuniformGcellGridRoundtrip) {
  Floorplan fp;
  fp.design       = "gcells";
  fp.die          = {0, 0, 10300, 12000};
  fp.gcell_grid   = {{"X", 0, 11, 1000}, {"X", 10000, 2, 300}, {"Y", 0, 7, 2000}};
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "gcells.def";
  write_def(fp, path.string());
  auto actual = read_def(path.string());
  EXPECT_NO_THROW(verify_def_roundtrip(fp, actual));
  ++actual.gcell_grid[1].step;
  EXPECT_THROW(verify_def_roundtrip(fp, actual), std::runtime_error);
  fp.gcell_grid[0].axis = "Z";
  EXPECT_THROW(write_def(fp, path.string()), std::runtime_error);
}
TEST(Physical, DefEscapedInstanceNames) {
  // The Si2 DEF reader retains escapes, as OpenDB does. Re-escaping these
  // names during export changes the instances referenced by their nets.
  Floorplan fp;
  fp.design              = "top";
  fp.die                 = {0, 0, 10000, 10000};
  const std::string name = "delay_data\\[10\\]$_SDFF_PP0_";
  Ann_place         place(460, 0, 1380, 2720);
  place.status = Status::FIXED;
  fp.components.push_back({name, "sky130_fd_sc_hd__inv_1", place});
  fp.nets.push_back({"wire0", {{name, "A"}}});
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "escaped.def";
  write_def(fp, path.string());
  const auto restored = read_def(path.string());
  EXPECT_EQ(restored.components.front().name, name);
  EXPECT_EQ(restored.nets.front().connections.front().instance, name);
  EXPECT_NO_THROW(verify_def_roundtrip(fp, restored));
}
TEST(Physical, DefPreEscapedSpecialNames) {
  // Names that already carry a retained escape before a special character must not be escaped twice.
  Floorplan fp;
  fp.design = "top";
  fp.die    = {0, 0, 10000, 10000};
  Ann_place place(460, 0, 1380, 2720);
  place.status = Status::FIXED;
  for (const std::string name : {"a\\+b", "a\\(b", "a\\;b"}) {
    fp.components.clear();
    fp.nets.clear();
    fp.components.push_back({name, "sky130_fd_sc_hd__inv_1", place});
    fp.nets.push_back({"wire0", {{name, "A"}}});
    const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "pre_escaped.def";
    write_def(fp, path.string());
    const auto restored = read_def(path.string());
    EXPECT_EQ(restored.components.front().name, name);
    EXPECT_EQ(restored.nets.front().connections.front().instance, name);
  }
  // A trailing lone backslash must not swallow the following separator; it reads back as an escaped backslash.
  fp.components.clear();
  fp.nets.clear();
  fp.components.push_back({"tail\\", "sky130_fd_sc_hd__inv_1", place});
  fp.nets.push_back({"wire0", {{"tail\\", "A"}}});
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "trailing.def";
  write_def(fp, path.string());
  const auto restored = read_def(path.string());
  ASSERT_EQ(restored.components.size(), 1);
  ASSERT_EQ(restored.nets.size(), 1);
  EXPECT_EQ(restored.components.front().macro, "sky130_fd_sc_hd__inv_1");
}
TEST(Physical, DefSinglePortPinKeepsGeometryAndPlacement) {
  const auto path  = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "port.def";
  const auto write = [&](const std::string& geometry) {
    std::ofstream out(path);
    out << "VERSION 5.8 ;\nDESIGN top ;\nUNITS DISTANCE MICRONS 1000 ;\n"
           "DIEAREA ( 0 0 ) ( 10000 10000 ) ;\nPINS 1 ;\n"
           "- a + NET wire0 + DIRECTION INPUT "
        << geometry << " ;\nEND PINS\nEND DESIGN\n";
  };
  const std::string geometry = "+ LAYER met1 ( -10 -20 ) ( 10 20 ) + FIXED ( 100 200 ) FN";
  write(geometry);
  const auto direct = read_def(path.string());
  write("+ PORT " + geometry);
  const auto port = read_def(path.string());
  EXPECT_NO_THROW(verify_def_roundtrip(direct, port));
  ASSERT_EQ(port.terminals.size(), 1);
  EXPECT_EQ(port.terminals[0].x, 100);
  EXPECT_EQ(port.terminals[0].y, 200);
  EXPECT_EQ(port.terminals[0].orientation, Orientation::FN);
  EXPECT_EQ(port.terminals[0].status, Status::FIXED);
  const auto roundtrip = path.parent_path() / "port_roundtrip.def";
  write_def(port, roundtrip.string());
  EXPECT_NO_THROW(verify_def_roundtrip(port, read_def(roundtrip.string())));
  write("+ PORT " + geometry + " + PORT " + geometry);
  EXPECT_THROW(read_def(path.string()), std::runtime_error);
  write("+ PORT + POLYGON met1 ( 0 0 ) ( 20 0 ) ( 20 20 ) + FIXED ( 100 200 ) N");
  EXPECT_THROW(read_def(path.string()), std::runtime_error);
}
TEST(Physical, OrientationTransforms) {
  EXPECT_EQ(orient_point(1, 2, 10, 20, Orientation::FS), (std::pair<double, double>{1, 18}));
  EXPECT_EQ(orient_point(1, 2, 10, 20, Orientation::FN), (std::pair<double, double>{9, 2}));
  EXPECT_EQ(orient_point(1, 2, 10, 20, Orientation::W), (std::pair<double, double>{18, 1}));
}
TEST(Physical, PlacementBlockagesPreserveAssociationsAndRoundtrip) {
  Floorplan fp;
  fp.design = "blocked";
  fp.die    = {0, 0, 10000, 10000};
  Ann_place fixed(1000, 1000, 1000, 1000);
  fixed.status = Status::FIXED;
  fp.components.push_back({"ram\\[0\\]", "RAM", fixed});
  fp.blockages = {
      { true, "ram\\[0\\]", {{900, 900, 2100, 2100}, {3000, 3000, 4000, 4000}}},
      {false,           {},                           {{0, 5000, 10000, 6000}}}
  };
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "blockages.def";
  write_def(fp, path.string());
  const auto restored = read_def(path.string());
  EXPECT_EQ(restored.blockages, fp.blockages);
  EXPECT_EQ(restored.components.size(), 1);
  EXPECT_NO_THROW(verify_def_roundtrip(fp, restored));
  auto changed              = restored;
  changed.blockages[0].soft = false;
  EXPECT_THROW(verify_def_roundtrip(fp, changed), std::runtime_error);
  changed                        = restored;
  changed.blockages[0].component = "missing";
  EXPECT_THROW(write_def(changed, path.string()), std::invalid_argument);
  changed                               = restored;
  changed.blockages[0].rectangles[0].xl = .5;
  EXPECT_THROW(write_def(changed, path.string()), std::invalid_argument);
  changed                            = restored;
  changed.components[0].place.status = Status::PLACED;
  EXPECT_THROW(placement_exclusions(changed, fp.die), std::invalid_argument);
}
TEST(Physical, PlacementExclusionsClipUnionAndSubtractFixedOccupancy) {
  Floorplan fp;
  fp.blockages = {
      { true, {}, {{-5, -5, 15, 15}, {0, 0, 10, 10}}},
      {false, {},                    {{0, 0, 6, 10}}}
  };
  const auto pieces = placement_exclusions(fp,
                                           {
                                               0,
                                               0,
                                               10,
                                               10
  },
                                           {{2, 2, 8, 8}});
  double     hard = 0, soft = 0;
  for (size_t i = 0; i < pieces.size(); ++i) {
    const auto& r = pieces[i].rect;
    EXPECT_GE(r.xl, 0);
    EXPECT_GE(r.yl, 0);
    EXPECT_LE(r.xh, 10);
    EXPECT_LE(r.yh, 10);
    const auto area                                         = (r.xh - r.xl) * (r.yh - r.yl);
    (pieces[i].kind == Placement_mask::hard ? hard : soft) += area;
    EXPECT_FALSE(r.xl < 8 && r.xh > 2 && r.yl < 8 && r.yh > 2);
    for (size_t j = 0; j < i; ++j) {
      const auto& q = pieces[j].rect;
      EXPECT_FALSE(r.xl < q.xh && r.xh > q.xl && r.yl < q.yh && r.yh > q.yl);
    }
  }
  EXPECT_DOUBLE_EQ(hard, 36);  // 60 minus the 24-DBU² fixed overlap.
  EXPECT_DOUBLE_EQ(soft, 28);
  EXPECT_TRUE(placement_exclusions(fp, {20, 20, 30, 30}).empty());
}
TEST(Physical, LargeExclusionSweepMatchesIndependentRaster) {
  Floorplan fp;
  fp.blockages = {
      {false, {}, std::vector<Rect>(1100, {0, 0, 1, 1})},
      {true, {}, {}}
  };
  for (int i = 0; i < 120; ++i) {
    const double x = (i * 17) % 37 - 3, y = (i * 11) % 29 - 2;
    fp.blockages[i % 2].rectangles.push_back({x, y, x + 1 + i % 5, y + 1 + i % 4});
  }
  std::vector<Rect> fixed;
  for (int i = 0; i < 17; ++i) {
    const double x = (i * 7) % 32, y = (i * 13) % 24;
    fixed.push_back({x, y, x + 3, y + 2});
  }
  const auto pieces        = placement_exclusions(fp, {0, 0, 32, 24}, fixed);
  const auto contains      = [](const Rect& r, double x, double y) { return r.xl <= x && x < r.xh && r.yl <= y && y < r.yh; };
  size_t     expected_area = 0;
  for (double y = .5; y < 24; y += 1) {
    for (double x = .5; x < 32; x += 1) {
      auto expected = Placement_mask::none;
      for (const auto& b : fp.blockages) {
        for (const auto& r : b.rectangles) {
          if (contains(r, x, y) && (expected == Placement_mask::none || !b.soft)) {
            expected = b.soft ? Placement_mask::soft : Placement_mask::hard;
          }
        }
      }
      for (const auto& r : fixed) {
        if (contains(r, x, y)) {
          expected = Placement_mask::none;
        }
      }
      expected_area  += expected != Placement_mask::none;
      size_t matches  = 0;
      for (const auto& p : pieces) {
        if (contains(p.rect, x, y)) {
          ++matches;
          EXPECT_EQ(p.kind, expected);
        }
      }
      EXPECT_EQ(matches, expected == Placement_mask::none ? 0 : 1) << x << ',' << y;
    }
  }
  double area      = 0;
  bool   seen_soft = false;
  for (const auto& p : pieces) {
    EXPECT_GE(p.rect.xl, 0);
    EXPECT_GE(p.rect.yl, 0);
    EXPECT_LE(p.rect.xh, 32);
    EXPECT_LE(p.rect.yh, 24);
    if (p.kind == Placement_mask::soft) {
      seen_soft = true;
    } else {
      EXPECT_FALSE(seen_soft);
    }
    area += (p.rect.xh - p.rect.xl) * (p.rect.yh - p.rect.yl);
  }
  EXPECT_DOUBLE_EQ(area, expected_area);
}
TEST(Physical, LargeSparseFixedOccupancyDoesNotFillWhitespace) {
  Floorplan fp;
  fp.blockages = {
      {false, {}, {}}
  };
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      fp.blockages.front().rectangles.push_back({double(2 * x), double(2 * y), double(2 * x + 1), double(2 * y + 1)});
    }
  }
  const auto pieces = placement_exclusions(fp, {0, 0, 128, 128});
  ASSERT_EQ(pieces.size(), 4096);
  for (const auto& p : pieces) {
    EXPECT_EQ(p.kind, Placement_mask::hard);
    EXPECT_DOUBLE_EQ(p.rect.xh - p.rect.xl, 1);
    EXPECT_DOUBLE_EQ(p.rect.yh - p.rect.yl, 1);
    EXPECT_EQ(static_cast<int>(p.rect.xl) % 2, 0);
    EXPECT_EQ(static_cast<int>(p.rect.yl) % 2, 0);
  }
}
TEST(Physical, UnsupportedBlockagesFailBeforeTheyCanBeDropped) {
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "unsupported_blockage.def";
  for (const std::string body : {"PLACEMENT + PARTIAL 50 RECT ( 0 0 ) ( 100 100 )",
                                 "LAYER met1 RECT ( 0 0 ) ( 100 100 )",
                                 "PLACEMENT POLYGON ( 0 0 ) ( 100 0 ) ( 100 100 )",
                                 "PLACEMENT + COMPONENT absent RECT ( 0 0 ) ( 100 100 )"}) {
    {
      std::ofstream out(path);
      out << "VERSION 5.8 ;\nDESIGN top ;\nUNITS DISTANCE MICRONS 1000 ;\n"
             "DIEAREA ( 0 0 ) ( 1000 1000 ) ;\nBLOCKAGES 1 ;\n- "
          << body << " ;\nEND BLOCKAGES\nEND DESIGN\n";
    }
    EXPECT_ANY_THROW(read_def(path.string())) << body;
  }
}
TEST(Physical, SegmentedRowAdapterPreservesEveryAvailableSiteAndOrientation) {
  Technology tech;
  tech.sites["core"] = {1, 1, true};
  Floorplan fp;
  fp.design = "segmented";
  fp.die    = {0, 0, 10000, 4000};
  fp.rows   = {
      {"r0a", "core",    0,    0,  6, 1000,  Orientation::N},
      {"r0b", "core", 8000,    0,  2, 1000,  Orientation::N},
      {"r1a", "core",    0, 1000,  6, 1000, Orientation::FS},
      {"r1b", "core", 8000, 1000,  2, 1000, Orientation::FS},
      { "r3", "core",    0, 3000, 10, 1000, Orientation::FS}
  };
  fp.blockages = {
      {true, {}, {{9000, 0, 10000, 1000}}}
  };
  const auto adapted = rectangular_placement_frame(fp, tech);
  ASSERT_EQ(adapted.rows.size(), 4);
  ASSERT_EQ(adapted.blockages.size(), 2);
  EXPECT_EQ(adapted.blockages.front(), fp.blockages.front());
  EXPECT_FALSE(adapted.blockages.back().soft);
  ASSERT_EQ(adapted.blockages.back().rectangles.size(), 2);
  for (int row = 0; row < 4; ++row) {
    EXPECT_EQ(adapted.rows[row].x, 0);
    EXPECT_EQ(adapted.rows[row].y, row * 1000);
    EXPECT_EQ(adapted.rows[row].count, 10);
    if (row != 2) {
      EXPECT_EQ(adapted.rows[row].orientation, row % 2 ? Orientation::FS : Orientation::N);
    }
    for (int site = 0; site < 10; ++site) {
      const bool original = std::any_of(fp.rows.begin(), fp.rows.end(), [&](const auto& r) {
        return r.y == row * 1000 && site * 1000 >= r.x && site * 1000 < r.x + r.count * r.step;
      });
      const bool masked
          = std::any_of(adapted.blockages.back().rectangles.begin(), adapted.blockages.back().rectangles.end(), [&](const auto& r) {
              return site * 1000 >= r.xl && site * 1000 < r.xh && row * 1000 >= r.yl && row * 1000 < r.yh;
            });
      EXPECT_EQ(original, !masked) << row << ':' << site;
    }
  }
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "segmented_stage.def";
  write_def(adapted, path.string());
  EXPECT_NO_THROW(verify_def_roundtrip(adapted, read_def(path.string())));
  EXPECT_EQ(fp.rows.size(), 5);  // original input was never rewritten
}
TEST(Physical, SegmentedRowAdapterRejectsAmbiguousOrOffGridFrames) {
  Technology tech;
  tech.sites["core"] = {1, 1, true};
  Floorplan fp;
  fp.die  = {0, 0, 10000, 4000};
  fp.rows = {
      {"r0", "core", 0,    0, 10, 1000,  Orientation::N},
      {"r1", "core", 0, 1000, 10, 1000, Orientation::FS}
  };
  for (int mode = 0; mode < 4; ++mode) {
    auto changed = fp;
    if (mode == 0) {
      changed.rows[1].y = 500;
    }
    if (mode == 1) {
      changed.rows[1].step = 500;
    }
    if (mode == 2) {
      changed.rows[1].orientation = Orientation::E;
    }
    if (mode == 3) {
      changed.rows.push_back({"overlap", "core", 1000, 0, 2, 1000, Orientation::N});
    }
    EXPECT_THROW(rectangular_placement_frame(changed, tech), std::invalid_argument) << mode;
  }
}
TEST(Physical, DefFixedWiresSpecialNetsAndViasRoundtrip) {
  const auto dir   = std::filesystem::path(std::getenv("TEST_TMPDIR"));
  const auto input = dir / "fixed_input.def";
  {
    std::ofstream out(input);
    out << R"(VERSION 5.8 ;
DESIGN wires ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
VIAS 1 ;
- via12 + RECT M1 ( -10 -20 ) ( 10 20 )
        + RECT V1 ( -5 -5 ) ( 5 5 )
        + RECT M2 ( -20 -10 ) ( 20 10 ) ;
END VIAS
COMPONENTS 2 ;
- u0 inv + FIXED ( 100 100 ) N ;
- u1 inv + FIXED ( 500 100 ) FS ;
END COMPONENTS
PINS 1 ;
- VDD + NET VDD + DIRECTION INOUT + USE POWER
  + LAYER M1 ( -10 -10 ) ( 10 10 ) + FIXED ( 100 0 ) N ;
END PINS
SPECIALNETS 1 ;
- VDD ( * VDD ) ( PIN VDD ) + USE POWER
  + ROUTED M1 20 + SHAPE FOLLOWPIN ( 0 0 0 ) ( 10000 * 0 )
  NEW M1 20 ( 100 0 ) via12 ;
END SPECIALNETS
NETS 1 ;
- n ( u0 Y ) ( u1 A ) + USE SIGNAL
  + FIXED M1 ( 110 120 ) via12
  NEW M2 ( 110 120 0 ) ( 510 * 0 ) via12 FS ;
END NETS
END DESIGN
)";
  }
  const auto fp = read_def(input.string());
  ASSERT_EQ(fp.nets.size(), 1);
  ASSERT_EQ(fp.special_nets.size(), 1);
  ASSERT_EQ(fp.vias.size(), 1);
  ASSERT_EQ(fp.terminals.size(), 1);
  EXPECT_EQ(fp.terminals[0].use, "POWER");
  ASSERT_EQ(fp.nets[0].wires.size(), 1);
  EXPECT_EQ(fp.nets[0].wires[0].type, "FIXED");
  ASSERT_EQ(fp.nets[0].wires[0].paths.size(), 2);
  const auto& path = fp.nets[0].wires[0].paths[1];
  ASSERT_EQ(path.steps.size(), 3);
  EXPECT_EQ(std::get<Route_point>(path.steps[1]).y, 120);
  EXPECT_EQ(std::get<Route_point>(path.steps[1]).extension, 0);
  EXPECT_EQ(std::get<Route_via>(path.steps[2]).orientation, Orientation::FS);
  EXPECT_EQ(fp.special_nets[0].wires[0].paths[0].shape, "FOLLOWPIN");
  const auto output = dir / "fixed_output.def";
  write_def(fp, output.string());
  EXPECT_NO_THROW(verify_def_roundtrip(fp, read_def(output.string())));
  auto broken = fp;
  std::get<Route_point>(broken.nets[0].wires[0].paths[1].steps[1]).x++;
  EXPECT_THROW(verify_def_roundtrip(fp, broken), std::runtime_error);
  broken = fp;
  broken.vias[0].shapes[0].rect.xh++;
  EXPECT_THROW(verify_def_roundtrip(fp, broken), std::runtime_error);
  broken = fp;
  broken.special_nets[0].wires.clear();
  EXPECT_THROW(verify_def_roundtrip(fp, broken), std::runtime_error);
}
TEST(Physical, DefUnsupportedRouteMustNotDisappear) {
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "unsupported_route.def";
  for (const std::string route : {"+ FIXED M1 MASK 1 ( 0 0 ) ( 100 0 )", "+ FIXED M1 TAPER ( 0 0 ) ( 100 0 )"}) {
    {
      std::ofstream out(path);
      out << "VERSION 5.8 ;\nDESIGN top ;\nUNITS DISTANCE MICRONS 1000 ;\n"
             "NETS 1 ;\n- n "
          << route << " ;\nEND NETS\nEND DESIGN\n";
    }
    EXPECT_THROW(read_def(path.string()), std::runtime_error) << route;
  }
}
TEST(Physical, SignalPatchRoundtrip) {
  const auto dir   = std::filesystem::path(std::getenv("TEST_TMPDIR"));
  const auto input = dir / "patch.def";
  {
    std::ofstream out(input);
    out << "VERSION 5.8 ;\nDESIGN patch ;\nUNITS DISTANCE MICRONS 1000 ;\n"
           "NETS 1 ;\n- n + FIXED M1 ( 100 200 ) RECT ( -12 -38 12 -27 )"
           " ( 300 200 ) ;\nEND NETS\nEND DESIGN\n";
  }
  const auto  fp    = read_def(input.string());
  const auto& steps = fp.nets.at(0).wires.at(0).paths.at(0).steps;
  ASSERT_EQ(steps.size(), 3);
  EXPECT_EQ(std::get<Route_rect>(steps[1]), (Route_rect{-12, -38, 12, -27}));
  const auto output = dir / "patch_out.def";
  write_def(fp, output.string());
  EXPECT_NO_THROW(verify_def_roundtrip(fp, read_def(output.string())));
  auto invalid                               = fp;
  invalid.nets[0].wires[0].paths[0].steps[1] = Route_rect{1, 0, 1, 10};
  EXPECT_THROW(write_def(invalid, output.string()), std::runtime_error);
  invalid = fp;
  invalid.nets[0].wires[0].paths[0].steps.erase(invalid.nets[0].wires[0].paths[0].steps.begin());
  EXPECT_THROW(write_def(invalid, output.string()), std::runtime_error);
}
TEST(Physical, DefGeneratedViaRoundtrip) {
  const auto dir  = std::filesystem::path(std::getenv("TEST_TMPDIR"));
  const auto path = dir / "generated_via.def";
  {
    std::ofstream out(path);
    out << R"(VERSION 5.8 ;
DESIGN generated ;
UNITS DISTANCE MICRONS 1000 ;
VIAS 1 ;
- array12 + VIARULE M2_M1 + CUTSIZE 18 18 + LAYERS M1 V1 M2
  + CUTSPACING 18 18 + ENCLOSURE 0 0 2 0 + ROWCOL 2 3
  + ORIGIN 4 -5 + OFFSET 1 2 3 4 ;
END VIAS
END DESIGN
)";
  }
  const auto fp = read_def(path.string());
  ASSERT_EQ(fp.vias.size(), 1);
  ASSERT_TRUE(fp.vias[0].generated);
  EXPECT_EQ(fp.vias[0].generated->rule, "M2_M1");
  EXPECT_EQ(fp.vias[0].generated->row_col, (std::array<int64_t, 2>{2, 3}));
  const auto output = dir / "generated_via_out.def";
  write_def(fp, output.string());
  EXPECT_NO_THROW(verify_def_roundtrip(fp, read_def(output.string())));
}
TEST(Physical, DefRejectsMalformedCustomWiresBeforeOpeningOutput) {
  Floorplan fp;
  fp.design = "bad_wire";
  Net net;
  net.name = "n";
  Wire       wire;
  Route_path route;
  route.layer = "M2";
  route.steps = {Route_point{0, 0, {}}, Route_point{100, 100, {}}};
  wire.paths.push_back(route);
  net.wires.push_back(wire);
  fp.nets.push_back(net);
  const auto output = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "invalid_custom.def";
  {
    std::ofstream out(output);
    out << "preserved";
  }
  EXPECT_THROW(write_def(fp, output.string()), std::runtime_error);
  std::ifstream in(output);
  std::string   text;
  in >> text;
  EXPECT_EQ(text, "preserved");
  fp.nets[0].wires[0].paths[0].steps = {Route_via{"v12"}};
  EXPECT_THROW(write_def(fp, output.string()), std::runtime_error);
  fp.nets[0].wires[0].paths.clear();
  EXPECT_THROW(write_def(fp, output.string()), std::runtime_error);
  fp.nets[0].wires[0]                = wire;
  fp.nets[0].wires[0].paths[0].steps = {Route_point{0, 0, {}}, Route_point{100, 0, {}}};
  fp.special_nets                    = std::move(fp.nets);
  EXPECT_THROW(write_def(fp, output.string()), std::runtime_error);  // special wires need explicit width
}
TEST(Physical, ViaOnlySpecialPathHasZeroWidth) {
  Floorplan fp;
  fp.design = "pg";
  fp.die    = {0, 0, 1000, 1000};
  Via via;
  via.name   = "v12";
  via.shapes = {{"M1", {-10, -10, 10, 10}}, {"V1", {-5, -5, 5, 5}}, {"M2", {-10, -10, 10, 10}}};
  fp.vias.push_back(via);
  Route_path path;
  path.layer = "M1";
  path.width = 0;
  path.shape = "STRIPE";
  path.steps = {Route_point{100, 100, {}}, Route_via{"v12"}};
  Net net;
  net.name = "VDD";
  net.use  = "POWER";
  net.wires.push_back({"ROUTED", {path}});
  fp.special_nets.push_back(net);
  const auto file = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "via_only.def";
  write_def(fp, file.string());
  EXPECT_NO_THROW(verify_def_roundtrip(fp, read_def(file.string())));
  fp.special_nets[0].wires[0].paths[0].steps.push_back(Route_point{200, 100, {}});
  EXPECT_THROW(write_def(fp, file.string()), std::runtime_error);
}
TEST(Physical, PathTaperRuleRoundtripAndRejection) {
  Floorplan fp;
  fp.design                   = "wide";
  fp.die                      = {0, 0, 1000, 1000};
  fp.nondefault_rules["wide"] = {{"M1", 40}, {"M2", 80}};
  Net net;
  net.name = "n";
  net.wires.push_back({"FIXED", {{"M1", {}, {}, {Route_point{100, 100, 0}, Route_point{500, 100, 0}}, "wide"}}});
  fp.nets.push_back(net);
  const auto file = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "taper.def";
  write_def(fp, file.string());
  EXPECT_NO_THROW(verify_def_roundtrip(fp, read_def(file.string())));
  auto changed                           = fp;
  changed.nondefault_rules["wide"]["M1"] = 42;
  EXPECT_THROW(verify_def_roundtrip(fp, changed), std::runtime_error);
  fp.nondefault_rules.clear();
  EXPECT_THROW(write_def(fp, file.string()), std::runtime_error);
  // Unsupported rule constraints must not disappear on import.
  std::ifstream input(file);
  std::string   text((std::istreambuf_iterator<char>(input)), {});
  input.close();
  text.insert(text.find(" + LAYER M1"), " + HARDSPACING");
  {
    std::ofstream output(file);
    output << text;
  }
  EXPECT_THROW(read_def(file.string()), std::runtime_error);
}
}  // namespace livehd::physical
