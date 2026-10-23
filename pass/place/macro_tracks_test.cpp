#include "macro_tracks.hpp"

#include "dreamplace/native/placement.hpp"
#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
std::pair<physical::Technology, physical::Floorplan> track_fixture() {
  physical::Technology tech;
  tech.dbu           = 1;
  tech.sites["grid"] = {6, 10, true};
  auto& vertical     = tech.layers["V"];
  vertical.direction = "VERTICAL";
  vertical.width     = 2;
  vertical.pitch_x = vertical.pitch_y = 4;
  vertical.offset_x = vertical.offset_y = 1;
  vertical.has_offset                   = true;
  auto& horizontal                      = tech.layers["H"];
  horizontal.direction                  = "HORIZONTAL";
  horizontal.width                      = 2;
  horizontal.pitch_x = horizontal.pitch_y = 6;
  horizontal.offset_x = horizontal.offset_y = 3;
  horizontal.has_offset                     = true;
  auto& macro                               = tech.macros["ram"];
  macro.klass                               = "BLOCK";
  macro.width                               = 11;
  macro.height                              = 13;
  macro.symmetry_x = macro.symmetry_y = true;
  macro.pins["A"]                     = {"INPUT", "SIGNAL", {{"V", {1, 0, 3, 4}}}};
  macro.pins["B"]                     = {"INPUT", "SIGNAL", {{"V", {5, 0, 7, 4}}}};
  macro.pins["Y"]                     = {"OUTPUT", "SIGNAL", {{"H", {0, 2, 6, 4}}}};
  macro.pins["power"]                 = {"INOUT", "POWER", {{"V", {3, 0, 5, 4}}}};
  auto& core                          = tech.macros["cell"];
  core.klass                          = "CORE";
  core.width                          = 6;
  core.height                         = 10;
  core.site                           = "grid";
  physical::Floorplan fp;
  fp.dbu = 1;
  fp.die = {0, 0, 120, 100};
  for (int row = 0; row < 10; ++row) {
    fp.rows.push_back(
        {"r" + std::to_string(row), "grid", 0, 10 * row, 20, 6, row % 2 ? physical::Orientation::FS : physical::Orientation::N});
  }
  fp.tracks = {
      {"X", "V", 3, 30, 4},
      {"Y", "H", 2, 17, 6}
  };
  return {tech, fp};
}
physical::Component track_macro(physical::Orientation orientation = physical::Orientation::N) {
  Ann_place p(30, 30, 11, 13);
  p.status      = Ann_place::Status::PLACED;
  p.orientation = orientation;
  return {"ram0", "ram", p};
}
}  // namespace
TEST(Placement, MacroTrackAlignmentUsesOrientedPinsAndImportedTrackPhase) {
  const auto [tech, fp] = track_fixture();
  for (auto orientation :
       {physical::Orientation::N, physical::Orientation::S, physical::Orientation::FN, physical::Orientation::FS}) {
    std::vector<physical::Component> cells{track_macro(orientation)};
    legalize_macro_tracks(tech, fp, cells, 2, 2, true);
    const auto& p = cells.front().place;
    EXPECT_EQ(p.orientation, orientation);
    EXPECT_EQ(p.width, 11);
    EXPECT_EQ(p.height, 13);
    const auto a = physical::orient_point(2, 1, 11, 13, orientation);
    const auto y = physical::orient_point(1, 3, 11, 13, orientation);
    EXPECT_EQ((p.x + int64_t(a.first) - 3) % 4, 0);
    EXPECT_EQ((p.y + int64_t(y.second) - 2) % 6, 0);
    EXPECT_GE(p.x, 2);
    EXPECT_GE(p.y, 2);
  }
}
TEST(Placement, RigidMacroTrackAlignmentUsesOneTranslationAcrossMastersAndOrientations) {
  auto [tech, fp]             = track_fixture();
  tech.macros.at("ram").width = 12;
  tech.macros.emplace("different_master", tech.macros.at("ram"));
  auto a = track_macro(), b = track_macro(physical::Orientation::FN);
  a.place.width = b.place.width  = 12;
  b.name                         = "other_hierarchy/ram1";
  b.macro                        = "different_master";
  b.place.x                     += 16;
  std::vector<physical::Component> cells{a, b};
  ASSERT_NO_THROW(legalize_rigid_macro_tracks(tech, fp, cells, {1, 1}, 2, 2, true));
  EXPECT_EQ(cells[1].place.x - cells[0].place.x, 16);
  EXPECT_EQ(cells[1].place.y, cells[0].place.y);
  EXPECT_EQ(cells[0].place.orientation, a.place.orientation);
  EXPECT_EQ(cells[1].place.orientation, b.place.orientation);
  EXPECT_NE(cells[0].place.x, a.place.x);
  for (auto& cell : cells) {
    cell.place.status = physical::Status::FIXED;
  }
  EXPECT_NO_THROW(legalize_macro_tracks(tech, fp, cells, 2, 2, false));
  // An incompatible frozen internal pitch must reject rather than splitting
  // the block to put individual pins on different track phases.
  cells              = {a, b};
  cells[1].place.x  -= 1;
  const auto before  = cells;
  EXPECT_THROW(legalize_rigid_macro_tracks(tech, fp, cells, {1, 1}, 2, 2, true), std::runtime_error);
  EXPECT_EQ(cells, before);
}
TEST(Placement, RigidMacroTrackAlignmentPreservesEachMembersSupportedPins) {
  auto [tech, fp]             = track_fixture();
  fp.tracks.front().start     = 0;
  tech.macros.at("ram").width = 12;
  tech.macros.at("ram").pins.clear();
  const auto pin = [](std::initializer_list<int> phases) {
    physical::Pin p{"INPUT", "SIGNAL", {}};
    for (const int phase : phases) {
      const double center = 4 - phase;
      p.shapes.push_back({
          "V",
          {center - .4, 1, center + .4, 2}
      });
    }
    return p;
  };
  tech.macros.at("ram").pins["A"] = pin({0, 2});
  tech.macros.at("ram").pins["B"] = pin({0, 2});
  tech.macros.emplace("other", tech.macros.at("ram"));
  auto& other = tech.macros.at("other");
  other.pins.clear();
  other.pins["D"]         = pin({0, 1});
  other.pins["E"]         = pin({0, 1});
  other.pins["ignored_F"] = pin({2});
  other.pins["ignored_G"] = pin({2});
  auto a = track_macro(), b = track_macro();
  a.place.width = b.place.width  = 12;
  b.name                         = "ram1";
  b.macro                        = "other";
  b.place.x                     += 18;
  std::vector<physical::Component> cells{a, b};
  // Each standalone abstract selects its supported pins. Combining all raw
  // pins would tie at another phase and silently discard D/E from the union.
  ASSERT_NO_THROW(legalize_rigid_macro_tracks(tech, fp, cells, {1, 1}, 2, 2, true));
  EXPECT_EQ(cells[1].place.x - cells[0].place.x, 18);
  EXPECT_EQ(cells[0].place.x % 4, 2);
  for (auto& cell : cells) {
    cell.place.status = physical::Status::FIXED;
  }
  EXPECT_NO_THROW(legalize_macro_tracks(tech, fp, cells, 2, 2, false));
}
TEST(Placement, RigidTrackPreflightIntersectsMemberPhasesWithoutMovingThem) {
  auto [tech, fp]             = track_fixture();
  tech.macros.at("ram").width = 12;
  tech.macros.emplace("other", tech.macros.at("ram"));
  auto a = track_macro(), b = track_macro(physical::Orientation::FN);
  a.place.width = b.place.width           = 12;
  b.name                                  = "ram1";
  b.macro                                 = "other";
  b.place.x                              += 16;
  auto                             check  = make_rigid_macro_track_checker(tech, fp);
  std::vector<physical::Component> cells{a, b};
  const auto                       before = cells;
  EXPECT_TRUE(check(cells));
  EXPECT_EQ(cells, before);
  // Translation and member ordering do not alter relative phase feasibility.
  for (auto& c : cells) {
    c.place.x += 5;
    c.place.y -= 7;
  }
  std::reverse(cells.begin(), cells.end());
  EXPECT_TRUE(check(cells));
  cells = before;
  --cells[1].place.x;
  EXPECT_FALSE(check(cells));
  EXPECT_EQ(cells[1].place.x, before[1].place.x - 1);
}
TEST(Placement, RigidTrackPreflightCombinesMixedLayerCoverageAcrossAxes) {
  auto [tech, fp] = track_fixture();
  auto& macro     = tech.macros.at("ram");
  macro.pins.clear();
  const auto mixed = [](double x, double y) {
    return physical::Pin{
        "INPUT",
        "SIGNAL",
        {{"V", {x - .4, 0, x + .4, 1}}, {"H", {0, y - .4, 1, y + .4}}}
    };
  };
  macro.pins["P"]      = mixed(2, 2);  // x phase 1 or y phase 0
  macro.pins["Q"]      = mixed(1, 1);  // x phase 2 or y phase 1
  auto other           = macro;
  other.pins["P"]      = mixed(3, 2);  // x phase 0 or y phase 0
  other.pins["Q"]      = mixed(4, 1);  // x phase 3 or y phase 1
  tech.macros["other"] = other;
  auto a = track_macro(), b = track_macro();
  b.macro     = "other";
  b.name      = "ram1";
  b.place.x  += 16;
  auto check  = make_rigid_macro_track_checker(tech, fp);
  // Each macro alone can serve both mixed pins. Their union has no shared
  // X/Y phase pair, despite both independent single-axis domains being full.
  EXPECT_TRUE(check({a}));
  EXPECT_TRUE(check({b}));
  EXPECT_FALSE(check({a, b}));
  // Give the second macro the same coverage: the joint domain is now nonempty.
  b.macro = "ram";
  EXPECT_TRUE(check({a, b}));
}
TEST(Placement, MacroTrackAlignmentRespectsFixedGeometryAndReportsConflicts) {
  auto [tech, fp]       = track_fixture();
  auto obstacle         = physical::Component{"obstacle", "cell", Ann_place(24, 20, 6, 10)};
  obstacle.place.status = Ann_place::Status::FIXED;
  std::vector<physical::Component> cells{track_macro(), obstacle};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ(cells.back(), obstacle);
  EXPECT_TRUE(cells.front().place.x - 2 >= 30 || cells.front().place.y - 2 >= 30);
  auto bad         = track_macro();
  bad.place.status = Ann_place::Status::FIXED;
  cells            = {bad};
  EXPECT_THROW(legalize_macro_tracks(tech, fp, cells, 0, 0, true), std::runtime_error);
  EXPECT_EQ(cells.front(), bad);
  cells                                  = {track_macro()};
  tech.macros.at("ram").pins["conflict"] = {"INPUT", "SIGNAL", {{"V", {3, 0, 5, 4}}}};
  // No phase serves all three V pins: keep the phase serving A and B.
  EXPECT_NO_THROW(legalize_macro_tracks(tech, fp, cells, 0, 0, true));
  EXPECT_EQ((cells.front().place.x + 2 - 3) % 4, 0);
  cells = {track_macro()};
  tech.macros.at("ram").pins["conflict"].shapes.push_back({
      "H",
      {0, 2, 6, 4}
  });
  EXPECT_NO_THROW(legalize_macro_tracks(tech, fp, cells, 0, 0, true));
}
TEST(Placement, MacroTrackAlignmentHonorsManufacturingGridAndExplicitLefFallback) {
  auto [tech, fp] = track_fixture();
  fp.tracks.clear();
  std::vector<physical::Component> cells{track_macro()};
  legalize_macro_tracks(tech, fp, cells, 0, 0, true);
  EXPECT_EQ((cells.front().place.x + 2 - 1) % 4, 0);
  EXPECT_EQ(cells.front().place.y % 6, 0);
  tech.layers.at("V").has_offset = tech.layers.at("H").has_offset = false;
  cells                                                           = {track_macro()};
  legalize_macro_tracks(tech, fp, cells, 0, 0, true);
  EXPECT_EQ(cells.front(), track_macro());
  auto fixture            = track_fixture();
  tech                    = fixture.first;
  fp                      = fixture.second;
  tech.manufacturing_grid = 2;
  fp.tracks.front().start = 2;
  fp.tracks.back().start  = 1;
  cells                   = {track_macro()};
  cells.front().place.x = cells.front().place.y = 31;
  legalize_macro_tracks(tech, fp, cells, 0, 0, true);
  EXPECT_EQ(cells.front().place.x % 2, 0);
  EXPECT_EQ(cells.front().place.y % 2, 0);
}
TEST(Placement, MacroTrackHookUsesPhysicalHaloGeometryBeforeCellPacking) {
  namespace dp = DreamPlace::native;
  dp::Design d;
  d.layout      = {0, 0, 24, 12, 2, 2};
  d.num_movable = 2;
  d.node_x      = {8, 8};
  d.node_y      = {4, 4};
  d.node_size_x = {2, 5};
  d.node_size_y = {2, 5};
  d.node_macro  = {0, 1};
  d.node_halo_x = d.node_halo_y = {0, 2};
  d.flat_net2pin_start          = {0, 2};
  d.flat_net2pin                = {0, 1};
  d.pin2node                    = {0, 1};
  d.pin_offset_x = d.pin_offset_y = {0, 0};
  d.net_weights                   = {1};
  d.net_mask                      = {1};
  d.build_maps();
  dp::Options options;
  options.max_iterations = 0;
  options.add_fillers    = false;
  options.bins_x = options.bins_y = 2;
  unsigned calls                  = 0;
  options.after_macro_legalize    = [&](dp::Design& physical) {
    ++calls;
    EXPECT_EQ(physical.node_size_x[1], 5);
    EXPECT_EQ(physical.node_size_y[1], 5);
    physical.node_x[1] = 9;
    physical.node_y[1] = 3;
  };
  const auto placed = dp::place(d, options);
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(placed.legal);
  EXPECT_EQ(d.node_x[1], 9);
  EXPECT_EQ(d.node_y[1], 3);
  EXPECT_TRUE(dp::is_legal(d));
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Placement, MacroMixedAccessKeepsDistantUsefulPhaseAfterCandidateTruncation) {
  auto [tech, fp] = track_fixture();
  auto& macro     = tech.macros.at("ram");
  macro.pins.clear();
  macro.pins["optional"] = {
      "INPUT",
      "SIGNAL",
      {{"V", {1, 0, 3, 4}}, {"H", {0, 2, 6, 4}}}
  };
  fp.tracks = {
      {"X", "V", 3, 1, 400},
      {"Y", "H", 2, 1, 600}
  };
  std::vector<physical::Component> cells{track_macro()};
  cells.front().place.x = cells.front().place.y = 80;
  legalize_macro_tracks(tech, fp, cells, 0, 0, true);
  EXPECT_EQ(cells.front().place.x, 1);
  EXPECT_EQ(cells.front().place.y, 80);
}
TEST(Placement, NativeMacroLegalityUsesExactRectanglesWithOffRowOrigins) {
  namespace dp = DreamPlace::native;
  dp::Design d;
  d.layout             = {0, 0, 20, 10, 2, 2};
  d.num_movable        = 2;
  d.node_x             = {2, 2, 16};
  d.node_y             = {1, 3.1, 0};
  d.node_size_x        = {5, 5, 2};
  d.node_size_y        = {2, 2, 2};
  d.node_macro         = {1, 1, 0};
  d.flat_net2pin_start = {0};
  d.build_maps();
  EXPECT_TRUE(dp::is_legal(d));
  EXPECT_TRUE(dp::detail::kernel_is_legal(d));
  d.node_y[1] = 2.5;
  EXPECT_FALSE(dp::is_legal(d));
  d.node_y[1] = 3.1;
  d.node_x[2] = 2;
  d.node_y[2] = 4;
  EXPECT_FALSE(dp::is_legal(d));
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Placement, CompactMacroStripMovesCoherentlyOntoTracksOrFailsAtomically) {
  auto [tech, fp] = track_fixture();
  fp.rows.resize(2);
  for (auto& row : fp.rows) {
    row.count = 17;
  }
  std::vector<physical::Component> cells;
  for (unsigned i = 0; i < 6; ++i) {
    auto c    = track_macro();
    c.name    = "ram" + std::to_string(i);
    c.place.x = 2 + 15 * i;
    c.place.y = 4;
    cells.push_back(c);
  }
  const auto original = cells;
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  for (unsigned i = 0; i < cells.size(); ++i) {
    EXPECT_EQ(cells[i].place.x, 5 + 16 * i);
    EXPECT_EQ(cells[i].place.y, 5);
    EXPECT_EQ(cells[i].place.orientation, original[i].place.orientation);
    EXPECT_EQ(cells[i].place.width, original[i].place.width);
    EXPECT_EQ(cells[i].place.height, original[i].place.height);
  }
  cells = original;
  for (auto& row : fp.rows) {
    row.count = 15;
  }
  EXPECT_THROW(legalize_macro_tracks(tech, fp, cells, 2, 2, true), std::runtime_error);
  EXPECT_EQ(cells, original);
}
TEST(Placement, OneRowMacroRemainsAnExactObstacleThroughAllDetailedKernels) {
  namespace dp = DreamPlace::native;
  dp::Design d;
  d.layout             = {0, 0, 24, 12, 2, 2};
  d.num_movable        = 3;
  d.node_x             = {8, 8, 8};
  d.node_y             = {4, 4, 4};
  d.node_size_x        = {2, 5, 2};
  d.node_size_y        = {2, 2, 2};
  d.node_macro         = {0, 1, 0};
  d.flat_net2pin_start = {0, 3};
  d.flat_net2pin       = {0, 1, 2};
  d.pin2node           = {0, 1, 2};
  d.pin_offset_x = d.pin_offset_y = {0, 0, 0};
  d.net_weights                   = {1};
  d.net_mask                      = {1};
  d.build_maps();
  dp::Options options;
  options.max_iterations = 0;
  options.add_fillers    = false;
  options.bins_x = options.bins_y = 2;
  options.after_macro_legalize    = [](dp::Design& frame) {
    frame.node_x[1] = 9;
    frame.node_y[1] = 3;
  };
  const auto result = dp::place(d, options);
  EXPECT_TRUE(result.legal);
  EXPECT_EQ(d.node_x[1], 9);
  EXPECT_EQ(d.node_y[1], 3);
  ASSERT_EQ(result.detailed_history.size(), 3);
  for (const auto& sample : result.detailed_history) {
    EXPECT_TRUE(sample.macros_preserved);
  }
}
TEST(Placement, CoherentRecoveryCannotBypassALaterFixedMacroPinFailure) {
  auto [tech, fp] = track_fixture();
  fp.rows.resize(3);
  for (auto& row : fp.rows) {
    row.count = 17;
  }
  auto mini   = tech.macros.at("ram");
  mini.width  = 3;
  mini.height = 1;
  mini.pins.clear();
  mini.pins["P"] = {"INPUT", "SIGNAL", {{"V", {0, 0, 2, 1}}}};
  tech.macros.emplace("mini", mini);
  std::vector<physical::Component> cells;
  for (unsigned i = 0; i < 6; ++i) {
    auto c    = track_macro();
    c.name    = "ram" + std::to_string(i);
    c.place.x = 2 + 15 * i;
    c.place.y = 4;
    cells.push_back(c);
  }
  auto immutable         = track_macro();
  immutable.name         = "fixed_late";
  immutable.macro        = "mini";
  immutable.place.x      = 99;
  immutable.place.y      = 29;
  immutable.place.width  = 3;
  immutable.place.height = 1;
  immutable.place.status = physical::Status::FIXED;
  cells.push_back(immutable);
  const auto original = cells;
  try {
    legalize_macro_tracks(tech, fp, cells, 2, 2, true);
    FAIL() << "Expected diagnostic for the fixed pin outside all routing tracks";
  } catch (const std::runtime_error& failure) {
    EXPECT_NE(std::string(failure.what()).find("fixed_late:P"), std::string::npos);
  }
  EXPECT_EQ(cells, original);
  cells.back().place.x = 98;
  const auto fixed     = cells.back();
  EXPECT_NO_THROW(legalize_macro_tracks(tech, fp, cells, 2, 2, true));
  EXPECT_EQ(cells.back(), fixed);
}
}  // namespace livehd::placement
