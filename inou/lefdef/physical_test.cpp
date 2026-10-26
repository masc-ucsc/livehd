#include "physical.hpp"

#include <algorithm>
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
  ASSERT_EQ(snapshot.snapshot_blockages.size(), 6);
  const auto& soft = snapshot.snapshot_blockages[0];
  EXPECT_TRUE(soft.placement);
  EXPECT_TRUE(soft.soft);
  EXPECT_TRUE(soft.pushdown);
  EXPECT_EQ(soft.component, "u");
  EXPECT_EQ(soft.rectangles,
            (std::vector<Rect>{
                {10, 20, 30, 40}
  }));
  EXPECT_DOUBLE_EQ(snapshot.snapshot_blockages[1].partial_density, 37.5);
  EXPECT_EQ(snapshot.snapshot_blockages[2].rectangles.size(), 2);
  const auto& routing = snapshot.snapshot_blockages[3];
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
  EXPECT_EQ(snapshot.snapshot_blockages[4].design_rule_width, 80);
  EXPECT_TRUE(snapshot.snapshot_blockages[4].slots);
  EXPECT_TRUE(snapshot.snapshot_blockages[5].fills);
  EXPECT_NO_THROW(verify_def_roundtrip(snapshot, snapshot));
  EXPECT_TRUE(snapshot.blockages.empty());
  EXPECT_THROW(placement_exclusions(snapshot, snapshot.die), std::invalid_argument);
  auto altered                            = snapshot;
  altered.snapshot_blockages.front().soft = false;
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
}  // namespace livehd::physical
