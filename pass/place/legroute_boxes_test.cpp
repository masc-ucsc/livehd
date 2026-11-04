// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gtest/gtest.h"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
struct Fixture {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  Design               design{tech, fp, options};
  Grid                 grid;
  Boxes                boxes;
  Fixture() {
    fp.dbu    = 1;
    grid.cols = 3;
    grid.rows = grid.regions = 1;
    grid.tile_w = grid.sites = 10;
    grid.tile_h              = 2;
    grid.site = grid.row_h = 1;
    grid.region            = {0, 0, 0};
    grid.blocked           = {0, 0, 0};
    grid.key               = {0, 0, 1};
    grid.keys              = {
        {{0, 0, 10}, {1, 0, 10}},
        { {0, 0, 2},  {1, 0, 2}}
    };
    auto& master  = tech.macros["cell"];
    master.width  = 4;
    master.height = 1;
    for (int c = 0; c < 6; ++c) {
      const int b = c / 2;
      Ann_place pose(b * 10, c % 2, 4, 1);
      pose.status = physical::Status::PLACED;
      fp.components.push_back({std::to_string(c), "cell", pose});
      design.macro.push_back(&master);
      design.movable.push_back(c);
      design.cell.push_back(c);
      design.width.push_back(4);
      design.flop.push_back(false);
      design.tall.push_back(false);
      design.nets_of.emplace_back();
      boxes.box_of.push_back(b);
      if (c % 2 == 0) {
        Box box;
        box.region = 0;
        box.cx     = b * 10 + 2;
        box.cy     = 1;
        box.sites  = 8;
        boxes.boxes.push_back(box);
      }
      boxes.boxes[size_t(b)].cells.push_back(c);
    }
  }
};
}  // namespace

TEST(LegrouteBoxes, RebalancesGroupingAgainstActualSlotsWithoutMovingPhysicalCells) {
  Fixture    f;
  const auto physical = f.fp.components;
  const auto keys     = f.grid.keys;
  auto       repaired = repair_box_slots(f.design, f.grid, f.boxes, 100);
  EXPECT_EQ(repaired.before, 1u);
  EXPECT_EQ(repaired.after, 0u);
  EXPECT_EQ(repaired.eliminated_boxes, 1u);
  EXPECT_EQ(repaired.moved_cells, 2u);
  EXPECT_EQ(f.boxes.boxes.size(), 2u);
  EXPECT_EQ(f.fp.components, physical);
  EXPECT_EQ(f.grid.keys, keys);
  std::vector<int> cells;
  for (size_t b = 0; b < f.boxes.boxes.size(); ++b) {
    EXPECT_TRUE(box_fits(f.design, f.grid, f.boxes.boxes[b], 0));
    for (int c : f.boxes.boxes[b].cells) {
      cells.push_back(c);
      EXPECT_EQ(f.boxes.box_of[size_t(c)], int(b));
    }
  }
  std::ranges::sort(cells);
  EXPECT_EQ(cells, (std::vector<int>{0, 1, 2, 3, 4, 5}));
  EXPECT_EQ(repair_box_slots(f.design, f.grid, f.boxes, 100).before, 0u);
}

TEST(LegrouteBoxes, FixedAssignmentsAndFillPolicyCannotBeRelaxedToClaimFit) {
  Fixture fixed;
  fixed.boxes.boxes[0].fixed = true;
  fixed.boxes.boxes[0].tile  = 0;
  fixed.boxes.boxes[1].fixed = true;
  fixed.boxes.boxes[1].tile  = 1;
  const auto original        = fixed.boxes.box_of;
  const auto report          = repair_box_slots(fixed.design, fixed.grid, fixed.boxes, 100);
  EXPECT_EQ(report.before, 1u);
  EXPECT_EQ(report.after, 1u);
  EXPECT_EQ(report.eliminated_boxes, 0u);
  EXPECT_EQ(fixed.boxes.box_of, original);
  EXPECT_EQ(fixed.boxes.boxes[0].tile, 0);
  Fixture constrained;
  constrained.options.box_fill = .4;
  const auto limited           = repair_box_slots(constrained.design, constrained.grid, constrained.boxes, 100);
  EXPECT_EQ(limited.after, 1u);
  EXPECT_EQ(limited.eliminated_boxes, 0u);
}
}  // namespace livehd::placement::lr
