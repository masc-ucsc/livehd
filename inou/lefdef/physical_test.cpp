#include "physical.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "gtest/gtest.h"
namespace livehd::physical {
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
  fp.nets.push_back({
      "wire0",
      {{"PIN", "a[0]"}, {"block.gate[0]", "A"}}
  });
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "roundtrip.def";
  write_def(fp, path.string());
  EXPECT_NO_THROW(verify_def_roundtrip(fp, read_def(path.string())));
  EXPECT_NO_THROW(verify_def_roundtrip(fp, read_def_snapshot(path.string())));
  auto broken = read_def(path.string());
  broken.terminals[0].x++;
  EXPECT_THROW(verify_def_roundtrip(fp, broken), std::runtime_error);
}
TEST(Physical, DefTracksAndMasksRoundtrip) {
  Floorplan fp;
  fp.design = "tracks";
  fp.die    = {0, 0, 10000, 10000};
  fp.tracks = {
      {"X", "met2", 230, 20, 460},
      {"Y", "met1", 170, 30, 340, 1, true}
  };
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "tracks.def";
  write_def(fp, path.string());
  const auto restored = read_def(path.string());
  EXPECT_EQ(restored.tracks, fp.tracks);
  EXPECT_NO_THROW(verify_def_roundtrip(fp, restored));
  auto changed = restored;
  ++changed.tracks.front().start;
  EXPECT_THROW(verify_def_roundtrip(fp, changed), std::runtime_error);
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
TEST(Physical, RoutedSnapshotPreservesBlockagesWithoutPlacementAuthority) {
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "snapshot_blockages.def";
  {
    std::ofstream file(path);
    file << R"(VERSION 5.8 ; DESIGN routed ; UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 10000 10000 ) ;
BLOCKAGES 6 ;
- PLACEMENT + SOFT + COMPONENT u + PUSHDOWN RECT ( 10 20 ) ( 30 40 ) ;
- PLACEMENT + PARTIAL 37.5 RECT ( 100 200 ) ( 300 400 ) ;
- PLACEMENT RECT ( 500 600 ) ( 700 800 ) RECT ( 900 1000 ) ( 1100 1200 ) ;
- LAYER met1 + MASK 2 + COMPONENT u + PUSHDOWN + EXCEPTPGNET + SPACING 50
  POLYGON ( 10 10 ) ( 50 10 ) ( 50 50 ) ( 10 50 ) ;
- LAYER met2 + DESIGNRULEWIDTH 80 + SLOTS RECT ( 20 30 ) ( 40 50 ) ;
- LAYER met3 + FILLS RECT ( 60 70 ) ( 80 90 ) ;
END BLOCKAGES
END DESIGN
)";
  }
  EXPECT_THROW(read_def(path.string()), std::runtime_error);
  const auto snapshot = read_def_snapshot(path.string());
  ASSERT_EQ(snapshot.blockages.size(), 6);
  const auto& soft = snapshot.blockages[0];
  EXPECT_TRUE(soft.placement);
  EXPECT_TRUE(soft.soft);
  EXPECT_TRUE(soft.pushdown);
  EXPECT_EQ(soft.component, "u");
  EXPECT_EQ(soft.rectangles,
            (std::vector<Rect>{
                {10, 20, 30, 40}
  }));
  EXPECT_DOUBLE_EQ(snapshot.blockages[1].partial_density, 37.5);
  EXPECT_EQ(snapshot.blockages[2].rectangles.size(), 2);
  const auto& routing = snapshot.blockages[3];
  EXPECT_FALSE(routing.placement);
  EXPECT_EQ(routing.layer, "met1");
  EXPECT_EQ(routing.component, "u");
  EXPECT_EQ(routing.mask, 2);
  EXPECT_EQ(routing.spacing, 50);
  EXPECT_TRUE(routing.except_pg);
  EXPECT_TRUE(routing.pushdown);
  EXPECT_EQ(routing.polygons,
            (std::vector<std::vector<std::pair<int64_t, int64_t>>>{
                {{10, 10}, {50, 10}, {50, 50}, {10, 50}}
  }));
  EXPECT_EQ(snapshot.blockages[4].design_rule_width, 80);
  EXPECT_TRUE(snapshot.blockages[4].slots);
  EXPECT_TRUE(snapshot.blockages[5].fills);
  EXPECT_NO_THROW(verify_def_roundtrip(snapshot, snapshot));
  auto altered                   = snapshot;
  altered.blockages.front().soft = false;
  EXPECT_THROW(verify_def_roundtrip(snapshot, altered), std::runtime_error);
  // Reading a snapshot cannot enable unsupported placement on a later call.
  EXPECT_THROW(read_def(path.string()), std::runtime_error);
  const auto output = path.parent_path() / "preserved_blockage_destination.def";
  {
    std::ofstream file(output);
    file << "preserve this destination";
  }
  EXPECT_THROW(write_def(snapshot, output.string()), std::runtime_error);
  std::ifstream file(output);
  EXPECT_EQ(std::string((std::istreambuf_iterator<char>(file)), {}), "preserve this destination");
}
}  // namespace livehd::physical
