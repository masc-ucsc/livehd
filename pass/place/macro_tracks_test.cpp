#include "macro_tracks.hpp"

#include <algorithm>

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

TEST(Placement, MacroTrackLatticeAgreesWithWhatTheLegalizerDemandsOfAFixedMacro) {
  const auto [tech, fp] = track_fixture();
  const auto on = [](const Macro_track_lattice& l, int64_t x, int64_t y) {
    const auto hit = [](const std::vector<int64_t>& phases, int64_t period, int64_t v) {
      return std::ranges::any_of(phases, [&](int64_t phase) { return ((v - phase) % period + period) % period == 0; });
    };
    return hit(l.phases[0], l.period[0], x) && hit(l.phases[1], l.period[1], y);
  };
  for (auto orientation :
       {physical::Orientation::N, physical::Orientation::S, physical::Orientation::FN, physical::Orientation::FS}) {
    const auto lattice = macro_track_lattice(tech, fp, "ram", orientation);
    ASSERT_TRUE(lattice.has_value());
    EXPECT_EQ(lattice->pins, 3u);  // the power pin does not constrain
    // an origin the legalizer moves a macro to is on the lattice
    std::vector<physical::Component> cells{track_macro(orientation)};
    legalize_macro_tracks(tech, fp, cells, 2, 2, true);
    EXPECT_TRUE(on(*lattice, cells.front().place.x, cells.front().place.y));
    // and the lattice is exactly what a FIXED macro there is not rejected for
    bool any_miss = false, any_hit = false;
    for (int dx = 0; dx < 12; ++dx) {
      for (int dy = 0; dy < 12; ++dy) {
        const auto x = 30 + dx, y = 30 + dy;
        std::vector<physical::Component> fixed{track_macro(orientation)};
        fixed.front().place.x      = x;
        fixed.front().place.y      = y;
        fixed.front().place.status = Ann_place::Status::FIXED;
        if (on(*lattice, x, y)) {
          any_hit = true;
          EXPECT_NO_THROW(legalize_macro_tracks(tech, fp, fixed, 2, 2, true)) << x << "," << y;
        } else {
          any_miss = true;
          EXPECT_THROW(legalize_macro_tracks(tech, fp, fixed, 2, 2, true), std::runtime_error) << x << "," << y;
        }
      }
    }
    EXPECT_TRUE(any_miss);
    EXPECT_TRUE(any_hit);
  }
  // no such master, or no pins that constrain: nothing to check
  EXPECT_FALSE(macro_track_lattice(tech, fp, "nope", physical::Orientation::N).has_value());
  EXPECT_FALSE(macro_track_lattice(tech, fp, "cell", physical::Orientation::N).has_value());
}
TEST(Placement, PinlessMovableMacroKeepsItsHaloInsideTheCore) {
  auto [tech, fp] = track_fixture();
  tech.macros.at("ram").pins.clear();
  auto macro = track_macro();
  macro.place.x = 0;
  std::vector<physical::Component> cells{macro};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ(cells[0].place.x, 2);
  EXPECT_EQ(cells[0].place.y, macro.place.y);
}

TEST(Placement, MacroTrackAlignmentLeavesLandingRowsAtSignalIOs) {
  auto [tech, fp] = track_fixture();
  auto macro = track_macro();
  macro.place.y = 83;  // the aligned top halo leaves less than one complete row
  auto quiet = macro;
  quiet.name = "quiet";
  quiet.place.x = 80;
  for (int x : {32, 33, 34, 35}) {
    fp.terminals.push_back({std::to_string(x), "", "INPUT", x, 100, physical::Orientation::N, physical::Status::FIXED});
  }
  fp.terminals.push_back({"VDD", "", "INOUT", 82, 100, physical::Orientation::N, physical::Status::FIXED, {}, "POWER"});
  std::vector<physical::Component> cells{macro, quiet};
  auto no_io = fp;
  no_io.terminals.clear();
  auto reference = cells;
  legalize_macro_tracks(tech, no_io, reference, 2, 2, true);
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ(cells[1].place, reference[1].place);
  EXPECT_EQ(cells[0].place.x, reference[0].place.x);
  EXPECT_GE(100 - cells[0].place.y - cells[0].place.height - 2, 30);
  EXPECT_EQ((cells[0].place.y - reference[0].place.y) % 6, 0);   // routing-track phase
  EXPECT_EQ((cells[0].place.y - reference[0].place.y) % 10, 0);  // standard-cell row phase
  auto fixed = reference;
  fixed[0].place.status = physical::Status::FIXED;
  legalize_macro_tracks(tech, fp, fixed, 2, 2, true);
  EXPECT_EQ(fixed[0].place.y, reference[0].place.y);
  fp.blockages.push_back({false, "", {{0, 30, 60, 80}}});
  auto blocked = reference;
  legalize_macro_tracks(tech, fp, blocked, 2, 2, true);
  EXPECT_EQ(blocked[0].place, reference[0].place);
}

TEST(Placement, CornerMacroSignalFacesLeaveRoomForBuffersWithoutMovingFixedNeighbors) {
  auto [tech, fp] = track_fixture();
  auto& macro = tech.macros.at("ram");
  macro.pins.clear();
  macro.pins["right0"] = {"INPUT", "SIGNAL", {{"H", {9, 1, 11, 3}}}};
  macro.pins["right1"] = {"INPUT", "SIGNAL", {{"H", {9, 7, 11, 9}}}};
  macro.pins["top"] = {"INPUT", "SIGNAL", {{"V", {5, 11, 7, 13}}}};
  auto corner = track_macro(), neighbor = track_macro();
  corner.place.x = 105;
  corner.place.y = 84;
  neighbor.name = "neighbor";
  neighbor.place.x = 89;
  neighbor.place.y = 84;
  std::vector<physical::Component> cells{corner, neighbor};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ(cells[0].place.x, 69);
  EXPECT_EQ(cells[0].place.y, corner.place.y);
  EXPECT_EQ(cells[1].place.x, 53);
  EXPECT_EQ(cells[1].place.y, neighbor.place.y);
  // On replay the full channel is already present; the preference must not drift.
  const auto saved = cells;
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ(cells, saved);

  // One neighbor needs no vertical metal phase of its own. Preserve its relative phase
  // to the access channel (12 DBU), even though six DBU would put it on the next site.
  auto finer_neighbor = neighbor;
  finer_neighbor.macro = "side";
  tech.macros["side"] = macro;
  tech.macros.at("side").pins.erase("top");
  finer_neighbor.place.x -= 6;
  cells = {corner, finer_neighbor};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_LT(cells[1].place.x, finer_neighbor.place.x);
  EXPECT_EQ((finer_neighbor.place.x - cells[1].place.x) % 12, 0);
  neighbor.place.status = physical::Status::FIXED;
  cells = {corner, neighbor};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ(cells[1].place, neighbor.place);
  EXPECT_EQ(cells[0].place.x, corner.place.x);  // try the other face without leaving a partial x move
  EXPECT_EQ(cells[0].place.y, 54);
  macro.pins["top"].use = "POWER";
  cells = {corner, neighbor};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ(cells[0].place, corner.place);  // a PG pin does not create a trapped signal corner
}

TEST(Placement, DenseSignalFaceLeavesRowsForBuffersAcrossTheBank) {
  auto [tech, fp] = track_fixture();
  auto& m = tech.macros.at("ram");
  m.width = 48;
  m.pins.clear();
  const auto pin = [&](int x) {
    return physical::Pin{"OUTPUT", "SIGNAL", {{"V", {double(x - 1), 0, double(x + 1), 3}}}};
  };
  for (int x : {6, 10, 14, 18, 22, 26, 30, 34, 38}) {
    m.pins.emplace(std::to_string(x), pin(x));
  }
  auto bank = track_macro();
  bank.place = Ann_place(45, 2, 48, 13);
  bank.place.status = physical::Status::PLACED;
  std::vector<physical::Component> cells{bank};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ(cells[0].place.x, bank.place.x);
  EXPECT_EQ(cells[0].place.y, 52);  // Three rows of buffer demand plus the two access rows.
  const auto saved = cells;
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ(cells, saved);
  // Distant pin shapes on only one half of the face, and the PG pin on the other half, do not qualify.
  for (auto& [name, p] : m.pins) {
    if (std::stoi(name) >= 24) {
      p.use = "GROUND";
    }
  }
  cells = {bank};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ(cells[0].place, bank.place);
}

TEST(Placement, ContinuousBoundaryBankOpensRowsWithoutMovingAnchors) {
  auto [tech, fp] = track_fixture();
  tech.macros.at("ram").pins.clear();  // CTS demand is independent of signal pins on this face.
  for (const bool top : {false, true}) {
    std::vector<physical::Component> bank;
    for (int i = 0; i < 5; ++i) {
      auto c = track_macro();
      c.name = "bank" + std::to_string(i);
      c.place.x = 2 + 15 * i;  // each pair of halos meets exactly
      c.place.y = top ? 85 : 2;
      bank.push_back(c);
    }
    bank[2].place.status = physical::Status::FIXED;
    auto trial = bank;
    legalize_macro_tracks(tech, fp, trial, 2, 2, true);
    for (int i : {0, 2, 4}) {
      EXPECT_EQ(trial[i].place, bank[i].place);
    }
    for (int i : {1, 3}) {
      const auto inward = (trial[i].place.y - bank[i].place.y) * (top ? -1 : 1);
      EXPECT_GE(inward, 20);
      EXPECT_EQ(inward % 10, 0);
      EXPECT_EQ(trial[i].place.x, bank[i].place.x);
    }
    const auto saved = trial;
    legalize_macro_tracks(tech, fp, trial, 2, 2, true);
    EXPECT_EQ(trial, saved);

    // An opening wide enough for a complete row breaks the run into short groups.
    for (size_t i = 2; i < bank.size(); ++i) {
      bank[i].place.x += 10;
    }
    trial = bank;
    legalize_macro_tracks(tech, fp, trial, 2, 2, true);
    EXPECT_EQ(trial, bank);
  }
}

TEST(Placement, DeepBoundaryColumnKeepsAnEntranceToItsInnerBanks) {
  auto [tech, fp] = track_fixture();
  auto& ram = tech.macros.at("ram");
  ram.pins.clear();
  for (const bool left : {false, true}) {
    for (int width : {11, 32}) {
      ram.width = width;
      std::vector<physical::Component> bank;
      for (int i = 0; i < 5; ++i) {
        auto c = track_macro();
        c.name = "bank" + std::to_string(i);
        c.place.width = width;
        c.place.x = left ? 3 : 120 - width - 3;
        c.place.y = 2 + 17 * i;
        bank.push_back(c);
      }
      bank[2].place.status = physical::Status::FIXED;
      auto trial = bank;
      legalize_macro_tracks(tech, fp, trial, 2, 2, true);
      if (width == 11) {
        EXPECT_EQ(trial, bank);  // Cells can approach these shallow macros along the interior rows.
        continue;
      }
      for (int i : {0, 2, 4}) {
        EXPECT_EQ(trial[i].place, bank[i].place);
      }
      for (int i : {1, 3}) {
        const auto inward = (trial[i].place.x - bank[i].place.x) * (left ? 1 : -1);
        EXPECT_GE(inward, 39);  // Keep row ends for both nearby cells and post-CTS buffers.
        EXPECT_EQ(inward % 6, 0);  // Keep the horizontal site phase.
        EXPECT_EQ(trial[i].place.y, bank[i].place.y);
      }
      const auto saved = trial;
      legalize_macro_tracks(tech, fp, trial, 2, 2, true);
      EXPECT_EQ(trial, saved);
    }
  }
}

TEST(Placement, InteriorOfTallCoreBankKeepsAnAccessibleSideChannel) {
  auto [tech, fp] = track_fixture();
  auto& ram       = tech.macros.at("ram");
  ram.pins.clear();
  ram.width  = 32;
  ram.height = 26;
  fp.die.yh  = 300;
  for (int r = 10; r < 30; ++r) {
    fp.rows.push_back(
        {"r" + std::to_string(r), "grid", 0, r * 10, 20, 6, r % 2 ? physical::Orientation::FS : physical::Orientation::N});
  }
  std::vector<physical::Component> bank;
  for (int i = 0; i < 3; ++i) {
    auto c         = track_macro();
    c.name         = "bank" + std::to_string(i);
    c.place.width  = 32;
    c.place.height = 26;
    c.place.x      = 85;
    c.place.y      = 82 + 31 * i;
    bank.push_back(c);
  }
  // The three halos cover 92 of 300 DBU. The middle SRAM is 46 DBU from either
  // entrance and 36 DBU deep; only one DBU remains for cells behind its halo.
  auto trial = bank;
  legalize_macro_tracks(tech, fp, trial, 2, 2, true);
  EXPECT_EQ(trial.front().place, bank.front().place);
  EXPECT_EQ(trial.back().place, bank.back().place);
  EXPECT_GT(bank[1].place.x - trial[1].place.x, 10);
  EXPECT_EQ((bank[1].place.x - trial[1].place.x) % 6, 0);
  EXPECT_EQ(trial[1].place.y, bank[1].place.y);

  // With a full row between the banks, all three already have an entrance.
  bank.back().place.y += 10;
  trial                = bank;
  legalize_macro_tracks(tech, fp, trial, 2, 2, true);
  EXPECT_EQ(trial, bank);
}

TEST(Placement, WideSideHaloNeverClosesAnExistingChannel) {
  auto [tech, fp] = track_fixture();
  fp.die.xh = 360;
  for (auto& row : fp.rows) {
    row.count = 60;
  }
  tech.macros.at("ram").pins.clear();
  tech.macros.at("ram").width = 40;
  const int64_t halo = 36;
  for (bool left : {false, true}) {
    std::vector<physical::Component> bank;
    for (int i = 0; i < 5; ++i) {
      auto c = track_macro();
      c.name = "bank" + std::to_string(i);
      c.place.width = 40;
      c.place.x = left ? 66 : 360 - 40 - 66;  // 30 DBU free beyond the halo, greater than two rows.
      c.place.y = 2 + 17 * i;
      bank.push_back(c);
    }
    auto trial = bank;
    legalize_macro_tracks(tech, fp, trial, halo, 2, true);
    for (int i : {1, 2, 3}) {
      const auto free = (left ? trial[i].place.x : 360 - trial[i].place.x - 40) - halo;
      EXPECT_GE(free, halo);
      EXPECT_EQ((free - 30) % 6, 0);
    }
    const auto saved = trial;
    legalize_macro_tracks(tech, fp, trial, halo, 2, true);
    EXPECT_EQ(trial, saved);
  }
}

// Track alignment never leaves two macro bodies closer than the macro channel, even with zero halos.
TEST(Placement, MacroTrackAlignmentKeepsTheMacroChannel) {
  const auto [tech, fp] = track_fixture();
  for (const int64_t channel : {0, 5}) {
    auto right    = track_macro();
    right.name    = "ram1";
    right.place.x = 30 + 11;  // abutting on the right
    std::vector<physical::Component> cells{track_macro(), right};
    legalize_macro_tracks(tech, fp, cells, 0, 0, true, channel, channel);
    const auto& a     = cells[0].place;
    const auto& b     = cells[1].place;
    const auto  gap_x = std::max(b.x - (a.x + a.width), a.x - (b.x + b.width));
    const auto  gap_y = std::max(b.y - (a.y + a.height), a.y - (b.y + b.height));
    EXPECT_GE(std::max(gap_x, gap_y), channel) << gap_x << ' ' << gap_y;
    if (channel) {
      // Exact clearance on the track lattice: at most one track period (4 in x, 6 in y) beyond the channel.
      EXPECT_TRUE((gap_x >= channel && gap_x < channel + 4) || (gap_y >= channel && gap_y < channel + 6)) << gap_x << ' ' << gap_y;
    }
  }
}

TEST(Placement, VirtualMemoryTrackAlignmentPreservesExplicitFixedOrigins) {
  auto [tech, fp]                     = track_fixture();
  tech.macros.at("ram").virtual_proxy = true;
  std::vector<physical::Component> cells{track_macro()};
  cells.front().place.status = Ann_place::Status::FIXED;
  EXPECT_THROW(legalize_macro_tracks(tech, fp, cells, 0, 0, true), std::runtime_error);
  EXPECT_EQ(cells.front().place.x, 30);
  EXPECT_EQ(cells.front().place.y, 30);
  cells.front().place.status = Ann_place::Status::PLACED;
  ASSERT_NO_THROW(legalize_macro_tracks(tech, fp, cells, 0, 0, true));
  EXPECT_NE(cells.front().place.x, 30);
  EXPECT_NE(cells.front().place.y, 30);
  cells.front().place.status = Ann_place::Status::FIXED;
  EXPECT_NO_THROW(legalize_macro_tracks(tech, fp, cells, 0, 0, false));
}

TEST(Placement, VirtualMemoryFindsDistantTrackAlignedSpaceWithClearance) {
  auto [tech, fp]                     = track_fixture();
  tech.macros.at("ram").virtual_proxy = true;
  fp.die                             = {0, 0, 1200, 1200};
  fp.rows.clear();
  for (int y = 0; y < 1200; y += 10) {
    fp.rows.push_back({"r" + std::to_string(y), "grid", 0, y, 200, 6, physical::Orientation::N});
  }
  fp.tracks[0].count = 280;  // Finite tracks end before the far edge of the die.
  fp.tracks[1].count = 190;
  auto& blocked      = fp.blockages.emplace_back().rectangles;
  blocked            = {
      {  0, 0,  990, 1200},
      {990, 0, 1200,  990}
  };
  // Many closer edges hide the distant opening from the bounded search.
  for (int i = 70; i < 690; i += 6) {
    blocked.push_back({double(i), double(i), double(i + 1), double(i + 1)});
  }
  auto fixed         = track_macro();
  fixed.name         = "fixed";
  fixed.place.x      = 1001;
  fixed.place.y      = 995;
  fixed.place.status = Ann_place::Status::FIXED;
  auto second        = track_macro();
  second.name        = "ram1";
  std::vector<physical::Component> cells{track_macro(), fixed, second};
  ASSERT_NO_THROW(legalize_macro_tracks(tech, fp, cells, 2, 2, true, 12, 12));
  EXPECT_EQ(cells[1], fixed);
  for (const auto i : {0, 2}) {
    const auto& p = cells[i].place;
    EXPECT_GE(p.x, 990);
    EXPECT_GE(p.y, 990);
    EXPECT_LE(p.x + 6, 3 + 279 * 4);
    EXPECT_LE(p.y + 3, 2 + 189 * 6);
    EXPECT_EQ((p.x + 2 - 3) % 4, 0);
    EXPECT_EQ((p.y + 3 - 2) % 6, 0);
    for (int j = 0; j < 3; ++j) {
      if (i != j) {
        const auto& q     = cells[j].place;
        const auto  gap_x = std::max(p.x - q.x - q.width, q.x - p.x - p.width);
        const auto  gap_y = std::max(p.y - q.y - q.height, q.y - p.y - p.height);
        EXPECT_GE(std::max(gap_x, gap_y), 12);
      }
    }
  }
  auto fixed_frame = cells;
  for (auto& c : fixed_frame) {
    c.place.status = Ann_place::Status::FIXED;
  }
  EXPECT_NO_THROW(legalize_macro_tracks(tech, fp, fixed_frame, 2, 2, false, 12, 12));
  blocked.push_back({990, 990, 1200, 1200});
  const auto saved = cells;
  EXPECT_THROW(legalize_macro_tracks(tech, fp, cells, 2, 2, true, 12, 12), Macro_placement_no_space);
  EXPECT_EQ(cells, saved);
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
TEST(Placement, CompactStripPrefersFullWidthPinAccessOverAGreedySliver) {
  auto [tech, fp] = track_fixture();
  auto& ram = tech.macros.at("ram");
  ram.pins.clear();
  ram.pins["A"] = {"INPUT", "SIGNAL", {{"V", {0, 0, 4, 4}}}};
  tech.layers.at("V").width = 4;
  // The preferred track phase is x=1 mod 4. Moving just the left macro to that phase would violate its
  // channel; its current origin still admits a track center near the edge. Moving the neighbor makes room.
  auto left = track_macro(), right = track_macro();
  left.place.x = 4;
  right.name = "neighbor";
  right.place.x = 19;
  std::vector<physical::Component> cells{left, right};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ((cells[0].place.x - 1) % 4, 0);
  EXPECT_EQ((cells[1].place.x - 1) % 4, 0);
  EXPECT_GE(cells[1].place.x - cells[0].place.x - cells[0].place.width, 4);
  // The same joint move would enter a hard placement blockage. A soft one may release its sites for legalization.
  fp.blockages.push_back({false, "", {{15, 0, 16, 100}}});
  cells = {left, right};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ(cells[0].place.x, left.place.x);
  fp.blockages.front().soft = true;
  cells = {left, right};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ((cells[0].place.x - 1) % 4, 0);
  fp.blockages.clear();
  // A fixed neighbor takes precedence over the full-width preference.
  right.macro = "cell";
  right.place.status = physical::Status::FIXED;
  right.place.x = 17;
  cells = {left, right};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true);
  EXPECT_EQ(cells[1].place.x, right.place.x);
  EXPECT_EQ(cells[0].place.x, left.place.x);
}

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

namespace livehd::placement {
// macro.pdn: a strap lattice on y moves the macro onto an admitted edge that is also on its routing phase; an
// unsatisfiable lattice falls back to the routing-track alignment alone and reports it.
TEST(Placement, MacroTrackAlignmentFollowsThePowerGridLattice) {
  const auto [tech, fp] = track_fixture();
  Pg_edge_lattice lattice;
  lattice.period  = 30;
  lattice.channel = 12;
  lattice.margin  = 1;
  lattice.groups  = {{{3, 8}}};
  Pg_macro_alignment pdn;
  pdn.axis[1] = lattice;
  std::vector<physical::Component> plain{track_macro()};
  legalize_macro_tracks(tech, fp, plain, 2, 2, true);
  std::vector<physical::Component> cells{track_macro()};
  bool                             aligned = false;
  legalize_macro_tracks(tech, fp, cells, 2, 2, true, 0, 0, &pdn, &aligned);
  EXPECT_TRUE(aligned);
  const auto& p = cells.front().place;
  EXPECT_TRUE(lattice.allows(p.y, p.y + p.height, 0, 100)) << p.y;
  EXPECT_EQ((p.y + 3 - 2) % 6, 0);
  EXPECT_EQ(p.x, plain.front().place.x);  // the x axis has no lattice
  EXPECT_NE(p.y, plain.front().place.y);  // the plain alignment lands off the lattice here
  // A group wider than the channel admits no edge: keep the plain alignment, report the fallback.
  pdn.axis[1]->groups = {{{0, 20}}};
  cells               = {track_macro()};
  legalize_macro_tracks(tech, fp, cells, 2, 2, true, 0, 0, &pdn, &aligned);
  EXPECT_FALSE(aligned);
  EXPECT_EQ(cells.front().place, plain.front().place);
  // Fixed macros are never moved or checked against the lattice.
  cells                      = {track_macro()};
  cells.front().place        = plain.front().place;
  cells.front().place.status = Ann_place::Status::FIXED;
  legalize_macro_tracks(tech, fp, cells, 2, 2, false, 0, 0, &pdn, &aligned);
  EXPECT_FALSE(aligned);
  EXPECT_EQ(cells.front().place.x, plain.front().place.x);
  EXPECT_EQ(cells.front().place.y, plain.front().place.y);
}
// macro.pdn with rigid macro groups: the lattice must admit every member macro's edges, not only the group's
// bounding box. Admitted low edges here are y = 9..14 (mod 30); on the routing phase only y = 11 (mod 30).
TEST(Placement, RigidMacroGroupsAlignEveryMemberToThePowerGridLattice) {
  const auto [tech, fp] = track_fixture();
  Pg_edge_lattice lattice;
  lattice.period  = 30;
  lattice.channel = 12;
  lattice.margin  = 1;
  lattice.groups  = {{{3, 8}}};
  Pg_macro_alignment pdn;
  pdn.axis[1]        = lattice;
  const auto stacked = [&](int64_t pitch) {
    auto a = track_macro(), b = track_macro();
    b.name     = "ram1";
    b.place.y += pitch;  // a whole number of routing periods: one phase for the group
    return std::vector<physical::Component>{a, b};
  };
  const auto members_ok = [&](const std::vector<physical::Component>& cells) {
    return std::all_of(cells.begin(), cells.end(), [&](const auto& c) {
      return lattice.allows(c.place.y, c.place.y + c.place.height, 0, 100);
    });
  };
  // Pitch 30: both members land on admitted edges together.
  auto aligned_cells = stacked(30);
  bool aligned       = false;
  legalize_rigid_macro_tracks(tech, fp, aligned_cells, {1, 1}, 2, 2, true, 0, 0, &pdn, &aligned);
  EXPECT_TRUE(aligned);
  EXPECT_TRUE(members_ok(aligned_cells));
  EXPECT_EQ(aligned_cells[1].place.y - aligned_cells[0].place.y, 30);
  // Pitch 18: when the lower member is admitted the upper one (18 above) never is, so no lattice frame exists even
  // though the group's bounding box (the lower member's low edge) can be admitted: report the fallback.
  auto cells = stacked(18);
  legalize_rigid_macro_tracks(tech, fp, cells, {1, 1}, 2, 2, true, 0, 0, &pdn, &aligned);
  EXPECT_FALSE(aligned);
  EXPECT_FALSE(members_ok(cells));
  EXPECT_EQ(cells[1].place.y - cells[0].place.y, 18);
}
}  // namespace livehd::placement
