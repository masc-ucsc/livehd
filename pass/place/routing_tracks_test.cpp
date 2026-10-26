#include "routing_tracks.hpp"

#include <algorithm>
#include <cmath>

#include "gtest/gtest.h"
#include "macro_tracks.hpp"

namespace livehd::placement {
TEST(RoutingTracks, AutomaticIoShapesReachStaggeredTracksOnEveryDieEdge) {
  physical::Technology tech;
  tech.dbu                = 1000;
  tech.layers["M2"].width = .018;
  physical::Floorplan fp;
  fp.die = {1000, 2000, 20000, 22000};
  // ASAP7's staggered M2 Y grids are unrelated to the 54-DBU site pitch.
  fp.tracks.push_back({"X", "M2", 1009, 528, 36});
  for (int offset : {45, 81, 117, 153, 189, 225, 270}) {
    fp.tracks.push_back({"Y", "M2", 2000 + offset, 74, 270});
  }
  const auto slots = io_perimeter_slots(tech, fp, "M2", 54);
  ASSERT_FALSE(slots.empty());
  for (auto [x, y] : slots) {
    physical::Terminal terminal;
    terminal.x      = std::llround(x);
    terminal.y      = std::llround(y);
    terminal.status = physical::Status::PLACED;
    terminal.shapes.push_back({
        "M2",
        {-9, -9, 9, 9}
    });
    fp.terminals.push_back(terminal);
  }
  auto fixed                = fp.terminals.front();
  fixed.name                = "fixed";
  fixed.status              = physical::Status::FIXED;
  fixed.shapes.front().rect = {-5, -7, 15, 17};
  fp.terminals.push_back(fixed);
  shape_io_perimeter_pins(fp);
  EXPECT_EQ(fp.terminals.back(), fixed);
  for (size_t i = 0; i < slots.size(); ++i) {
    const auto& t = fp.terminals[i];
    const auto& r = t.shapes.front().rect;
    EXPECT_GE(t.x + r.xl, fp.die.xl);
    EXPECT_GE(t.y + r.yl, fp.die.yl);
    EXPECT_LE(t.x + r.xh, fp.die.xh);
    EXPECT_LE(t.y + r.yh, fp.die.yh);
    for (const auto* axis : {"X", "Y"}) {
      const bool   x  = std::string_view(axis) == "X";
      const double lo = x ? t.x + r.xl : t.y + r.yl, hi = x ? t.x + r.xh : t.y + r.yh;
      EXPECT_TRUE(std::any_of(fp.tracks.begin(),
                              fp.tracks.end(),
                              [&](const auto& track) {
                                if (track.axis != axis) {
                                  return false;
                                }
                                const auto index = std::max(0.0, std::ceil((lo - track.start) / track.step));
                                return index < track.count && track.start + index * track.step <= hi;
                              }))
          << axis << " at " << t.x << ',' << t.y;
    }
  }
  const auto before = fp.terminals;
  shape_io_perimeter_pins(fp);
  EXPECT_EQ(fp.terminals, before);
}

TEST(RoutingTracks, IoSlotsKeepLegacySpreadWithoutKnownTracks) {
  physical::Technology tech;
  physical::Floorplan  fp;
  fp.die           = {0, 0, 100, 100};
  const auto slots = io_perimeter_slots(tech, fp, "unknown", 20);
  ASSERT_EQ(slots.size(), 20);
  EXPECT_EQ(slots.front(), (std::pair<double, double>{10, 0}));
  EXPECT_THROW(io_perimeter_slots(tech, fp, "unknown", 0), std::invalid_argument);
  fp.tracks.push_back({"X", "known", 1000, 3, 20});
  EXPECT_THROW(io_perimeter_slots(tech, fp, "known", 20), std::invalid_argument);
}

TEST(RoutingTracks, TopAndBottomIoSlotsUseVerticalLayerTrackPhase) {
  physical::Technology tech;
  tech.layers["M2"].width = .018;
  tech.layers["M3"].width = .018;
  physical::Floorplan fp;
  fp.die    = {0, 0, 1000, 1000};
  fp.tracks = {
      {"X", "M2", 27, 20,  48},
      {"Y", "M2", 45,  4, 270},
      {"X", "M3",  9, 28,  36},
      {"Y", "M3",  9, 28,  36}
  };
  const auto slots = io_perimeter_slots(tech, fp, "M2", 54, "M3");
  for (auto [x, y] : slots) {
    if (y == 0 || y == 1000) {
      EXPECT_EQ(std::llround(x - 9) % 36, 0);
    } else {
      EXPECT_EQ(std::llround(y - 45) % 270, 0);
    }
  }
}

TEST(RoutingTracks, ExplicitPhaseSurvivesDieTranslationAndWireMargin) {
  physical::Technology tech;
  tech.dbu                = 1000;
  tech.layers["M3"].width = .018;
  tech.layers["M6"].width = .032;
  physical::Floorplan fp;
  fp.die = {1000, 2000, 250000, 250000};
  apply_routing_tracks(tech,
                       fp,
                       "# Explicit grid from platform data\n"
                       "make_tracks M3 -x_offset .009 -x_pitch .036 \\\n -y_offset .009 -y_pitch .036\n"
                       "make_tracks M6 -x_offset .012 -x_pitch .048 -y_offset .016 -y_pitch .064\n");
  ASSERT_EQ(fp.tracks.size(), 4);
  EXPECT_EQ(fp.tracks[0], (physical::Track{"X", "M3", 1009, 6917, 36}));
  EXPECT_EQ(fp.tracks[1], (physical::Track{"Y", "M3", 2009, 6889, 36}));
  EXPECT_EQ(fp.tracks[2], (physical::Track{"X", "M6", 1060, 5186, 48}));
  EXPECT_EQ(fp.tracks[3], (physical::Track{"Y", "M6", 2016, 3875, 64}));
}

TEST(RoutingTracks, MalformedOrExecutableInputLeavesExistingGridUnchanged) {
  physical::Technology tech;
  tech.dbu                = 1000;
  tech.layers["M3"].width = .018;
  physical::Floorplan fp;
  fp.die    = {0, 0, 1000, 1000};
  fp.tracks = {
      {"X", "old", 0, 5, 10}
  };
  const auto original = fp.tracks;
  for (const auto* bad : {"",
                          "source other.tcl",
                          "make_tracks absent -x_offset 0 -x_pitch 1 -y_offset 0 -y_pitch 1",
                          "make_tracks M3 -x_offset $x -x_pitch .036 -y_offset .009 -y_pitch .036",
                          "make_tracks M3 -x_offset .009 -x_pitch 0 -y_offset .009 -y_pitch .036",
                          "make_tracks M3 -x_offset .009 -x_pitch .036 -y_offset .009",
                          "make_tracks M3 -x_offset .009 -x_pitch .036 -y_offset .009 -y_pitch .036; exec touch x",
                          "make_tracks M3 -x_offset .0001 -x_pitch .036 -y_offset .009 -y_pitch .036"}) {
    EXPECT_THROW(apply_routing_tracks(tech, fp, bad), std::invalid_argument) << bad;
    EXPECT_EQ(fp.tracks, original);
  }
}

TEST(RoutingTracks, MacroAlignmentUsesRouterPhaseInsteadOfLefOffset) {
  physical::Technology tech;
  tech.dbu        = 1000;
  auto& layer     = tech.layers["M3"];
  layer.direction = "VERTICAL";
  layer.width     = .018;
  layer.pitch_x = layer.pitch_y = .036;
  layer.has_offset              = true;
  auto& ram                     = tech.macros["ram"];
  ram.klass                     = "BLOCK";
  ram.width = ram.height = 1;
  ram.pins["D"]          = {"INPUT", "SIGNAL", {{"M3", {.207, 0, .225, .054}}}};
  physical::Floorplan fp;
  fp.die = {0, 0, 250000, 250000};
  apply_routing_tracks(tech, fp, "make_tracks M3 -x_offset .009 -x_pitch .036 -y_offset .009 -y_pitch .036\n");
  tech.sites["row"] = {1, 1, true};
  for (int64_t y = 0; y < 250000; y += 1000) {
    fp.rows.push_back({"r" + std::to_string(y), "row", 0, y, 250, 1000, physical::Orientation::N});
  }
  Ann_place place(100000, 100000, 1000, 1000);
  place.status = Ann_place::Status::PLACED;
  std::vector<physical::Component> cells{
      {"ram0", "ram", place}
  };
  legalize_macro_tracks(tech, fp, cells, 0, 0, true);
  EXPECT_EQ((cells[0].place.x + 216 - 9) % 36, 0);
  EXPECT_NE(cells[0].place.x, place.x);
}
}  // namespace livehd::placement
