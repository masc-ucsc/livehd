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
  EXPECT_THROW(legalize_macro_tracks(tech, fp, cells, 0, 0, true), std::runtime_error);
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
