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
  Route_cuts           cuts;
  Fixture(int cols = 3, int rows = 2) {
    fp.dbu                  = 1;
    options.partition_cells = 4;
    options.legal_boxes     = false;
    grid.cols               = cols;
    grid.rows               = rows;
    grid.regions            = 2;
    grid.site = grid.row_h = 1;
    grid.tile_w = grid.sites = 10;
    grid.tile_h              = 2;
    grid.region.resize(grid.size());
    for (size_t t = 0; t < grid.size(); ++t) {
      grid.region[t] = grid.col(int(t)) == 0 ? 0 : 1;
    }
    grid.key.assign(grid.size(), 0);
    grid.keys = {
        {{0, 0, 10}, {1, 0, 10}},
        { {0, 0, 4},  {1, 0, 4}}
    };
    grid.blocked.assign(grid.size(), 0);
    cuts.forbidden_h.assign(grid.size(), false);
    cuts.forbidden_v.assign(grid.size(), false);
  }
  void box(int home, std::initializer_list<int> widths) {
    Box b;
    b.tile   = home;
    b.region = grid.region[size_t(home)];
    b.cx     = grid.cx(grid.col(home));
    b.cy     = grid.cy(grid.row(home));
    for (int width : widths) {
      const auto id     = fp.components.size();
      const auto name   = "c" + std::to_string(id);
      auto&      master = tech.macros[name];
      master.width      = width;
      master.height     = 1;
      master.klass      = "CORE";
      Ann_place pose(grid.left(grid.col(home)), grid.bottom(grid.row(home)), width, 1);
      pose.status = physical::Status::PLACED;
      fp.components.push_back({name, name, pose});
      design.macro.push_back(&master);
      design.cell.push_back(int(id));
      design.movable.push_back(int(id));
      design.width.push_back(width);
      design.tall.push_back(false);
      boxes.box_of.push_back(int(boxes.boxes.size()));
      b.cells.push_back(int(id));
      b.sites += width;
    }
    boxes.boxes.push_back(std::move(b));
  }
  Legroute_gate_report assess() {
    return assess_legroute_gates(design,
                                 grid,
                                 &boxes,
                                 {},
                                 {},
                                 std::vector<int>(grid.size(), 20),
                                 std::vector<int>(grid.size(), 20),
                                 true);
  }
  Partition_fit_report repair() { return repair_partition_fit(design, grid, boxes, cuts); }
};
}  // namespace

TEST(LegroutePartitionFit, BoundarySlackRepairsExactMatchingWithoutMovingCellsOrChangingBoxes) {
  Fixture f;
  f.grid.key[f.grid.tile(0, 1)] = 1;
  f.box(0, {6});
  f.box(0, {6});
  f.box(2, {6});
  const auto before = f.assess();
  ASSERT_EQ(before.unfittable_boxes, 1u);
  const auto components = f.fp.components;
  const auto original   = f.boxes;
  const auto keys       = f.grid.key;
  const auto report     = f.repair();
  EXPECT_TRUE(report.assessed);
  EXPECT_TRUE(report.resolved);
  EXPECT_EQ(report.missing_before, 1u);
  EXPECT_EQ(report.missing_after, 0u);
  EXPECT_EQ(report.transfers, 1u);
  EXPECT_LE(report.work, report.work_limit);
  EXPECT_TRUE(connected_regions(f.grid, f.grid.region));
  EXPECT_TRUE(f.cuts.allows(f.grid, f.grid.region));
  EXPECT_FALSE(f.assess().requires_replan());
  EXPECT_EQ(f.fp.components, components);
  EXPECT_EQ(f.grid.key, keys);
  EXPECT_EQ(f.boxes.box_of, original.box_of);
  for (size_t i = 0; i < original.boxes.size(); ++i) {
    EXPECT_EQ(f.boxes.boxes[i].cells, original.boxes[i].cells);
    EXPECT_EQ(f.boxes.boxes[i].tile, original.boxes[i].tile);
    EXPECT_EQ(f.boxes.boxes[i].cx, original.boxes[i].cx);
    EXPECT_EQ(f.boxes.boxes[i].cy, original.boxes[i].cy);
  }
  const auto repaired = f.grid.region;
  EXPECT_EQ(f.repair().transfers, 0u);
  EXPECT_EQ(f.grid.region, repaired);
}

TEST(LegroutePartitionFit, PhysicalDensityCanImproveWithAConnectedEmptyTile) {
  Fixture f(3, 1);
  f.box(0, {10, 10});
  ASSERT_EQ(f.assess().cell_dense_regions, 1u);
  ASSERT_EQ(f.assess().unfittable_boxes, 0u);
  const auto report = f.repair();
  EXPECT_EQ(report.dense_before, 1u);
  EXPECT_EQ(report.dense_after, 0u);
  EXPECT_TRUE(report.resolved);
  EXPECT_FALSE(f.assess().requires_replan());
  EXPECT_EQ(f.grid.region, (std::vector<int>{0, 0, 1}));
}

TEST(LegroutePartitionFit, AtomicPairCrossesNeutralPartialTileWithoutPublishingAnIntermediateCut) {
  Fixture f(4, 2);
  // The boundary column has only partial row segments: taking one such tile
  // cannot fit another box. The next column has usable sites. Protect the two
  // original owner tiles so moving their boxes out cannot bypass the fixture.
  for (int t : {f.grid.tile(0, 1), f.grid.tile(1, 0), f.grid.tile(1, 1)}) {
    f.grid.key[t] = 1;
  }
  f.cuts.forbidden_v[0] = true;
  f.box(0, {6});
  f.box(0, {6});
  const auto original = f.grid.region;
  const auto pose     = f.fp.components;
  ASSERT_EQ(f.assess().unfittable_boxes, 1u);
  const auto report = f.repair();
  EXPECT_TRUE(report.resolved);
  EXPECT_EQ(report.patches, 1u);
  EXPECT_EQ(report.transfers, 2u);
  size_t changed = 0;
  for (size_t t = 0; t < original.size(); ++t) {
    changed += original[t] != f.grid.region[t];
  }
  EXPECT_EQ(changed, 2u);
  EXPECT_TRUE(connected_regions(f.grid, f.grid.region));
  EXPECT_TRUE(f.cuts.allows(f.grid, f.grid.region));
  EXPECT_FALSE(f.assess().requires_replan());
  EXPECT_EQ(f.fp.components, pose);
  EXPECT_EQ(f.boxes.boxes[0].tile, 0);
  EXPECT_EQ(f.boxes.boxes[1].tile, 0);
}

TEST(LegroutePartitionFit, RejectedPairLeavesNeutralBoundaryTilesAndHomeBoxesUntouched) {
  Fixture f(4, 2);
  for (int t : {f.grid.tile(0, 1), f.grid.tile(1, 0), f.grid.tile(1, 1)}) {
    f.grid.key[t] = 1;
  }
  f.cuts.forbidden_v[0] = true;
  // Either useful two-tile patch would split a protected coarse edge.
  f.cuts.forbidden_h[2] = f.cuts.forbidden_h[6] = true;
  f.box(0, {6});
  f.box(0, {6});
  const auto ownership = f.grid.region;
  const auto pose      = f.fp.components;
  const auto report    = f.repair();
  EXPECT_FALSE(report.resolved);
  EXPECT_FALSE(report.limited);
  EXPECT_EQ(report.missing_after, 1u);
  EXPECT_EQ(report.transfers, 0u);
  EXPECT_EQ(f.grid.region, ownership);
  EXPECT_EQ(f.fp.components, pose);
  EXPECT_EQ(f.boxes.boxes[0].region, 0);
  EXPECT_EQ(f.boxes.boxes[1].region, 0);
}

TEST(LegroutePartitionFit, RejectsATransferThatWouldDisconnectTheDonor) {
  Fixture f(5, 3);
  f.grid.region.assign(f.grid.size(), 0);
  for (int t : {f.grid.tile(1, 1), f.grid.tile(2, 1), f.grid.tile(3, 1), f.grid.tile(2, 2)}) {
    f.grid.region[t] = 1;
  }
  f.grid.key.assign(f.grid.size(), 1);
  f.grid.key[0] = f.grid.key[f.grid.tile(2, 1)] = 0;
  f.box(0, {6});
  f.box(0, {6});
  ASSERT_TRUE(connected_regions(f.grid, f.grid.region));
  const auto ownership = f.grid.region;
  const auto report    = f.repair();
  EXPECT_EQ(report.missing_before, 1u);
  EXPECT_EQ(report.missing_after, 1u);
  EXPECT_FALSE(report.resolved);
  EXPECT_EQ(report.transfers, 0u);
  EXPECT_EQ(f.grid.region, ownership);
}

TEST(LegroutePartitionFit, PreservesProtectedCoarseEdgesAndConfiguredCellTarget) {
  for (bool protect : {false, true}) {
    Fixture f(3, 1);
    f.options.partition_cells = 2;
    f.box(0, {6});
    f.box(0, {6});
    f.box(protect ? 2 : 1, {6});
    if (protect) {
      f.cuts.forbidden_h[1] = true;
    }
    const auto ownership = f.grid.region;
    const auto report    = f.repair();
    EXPECT_EQ(report.missing_after, 1u);
    EXPECT_EQ(report.transfers, 0u);
    EXPECT_EQ(f.grid.region, ownership);
    EXPECT_TRUE(f.cuts.allows(f.grid, f.grid.region));
  }
}

TEST(LegroutePartitionFit, CannotMoveAMatchingDeficitIntoAHealthyNeighbor) {
  Fixture f;
  f.grid.key[f.grid.tile(0, 1)] = f.grid.key[f.grid.tile(1, 1)] = f.grid.key[f.grid.tile(2, 1)] = 1;
  f.box(0, {6});
  f.box(0, {6});
  f.box(1, {6});
  f.box(2, {6});
  const auto ownership = f.grid.region;
  const auto report    = f.repair();
  EXPECT_EQ(report.missing_before, 1u);
  EXPECT_EQ(report.missing_after, 1u);
  EXPECT_EQ(report.transfers, 0u);
  EXPECT_EQ(f.grid.region, ownership);
}

TEST(LegroutePartitionFit, RelayMovesRemoteSlackThroughASaturatedNeighborAtomically) {
  Fixture f(6, 1);
  f.grid.regions            = 3;
  f.grid.region             = {0, 0, 1, 1, 2, 2};
  f.grid.key[1]             = 1;  // target has one fitting slot for its two boxes
  f.options.partition_cells = 8;
  f.box(0, {6});
  f.box(0, {6});
  f.box(3, {6});
  f.box(3, {6});  // mediator's two usable slots are saturated
  f.box(5, {6});  // remote region has the one spare slot
  const auto pose     = f.fp.components;
  const auto original = f.boxes;
  ASSERT_EQ(f.assess().unfittable_boxes, 1u);
  const auto report = f.repair();
  EXPECT_TRUE(report.resolved);
  EXPECT_EQ(report.missing_after, 0u);
  EXPECT_EQ(report.transfers, 2u);
  EXPECT_EQ(f.grid.region, (std::vector<int>{0, 0, 0, 1, 1, 2}));
  EXPECT_TRUE(connected_regions(f.grid, f.grid.region));
  EXPECT_TRUE(f.cuts.allows(f.grid, f.grid.region));
  EXPECT_FALSE(f.assess().requires_replan());
  EXPECT_EQ(f.fp.components, pose);
  EXPECT_EQ(f.boxes.box_of, original.box_of);
  for (size_t i = 0; i < original.boxes.size(); ++i) {
    EXPECT_EQ(f.boxes.boxes[i].cells, original.boxes[i].cells);
    EXPECT_EQ(f.boxes.boxes[i].tile, original.boxes[i].tile);
    EXPECT_EQ(f.boxes.boxes[i].cx, original.boxes[i].cx);
    EXPECT_EQ(f.boxes.boxes[i].cy, original.boxes[i].cy);
  }
}

TEST(LegroutePartitionFit, RejectedRelayDoesNotPublishItsNeutralFirstStep) {
  for (bool protect : {false, true}) {
    Fixture f(6, 1);
    f.grid.regions            = 3;
    f.grid.region             = {0, 0, 1, 1, 2, 2};
    f.grid.key[1]             = 1;
    f.options.partition_cells = 8;
    f.box(0, {6});
    f.box(0, {6});
    f.box(3, {6});
    f.box(3, {6});
    f.box(5, {6});
    if (protect) {
      f.cuts.forbidden_h[2] = true;  // second relay leg would split this edge
    } else {
      f.box(5, {6});  // the remote region also has no slack
    }
    const auto ownership = f.grid.region;
    const auto pose      = f.fp.components;
    const auto report    = f.repair();
    EXPECT_FALSE(report.resolved);
    EXPECT_EQ(report.missing_after, 1u);
    EXPECT_EQ(report.transfers, 0u);
    EXPECT_EQ(f.grid.region, ownership);
    EXPECT_EQ(f.fp.components, pose);
    EXPECT_TRUE(f.cuts.allows(f.grid, f.grid.region));
  }
}

TEST(LegroutePartitionFit, LegalBoxAndNonPartitionFlowsAreUnchanged) {
  for (bool legal : {false, true}) {
    Fixture f;
    f.box(0, {6});
    f.box(0, {6});
    f.options.legal_boxes = legal;
    if (!legal) {
      f.options.partition_cells = 0;
    }
    const auto ownership = f.grid.region;
    const auto report    = f.repair();
    EXPECT_FALSE(report.assessed);
    EXPECT_EQ(report.transfers, 0u);
    EXPECT_EQ(f.grid.region, ownership);
  }
}
}  // namespace livehd::placement::lr
