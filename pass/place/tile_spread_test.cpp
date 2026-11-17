// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tile_spread.hpp"

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
// dbu 1: site 1 x 10, four routing layers (the 4th has pitch 2 -> a 30-site routing tile), rows of 60 sites.
std::pair<physical::Technology, physical::Floorplan> fixture(int rows = 2) {
  physical::Technology tech;
  tech.dbu           = 1;
  tech.sites["core"] = {1, 10, true};
  for (const auto& [name, dir] : std::vector<std::pair<std::string, std::string>>{{"M1", "VERTICAL"},
                                                                                  {"M2", "HORIZONTAL"},
                                                                                  {"M3", "VERTICAL"},
                                                                                  {"M4", "HORIZONTAL"}}) {
    auto& l     = tech.layers[name];
    l.direction = dir;
    l.pitch_x = l.pitch_y = 2;
    tech.routing_layers.push_back(name);
  }
  auto& cell  = tech.macros["inv"];
  cell.klass  = "CORE";
  cell.width  = 4;
  cell.height = 10;
  cell.site   = "core";
  cell.pins["A"] = {"INPUT", "SIGNAL", {{"M1", {0.5, 4, 1.5, 6}}}};  // x center 1
  cell.pins["Y"] = {"OUTPUT", "SIGNAL", {{"M1", {2.5, 4, 3.5, 6}}}};  // x center 3
  physical::Floorplan fp;
  fp.dbu = 1;
  fp.die = {0, 0, 60, 10.0 * rows};
  for (int r = 0; r < rows; ++r) {
    fp.rows.push_back({"r" + std::to_string(r), "core", 0, 10 * r, 60, 1, physical::Orientation::N});
  }
  return {tech, fp};
}
physical::Component inv(const std::string& name, int64_t x, int64_t y) {
  Ann_place p(x, y, 4, 10);
  p.status = Ann_place::Status::PLACED;
  return {name, "inv", p};
}
}  // namespace

TEST(TileSpread, RoutingTileIsFifteenPitchesOfTheFourthLayer) {
  const auto [tech, fp] = fixture();
  EXPECT_EQ(routing_tile_width(tech, fp), 30);
}

TEST(TileSpread, PackedCellsShareTheTileWhitespace) {
  auto [tech, fp] = fixture(1);
  // three abutting cells at the left of the first tile [0, 30): 12 used, 18 free
  fp.components = {inv("a", 0, 0), inv("b", 4, 0), inv("c", 8, 0)};
  const auto report = spread_cells_in_tiles(tech, fp);
  EXPECT_EQ(report.tile_w, 30);
  // unit = 18 / 3 = 6: edge gaps 3, inner gaps 6
  EXPECT_EQ(fp.components[0].place.x, 3);
  EXPECT_EQ(fp.components[1].place.x, 13);
  EXPECT_EQ(fp.components[2].place.x, 23);
  EXPECT_EQ(report.moved, 3);
}

TEST(TileSpread, ObstaclesAndTileCrossingCellsStayAndSplitTheStretch) {
  auto [tech, fp] = fixture(1);
  auto fixed      = inv("f", 10, 0);
  fixed.place.status = Ann_place::Status::FIXED;
  // b crosses the tile edge at 30: it is an obstacle too
  fp.components = {inv("a", 0, 0), fixed, inv("b", 28, 0)};
  spread_cells_in_tiles(tech, fp);
  EXPECT_EQ(fp.components[0].place.x, 3);  // centered in [0, 10)
  EXPECT_EQ(fp.components[1].place.x, 10);
  EXPECT_EQ(fp.components[2].place.x, 28);
}

TEST(TileSpread, ConnectedPinsAlignWithTheRowBelow) {
  auto [tech, fp] = fixture(2);
  // row 0: driver d with Y at x = 20 + 3 = 23 (alone in its tile it centers at 13; give it a neighbour so it stays put)
  fp.components = {inv("d", 0, 0), inv("e", 4, 0), inv("s", 0, 10)};
  physical::Net net;
  net.name        = "n";
  net.connections = {{"d", "Y"}, {"s", "A"}};
  fp.nets.push_back(net);
  spread_cells_in_tiles(tech, fp);
  const auto driver_pin = fp.components[0].place.x + 3;
  // s alone would center at 13; aligning its A pin (dx 1) with the driver's Y is within its window
  EXPECT_EQ(fp.components[2].place.x + 1, driver_pin);
}

TEST(TileSpread, LegalPlacementStaysLegal) {
  auto [tech, fp] = fixture(2);
  for (int i = 0; i < 12; ++i) {
    fp.components.push_back(inv("c" + std::to_string(i), (i % 6) * 9, (i / 6) * 10));
  }
  spread_cells_in_tiles(tech, fp);
  for (size_t i = 0; i < fp.components.size(); ++i) {
    const auto& a = fp.components[i].place;
    EXPECT_GE(a.x, 0);
    EXPECT_LE(a.x + a.width, 60);
    for (size_t j = i + 1; j < fp.components.size(); ++j) {
      const auto& b = fp.components[j].place;
      if (a.y == b.y) {
        EXPECT_TRUE(a.x + a.width <= b.x || b.x + b.width <= a.x) << i << " overlaps " << j;
      }
    }
  }
}
}  // namespace livehd::placement
