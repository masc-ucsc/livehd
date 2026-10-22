#include "routing_tracks.hpp"

#include "gtest/gtest.h"
#include "macro_tracks.hpp"

namespace livehd::placement {
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
