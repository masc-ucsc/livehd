// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
TEST(LegrouteClock, ChargeIsTheSpineTreeOfTheClockSinkTiles) {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  Design               design{tech, fp, options};
  Grid                 grid;
  fp.dbu      = 1;
  grid.cols   = 2;
  grid.rows   = 5;
  grid.tile_w = 100;
  grid.tile_h = 2;
  Net clk;
  clk.clock = true;
  // the clock enters at the origin (tile 0)
  fp.terminals.push_back({.name = "clk", .x = 0, .y = 0});
  clk.pins.push_back({.term = 0, .output = true});
  clk.driver = 0;
  // flops in rows 1, 2, 3 of column 0 and row 1 of column 1
  for (const int tile : {2, 4, 6, 3}) {
    const auto index  = fp.components.size();
    const auto name   = "flop" + std::to_string(index);
    auto&      master = tech.macros[name];
    master.width      = 10;
    master.height     = 1;
    master.klass      = "CORE";
    Ann_place pose(double(grid.left(grid.col(tile)) + 10), double(grid.bottom(grid.row(tile))), 10, 1);
    pose.status = physical::Status::PLACED;
    fp.components.push_back({name, name, pose});
    design.macro.push_back(&master);
    design.cell.push_back(int(index));
    design.movable.push_back(int(index));
    clk.pins.push_back({.comp = int(index), .clock = true});
  }
  design.nets.push_back(clk);
  const auto domain = plan_clock_domain(grid, {2, 4, 6, 3}, 4);
  EXPECT_EQ(domain.trunk_c, 0);
  EXPECT_EQ(domain.ribs, std::vector<int>{3});  // one rib, centred within half a pitch of every flop row
  EXPECT_EQ(domain.tap_rib, (std::vector<int>{3, 3, 3, 3}));
  ASSERT_EQ(domain.span.size(), 1u);
  EXPECT_EQ(domain.span.at(3), (std::pair<int, int>{0, 1}));
  const auto charge = clock_tree_charge(design, grid, 4);
  // vertical: the trunk on the rib row, one tap per flop from its row to the rib, and the entry join up column 0
  EXPECT_EQ(charge.v, (std::vector<int>{1, 0, 2, 1, 3, 1, 5, 1, 0, 0}));
  // horizontal: the rib across both columns
  EXPECT_EQ(charge.h, (std::vector<int>{0, 0, 0, 0, 0, 0, 1, 1, 0, 0}));
  // a signal net, a power net and a clock net with a single pin are not charged
  Design other{tech, fp, options};
  other.macro   = design.macro;
  other.cell    = design.cell;
  other.movable = design.movable;
  Net signal    = clk, power = clk, lone;
  signal.clock  = false;
  power.power   = true;
  lone.clock    = true;
  lone.pins     = {clk.pins[1]};
  other.nets    = {signal, power, lone};
  const auto none = clock_tree_charge(other, grid, 4);
  EXPECT_EQ(none.v, std::vector<int>(grid.size(), 0));
  EXPECT_EQ(none.h, std::vector<int>(grid.size(), 0));
}
}  // namespace livehd::placement::lr
