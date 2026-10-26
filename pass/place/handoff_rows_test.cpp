#include "handoff_rows.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
namespace ph = physical;
std::pair<ph::Technology, ph::Floorplan> fixture() {
  ph::Technology tech;
  tech.sites["s"]           = {.01, .01, true};
  tech.macros["ram"].klass  = "BLOCK";
  tech.macros["ram"].width  = .020;
  tech.macros["ram"].height = .010;
  tech.macros["cell"].klass = "CORE";
  tech.macros["cell"].width = tech.macros["cell"].height = .010;
  ph::Floorplan fp;
  for (int i = 0; i < 3; ++i) {
    fp.rows.push_back({"r" + std::to_string(i), "s", 0, i * 10, 10, 10, ph::Orientation::N});
  }
  Ann_place place(35, 10, 20, 10);
  place.status = ph::Status::FIXED;
  fp.components.push_back({"m", "ram", place});
  return {tech, fp};
}
}  // namespace
TEST(HandoffRows, CutsOutwardToSitesAndKeepsBoundaryTouchingRows) {
  const auto [tech, fp] = fixture();
  const auto rows       = cut_macro_rows(fp, tech, 2, 0, 10);
  ASSERT_EQ(rows.size(), 4);
  EXPECT_EQ(rows[0], fp.rows[0]);
  EXPECT_EQ(rows[1], (ph::Row{"r1_1", "s", 0, 10, 3, 10, ph::Orientation::N}));
  EXPECT_EQ(rows[2], (ph::Row{"r1_2", "s", 60, 10, 4, 10, ph::Orientation::N}));
  EXPECT_EQ(rows[3], fp.rows[2]);
}
TEST(HandoffRows, OverlappingMacrosAndMinimumSegments) {
  auto [tech, fp] = fixture();
  auto other      = fp.components[0];
  other.name      = "other";
  other.place.x   = 55;
  fp.components.push_back(other);
  const auto rows = cut_macro_rows(fp, tech, 2, 0, 10);
  ASSERT_EQ(rows.size(), 3);
  EXPECT_EQ(rows[1], (ph::Row{"r1_1", "s", 0, 10, 3, 10, ph::Orientation::N}));
}
TEST(HandoffRows, FixedCellsProtectRowsAndUnplacedMacrosDoNotCut) {
  auto [tech, fp] = fixture();
  Ann_place place(0, 10, 10, 10);
  place.status = ph::Status::LOCKED;
  fp.components.push_back({"fixed", "cell", place});
  EXPECT_EQ(cut_macro_rows(fp, tech, 2, 0, 10), fp.rows);
  fp.components.pop_back();
  fp.components[0].place.status = ph::Status::UNPLACED;
  EXPECT_EQ(cut_macro_rows(fp, tech, 2, 0, 10), fp.rows);
}
TEST(HandoffRows, EndcapCornerStepAndRotatedMacro) {
  auto [tech, fp]          = fixture();
  tech.macros["ram"].width = .005;
  fp.components[0].place.x = 0;
  auto rows                = cut_macro_rows(fp, tech, 0, 0, 30);
  ASSERT_EQ(rows.size(), 3);
  EXPECT_EQ(rows[1].x, 20);
  EXPECT_EQ(rows[1].count, 8);
  tech.macros["ram"].width           = .020;
  fp.components[0].place.x           = 35;
  fp.components[0].place.orientation = ph::Orientation::W;
  rows                               = cut_macro_rows(fp, tech, 0, 0, 0);
  EXPECT_EQ(rows.size(), 5);  // rotated macro intersects two rows
}
TEST(HandoffRows, Asap7ReferenceFrame) {
  ph::Technology tech;
  tech.sites["asap7sc7p5t"] = {.054, .270, true};
  auto& master              = tech.macros["memory"];
  master.klass              = "BLOCK";
  master.width              = 10;
  master.height             = 5;
  ph::Floorplan fp;
  for (int i = 0; i < 60; ++i) {
    fp.rows.push_back(
        {"ROW_" + std::to_string(i), "asap7sc7p5t", 0, i * 270, 1500, 54, i % 2 == 0 ? ph::Orientation::N : ph::Orientation::FS});
  }
  Ann_place p(20133, 5130, 10000, 5000);
  p.status = ph::Status::FIXED;
  fp.components.push_back({"ram", "memory", p});
  const auto rows = cut_macro_rows(fp, tech, 2000, 2000, 216);
  ASSERT_EQ(rows.size(), 94);
  EXPECT_EQ(rows[11].name, "ROW_11_1");
  EXPECT_EQ(rows[11].count, 335);
  EXPECT_EQ(rows[12].name, "ROW_11_2");
  EXPECT_EQ(rows[12].x, 32184);
  EXPECT_EQ(rows[12].count, 904);
}
TEST(HandoffRows, InvalidDimensionsDoNotOverflow) {
  auto [tech, fp] = fixture();
  EXPECT_THROW(cut_macro_rows(fp, tech, -1, 0, 0), std::invalid_argument);
  EXPECT_THROW(cut_macro_rows(fp, tech, 0, 0, std::numeric_limits<int64_t>::max()), std::runtime_error);
  fp.components[0].place.x = std::numeric_limits<int64_t>::max();
  EXPECT_THROW(cut_macro_rows(fp, tech, 0, 0, 0), std::runtime_error);
}
TEST(HandoffRows, EndcapsMatchRowEdgesAndMirrorRightSide) {
  auto [tech, fp] = fixture();
  auto& tap       = tech.macros["tap"];
  tap.site        = "s";
  tap.width       = .020;
  tap.height      = .010;
  tap.symmetry_x = tap.symmetry_y = true;
  fp.rows[1].orientation          = ph::Orientation::FS;
  fp.rows                         = cut_macro_rows(fp, tech, 2, 0, 20);
  const auto caps                 = row_endcaps(fp, tech, "tap");
  ASSERT_EQ(caps.size(), 6);  // narrow left macro segment removed
  EXPECT_EQ(caps[0].place.x, 0);
  EXPECT_EQ(caps[1].place.x, 80);
  EXPECT_EQ(caps[1].place.orientation, ph::Orientation::FN);
  EXPECT_EQ(caps[2].place.x, 60);
  EXPECT_EQ(caps[2].place.orientation, ph::Orientation::FS);
  EXPECT_EQ(caps[3].place.orientation, ph::Orientation::S);
  for (const auto& cap : caps) {
    EXPECT_EQ(cap.place.status, ph::Status::LOCKED);
  }
  fp.components.insert(fp.components.end(), caps.begin(), caps.end());
  EXPECT_THROW(row_endcaps(fp, tech, "tap"), std::invalid_argument);
}
TEST(HandoffRows, JoinedRowsHaveNoInternalEndcaps) {
  auto [tech, fp] = fixture();
  auto& tap       = tech.macros["cell"];
  tap.site        = "s";
  tap.symmetry_x = tap.symmetry_y = true;
  fp.rows.resize(1);
  fp.rows.push_back({"extension", "s", 100, 0, 10, 10, ph::Orientation::N});
  const auto caps = row_endcaps(fp, tech, "cell");
  ASSERT_EQ(caps.size(), 2);
  EXPECT_EQ(caps[0].place.x, 0);
  EXPECT_EQ(caps[1].place.x, 190);
  fp.rows[1].x = 90;
  EXPECT_THROW(row_endcaps(fp, tech, "cell"), std::invalid_argument);
}
TEST(HandoffRows, EndcapsRejectUnsupportedAndUndersizedRows) {
  auto [tech, fp]          = fixture();
  tech.macros["cell"].site = "s";
  fp.rows[0].orientation   = ph::Orientation::FS;
  EXPECT_THROW(row_endcaps(fp, tech, "cell"), std::invalid_argument);
  fp.rows[0].orientation = ph::Orientation::N;
  fp.rows[0].count       = 1;
  EXPECT_THROW(row_endcaps(fp, tech, "cell"), std::invalid_argument);
  fp.rows[0].count = 10;
  fp.rows[1].y     = 5;
  EXPECT_THROW(row_endcaps(fp, tech, "cell"), std::invalid_argument);
}

TEST(HandoffRows, TapPitchStaggersInteriorRowsAndDoublesBoundaryDensity) {
  auto [tech, fp] = fixture();
  fp.components.clear();
  auto& tap                = tech.macros["tap"];
  tap.site                 = "s";
  tap.width                = .020;
  tap.height               = .010;
  tap.symmetry_x           = true;
  tech.macros["cell"].site = "s";
  fp.rows[1].orientation   = ph::Orientation::FS;
  const auto taps          = row_taps(fp, tech, "tap", 40);
  ASSERT_EQ(taps.size(), 5);
  EXPECT_EQ(taps[0].place.x, 40);
  EXPECT_EQ(taps[1].place.x, 80);
  EXPECT_EQ(taps[2].place.x, 80);
  EXPECT_EQ(taps[2].place.y, 10);
  EXPECT_EQ(taps[2].place.orientation, ph::Orientation::FS);
  EXPECT_EQ(taps[3].place.x, 40);
  EXPECT_EQ(taps[4].place.x, 80);
  for (const auto& inst : taps) {
    EXPECT_EQ(inst.place.status, ph::Status::LOCKED);
  }
  fp.rows[1].orientation = ph::Orientation::N;
  EXPECT_EQ(row_taps(fp, tech, "tap", 40)[2].place.x, 40);
}
TEST(HandoffRows, TapsRelocateAtFixedObstaclesButIgnoreMovableCells) {
  auto [tech, fp] = fixture();
  fp.components.clear();
  fp.rows.resize(1);
  auto& tap                = tech.macros["tap"];
  tap.site                 = "s";
  tap.width                = .020;
  tap.height               = .010;
  tech.macros["cell"].site = "s";
  Ann_place p(50, 0, 10, 10);
  p.status = ph::Status::FIXED;
  fp.components.push_back({"fixed", "cell", p});
  auto taps = row_taps(fp, tech, "tap", 40);
  ASSERT_EQ(taps.size(), 2);
  EXPECT_EQ(taps[0].place.x, 30);
  EXPECT_EQ(taps[1].place.x, 70);
  fp.components[0].place.status = ph::Status::PLACED;
  taps                          = row_taps(fp, tech, "tap", 40);
  EXPECT_EQ(taps[0].place.x, 40);
  EXPECT_EQ(taps[1].place.x, 80);
  taps[0].place.status = ph::Status::PLACED;
  fp.components.push_back(taps[0]);
  EXPECT_THROW(row_taps(fp, tech, "tap", 40), std::invalid_argument);
}
TEST(HandoffRows, TapGapPolicyDependsOnAvailableOneSiteMaster) {
  auto [tech, fp] = fixture();
  fp.components.clear();
  fp.rows.resize(1);
  auto& tap  = tech.macros["tap"];
  tap.site   = "s";
  tap.width  = .020;
  tap.height = .010;
  Ann_place p(70, 0, 20, 10);
  p.status = ph::Status::FIXED;
  fp.components.push_back({"fixed", "tap", p});
  auto taps = row_taps(fp, tech, "tap", 40);
  ASSERT_EQ(taps.size(), 1);
  EXPECT_EQ(taps[0].place.x, 50);  // one-site gap closes against obstruction
  tech.macros["cell"].site = "s";
  taps                     = row_taps(fp, tech, "tap", 40);
  EXPECT_EQ(taps[0].place.x, 40);  // one-site gap is fillable
}
TEST(HandoffRows, TapsRejectZeroPitchAndUnsupportedFrames) {
  auto [tech, fp]          = fixture();
  tech.macros["cell"].site = "s";
  EXPECT_THROW(row_taps(fp, tech, "cell", 0), std::invalid_argument);
  EXPECT_THROW(row_taps(fp, tech, "cell", 9), std::invalid_argument);
  fp.rows[0].orientation = ph::Orientation::S;
  EXPECT_THROW(row_taps(fp, tech, "cell", 40), std::invalid_argument);
}
TEST(HandoffRows, TapDensityIncludesHorizontalEdgesOfMacroHoles) {
  auto [tech, fp] = fixture();
  fp.components.clear();
  fp.rows.clear();
  auto& tap                = tech.macros["tap"];
  tap.site                 = "s";
  tap.width                = .020;
  tap.height               = .010;
  tap.symmetry_x           = true;
  tech.macros["cell"].site = "s";
  for (int i = 0; i < 5; ++i) {
    fp.rows.push_back({"r" + std::to_string(i), "s", 0, i * 10, 20, 10, i % 2 == 0 ? ph::Orientation::N : ph::Orientation::FS});
  }
  fp.rows[2].count = 8;
  fp.rows.push_back({"r2_right", "s", 120, 20, 8, 10, ph::Orientation::N});
  const auto taps = row_taps(fp, tech, "tap", 40);
  ASSERT_EQ(taps.size(), 18);
  std::vector<int64_t> above_hole;
  for (const auto& tap_inst : taps) {
    if (tap_inst.place.y == 30) {
      above_hole.push_back(tap_inst.place.x);
    }
  }
  EXPECT_EQ(above_hole, (std::vector<int64_t>{40, 80, 120, 160}));
}
TEST(HandoffRows, RepeaterSitesRespectCellsCutsOrientationAndAtomicFailure) {
  auto [tech, fp]                = fixture();
  fp.die                         = {0, 0, 100, 30};
  tech.macros["cell"].site       = "s";
  tech.macros["cell"].symmetry_x = true;
  fp.rows[1].orientation         = ph::Orientation::FS;
  Ann_place existing(0, 10, 10, 10);
  existing.status = ph::Status::PLACED;
  fp.components.push_back({"existing", "cell", existing});
  fp.rows             = cut_macro_rows(fp, tech, 0, 0, 0);
  const auto before   = fp.components;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  const auto result   = reserve_repeater_sites(fp, tech, {{"b0", "cell", 5, 15}, {"b1", "cell", 5, 15}}, deadline);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->size(), 2);
  // One slot above is as close as the next slot in the occupied row; row
  // order breaks ties deterministically and reserves it for the next request.
  EXPECT_EQ((*result)[0].place.x, 0);
  EXPECT_EQ((*result)[0].place.y, 0);
  EXPECT_EQ((*result)[1].place.x, 10);
  EXPECT_EQ((*result)[1].place.y, 10);
  EXPECT_EQ((*result)[1].place.orientation, ph::Orientation::FS);
  EXPECT_EQ((*result)[1].place.status, ph::Status::FIXED);
  EXPECT_EQ(fp.components, before);
  fp.rows = {{"tiny", "s", 80, 20, 1, 10, ph::Orientation::N}};
  EXPECT_FALSE(reserve_repeater_sites(fp, tech, {{"b0", "cell", 85, 25}, {"b1", "cell", 85, 25}}, deadline));
  EXPECT_EQ(fp.components, before);
  EXPECT_FALSE(reserve_repeater_sites(fp, tech, {{"b0", "cell", 85, 25}}, std::chrono::steady_clock::now()));
}
TEST(HandoffRows, RepeaterSitesRequireEntireWidthAndMasterSymmetry) {
  auto [tech, fp] = fixture();
  fp.die          = {0, 0, 100, 30};
  fp.components.clear();
  fp.rows             = {{"r", "s", 0, 10, 10, 10, ph::Orientation::FS}};
  auto& cell          = tech.macros["cell"];
  cell.site           = "s";
  cell.width          = .020;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  EXPECT_FALSE(reserve_repeater_sites(fp, tech, {{"b", "cell", 100, 15}}, deadline));
  cell.symmetry_x   = true;
  const auto result = reserve_repeater_sites(fp, tech, {{"b", "cell", 100, 15}}, deadline);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->front().place.x, 80);
  Ann_place obstacle(75, 10, 20, 10);
  obstacle.status = ph::Status::FIXED;
  fp.components.push_back({"obstacle", "cell", obstacle});
  const auto blocked = reserve_repeater_sites(fp, tech, {{"b", "cell", 100, 15}}, deadline);
  ASSERT_TRUE(blocked);
  EXPECT_EQ(blocked->front().place.x, 50);  // partial sites at 70 and 90 remain blocked
  EXPECT_THROW(reserve_repeater_sites(fp, tech, {{"obstacle", "cell", 0, 0}}, deadline), std::invalid_argument);
  fp.components.front().place.status = ph::Status::UNPLACED;
  EXPECT_THROW(reserve_repeater_sites(fp, tech, {{"b", "cell", 0, 0}}, deadline), std::invalid_argument);
}
TEST(HandoffRows, RepeaterNearestSitesAgreeWithExhaustiveGrid) {
  auto [tech, fp]                = fixture();
  fp.die                         = {0, 0, 100, 30};
  tech.macros["cell"].site       = "s";
  tech.macros["cell"].symmetry_x = true;
  fp.components.clear();
  std::reverse(fp.rows.begin(), fp.rows.end());
  for (int row = 0; row < 3; ++row) {
    for (int x = 0; x < 10; ++x) {
      if ((x * 7 + row * 11) % 5 != 0) {
        continue;
      }
      Ann_place cell(x * 10, row * 10, 10, 10);
      cell.status = ph::Status::PLACED;
      fp.components.push_back({std::to_string(row) + "_" + std::to_string(x), "cell", cell});
    }
  }
  for (int target = -20; target <= 120; target += 7) {
    const double tx = target, ty = (target + 20) % 47 - 10;
    const auto   result
        = reserve_repeater_sites(fp, tech, {{"new", "cell", tx, ty}}, std::chrono::steady_clock::now() + std::chrono::seconds(2));
    ASSERT_TRUE(result);
    double expected = std::numeric_limits<double>::infinity();
    for (int y = 0; y < 30; y += 10) {
      for (int x = 0; x < 100; x += 10) {
        bool occupied = false;
        for (const auto& cell : fp.components) {
          occupied |= cell.place.x == x && cell.place.y == y;
        }
        if (!occupied) {
          expected = std::min(expected, std::abs(x + 5 - tx) + std::abs(y + 5 - ty));
        }
      }
    }
    const auto& cell = result->front().place;
    EXPECT_DOUBLE_EQ(std::abs(cell.x + 5 - tx) + std::abs(cell.y + 5 - ty), expected);
  }
}
TEST(HandoffRows, RepeaterRowIndexHandlesUnsortedMixedHeights) {
  auto [tech, fp] = fixture();
  fp.die = {0, 0, 100, 30};
  tech.sites["tall"] = {.01, .02, true};
  tech.macros["cell"].site = "s";
  tech.macros["tall_cell"] = tech.macros["cell"];
  tech.macros["tall_cell"].height = .02;
  tech.macros["tall_cell"].site = "tall";
  tech.macros["ram"].width = .01;
  tech.macros["ram"].height = .02;
  fp.components.clear();
  Ann_place obstruction(0, 5, 10, 20);
  obstruction.status = ph::Status::FIXED;
  fp.components.push_back({"m", "ram", obstruction});
  fp.rows = {{"upper", "s", 0, 20, 10, 10, ph::Orientation::N},
             {"lower", "tall", 0, 0, 10, 10, ph::Orientation::N}};
  const auto result = reserve_repeater_sites(fp, tech, {{"a", "tall_cell", 5, 10}, {"b", "cell", 5, 25}},
                                              std::chrono::steady_clock::now() + std::chrono::seconds(2));
  ASSERT_TRUE(result);
  ASSERT_EQ(result->size(), 2);
  EXPECT_EQ((*result)[0].place.x, 10);
  EXPECT_EQ((*result)[0].place.y, 0);
  EXPECT_EQ((*result)[1].place.x, 10);
  EXPECT_EQ((*result)[1].place.y, 20);
}
TEST(HandoffRows, RepeaterSearchSkipsWiresAndClearanceBoundary) {
  auto [tech, fp] = fixture();
  fp.components.clear();
  fp.rows.resize(1);
  fp.die = {0, 0, 100, 10};
  auto& master = tech.macros["cell"];
  master.site = "s";
  master.obstructions.push_back({"M2", {0, 0, .010, .010}});
  auto& layer = tech.layers["M2"];
  layer.width = .002;
  layer.rules.loaded = true;
  layer.rules.spacing.push_back({.004, {}, {}});
  ph::Net net;
  net.name = "wired";
  net.wires.push_back({"FIXED", {{"M2", {}, {}, {ph::Route_point{5, 0, 0}, ph::Route_point{5, 10, 0}}}}});
  fp.nets.push_back(net);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  const auto result = reserve_repeater_sites(fp, tech, {{"a", "cell", 5, 5}, {"b", "cell", 5, 5}}, deadline);
  ASSERT_TRUE(result);
  ASSERT_EQ(result->size(), 2);
  // Wire ends at x=6; clearance touches x=10, so both first sites are blocked.
  EXPECT_EQ((*result)[0].place.x, 20);
  EXPECT_EQ((*result)[1].place.x, 30);
  EXPECT_TRUE(fp.components.empty());
  master.obstructions[0].rect.xl = -.020;
  const auto overhang = reserve_repeater_sites(fp, tech, {{"a", "cell", 5, 5}}, deadline);
  ASSERT_TRUE(overhang);
  EXPECT_EQ((*overhang)[0].place.x, 40);
  master.obstructions[0].rect.xl = 0;
  fp.rows[0].count = 2;
  EXPECT_FALSE(reserve_repeater_sites(fp, tech, {{"a", "cell", 5, 5}}, deadline));
  EXPECT_TRUE(fp.components.empty());
  // Upper-layer wiring does not block a cell without geometry on that layer.
  master.obstructions[0].layer = "M1";
  const auto different_layer = reserve_repeater_sites(fp, tech, {{"a", "cell", 5, 5}}, deadline);
  ASSERT_TRUE(different_layer);
  EXPECT_EQ((*different_layer)[0].place.x, 0);
  master.obstructions[0].layer = "M2";
  // A pin/OBS overhang flips with an FS row; the body itself stays in-row.
  fp.nets[0].wires[0].paths[0].steps = {ph::Route_point{0, 16, 0}, ph::Route_point{100, 16, 0}};
  master.obstructions[0].rect.yh = .025;
  EXPECT_FALSE(reserve_repeater_sites(fp, tech, {{"a", "cell", 5, 5}}, deadline));
  master.symmetry_x = true;
  fp.rows[0].orientation = ph::Orientation::FS;
  const auto flipped = reserve_repeater_sites(fp, tech, {{"a", "cell", 5, 5}}, deadline);
  ASSERT_TRUE(flipped);
  EXPECT_EQ((*flipped)[0].place.x, 0);
  EXPECT_EQ((*flipped)[0].place.orientation, ph::Orientation::FS);
  layer.rules.loaded = false;
  EXPECT_THROW(reserve_repeater_sites(fp, tech, {{"a", "cell", 5, 5}}, deadline), std::invalid_argument);
}
}  // namespace livehd::placement
