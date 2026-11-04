// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>

#include "gtest/gtest.h"
#include "legroute_internal.hpp"
#include "rapidjson/document.h"

namespace livehd::placement::lr {
namespace {
struct Fixture {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  Design               design{tech, fp, options};
  Grid                 grid;
  std::vector<int>     capacity{20, 20};
  Fixture() {
    options.global_share = 1;
    fp.dbu               = 1;
    grid.cols = grid.regions = 2;
    grid.rows                = 1;
    grid.site = grid.row_h = 1;
    grid.tile_w = grid.sites = 100;
    grid.tile_h              = 2;
    grid.region              = {0, 1};
    grid.key                 = {0, 0};
    grid.keys                = {{{0, 0, 100}}};
    grid.placeable_sites     = {100, 100};
  }
  void cell(double width, double height = 1, int64_t x = 0) {
    const auto index  = fp.components.size();
    const auto name   = "cell" + std::to_string(index);
    auto&      master = tech.macros[name];
    master.width      = width;
    master.height     = height;
    master.klass      = "CORE";
    Ann_place pose(x, 0, std::llround(width), std::llround(height));
    pose.status = physical::Status::PLACED;
    fp.components.push_back({name, name, pose});
    design.macro.push_back(&master);
    design.cell.push_back(int(index));
    design.movable.push_back(int(index));
    design.width.push_back(std::llround(width));
    design.tall.push_back(height == 2);
  }
  Legroute_gate_report assess(const Global_routes& routes = {}, const Boxes* boxes = nullptr, bool ready = true) {
    return assess_legroute_gates(design, grid, boxes, routes, {}, capacity, capacity, ready);
  }
};
}  // namespace

TEST(LegrouteGate, PhysicalAreaChecksTheNinetyPercentCeilingAndBothRows) {
  Fixture f;
  f.cell(90);
  EXPECT_DOUBLE_EQ(f.assess().max_cell_density, .90);
  EXPECT_FALSE(f.assess().requires_replan());
  f.tech.macros.at("cell0").width = 91;
  auto dense                      = f.assess();
  EXPECT_DOUBLE_EQ(dense.max_cell_density, .91);
  EXPECT_DOUBLE_EQ(dense.density_limit, .90);
  EXPECT_EQ(dense.cell_dense_regions, 1u);
  EXPECT_EQ(dense.affected_components, std::vector<size_t>{0});
  ASSERT_EQ(dense.hotspots.size(), 1u);
  EXPECT_EQ(dense.hotspots[0].xl, 0);
  EXPECT_EQ(dense.hotspots[0].xh, 100);
  f.tech.macros.at("cell0").width  = 45.5;
  f.tech.macros.at("cell0").height = 2;
  EXPECT_DOUBLE_EQ(f.assess().max_cell_density, .91);
  // an explicitly looser ceiling is honoured (historical .95 controls)
  f.options.density_ceiling = .95;
  EXPECT_FALSE(f.assess().requires_replan());
  f.options.density_ceiling = .90;
  f.grid.placeable_sites[0] = 0;
  dense                     = f.assess();
  EXPECT_TRUE(std::isinf(dense.max_cell_density));
  rapidjson::Document json;
  json.Parse(legroute_gate_json(dense).c_str());
  ASSERT_FALSE(json.HasParseError());
  EXPECT_TRUE(json["max_cell_density"].IsNull());
  EXPECT_EQ(json["regions"][0]["placeable_area_dbu2"].GetDouble(), 0);
}

TEST(LegrouteGate, AssignedBoxTileWinsOverItsOriginalCentroid) {
  Fixture f;
  f.cell(10);
  f.grid.placeable_sites[1] = 10;
  Boxes boxes;
  boxes.box_of = {0};
  boxes.boxes.emplace_back();
  boxes.boxes[0].cells = {0};
  boxes.boxes[0].tile  = 1;
  boxes.boxes[0].cx    = 5;
  const auto moved     = f.assess({}, &boxes);
  EXPECT_EQ(moved.regions[0].cells, 0u);
  EXPECT_EQ(moved.regions[1].cells, 1u);
  EXPECT_DOUBLE_EQ(moved.max_cell_density, 1);
  EXPECT_EQ(moved.hotspots[0].xl, 100);
  boxes.boxes[0].tile   = -1;
  const auto unassigned = f.assess({}, &boxes);
  EXPECT_EQ(unassigned.regions[0].cells, 1u);
  EXPECT_FALSE(unassigned.requires_replan());
}

TEST(LegrouteGate, RawRoutingDensityChecksNinetyBeforeIntegerOverflow) {
  Fixture       f;
  Global_routes routes;
  routes.trees.resize(18);
  for (auto& tree : routes.trees) {
    tree.routed = true;
    tree.edges  = {
        {0, 1}
    };
  }
  EXPECT_DOUBLE_EQ(f.assess(routes).max_routing_density, .90);
  EXPECT_FALSE(f.assess(routes).requires_replan());
  routes.trees.push_back(routes.trees.front());
  EXPECT_TRUE(f.assess(routes).requires_replan());  // 19 of 20: over the ceiling
  routes.trees.push_back(routes.trees.front());
  const auto saturated = f.assess(routes);
  EXPECT_EQ(saturated.routing_dense_edges, 1u);
  EXPECT_DOUBLE_EQ(saturated.max_routing_density, 1);
  EXPECT_EQ(saturated.overflow_tracks, 0u);  // catches 100% even without old overflow metadata
  EXPECT_TRUE(saturated.requires_replan());
  EXPECT_EQ(saturated.hotspots.size(), 1u);
}

TEST(LegrouteGate, EveryConfiguredOversizeIsBlockedOnlyAfterCutsAreReady) {
  Fixture f;
  f.cell(5);
  f.cell(5, 1, 10);
  f.options.region_cell_limit = 1;
  EXPECT_EQ(f.assess({}, nullptr, false).oversized_regions, 0u);
  auto too_large = f.assess();
  EXPECT_EQ(too_large.oversized_regions, 1u);
  EXPECT_EQ(too_large.affected_components.size(), 2u);
  f.options.partition_cells = 2;
  EXPECT_FALSE(f.assess().requires_replan());
}

TEST(LegrouteGate, MovableBoxesNeedDistinctFittingTilesWithoutChangingOwnership) {
  Fixture f;
  f.cell(10);
  f.cell(10);
  f.options.partition_cells = 2;
  f.grid.blocked            = {0, 0};
  Boxes boxes;
  boxes.box_of = {0, 1};
  for (int c = 0; c < 2; ++c) {
    Box box;
    box.cells  = {c};
    box.sites  = 10;
    box.cx     = 5;
    box.region = 0;
    boxes.boxes.push_back(box);
  }
  const auto report = f.assess({}, &boxes);
  EXPECT_EQ(report.unfittable_regions, 1u);
  EXPECT_EQ(report.unfittable_boxes, 1u);
  EXPECT_EQ(report.regions[0].unfittable_boxes, 1u);
  EXPECT_EQ(report.cell_dense_regions, 0u);
  EXPECT_EQ(report.oversized_regions, 0u);
  EXPECT_TRUE(report.requires_replan());
  EXPECT_EQ(report.affected_components, (std::vector<size_t>{0, 1}));
  EXPECT_EQ(report.hotspots.size(), 1u);
  for (const auto& box : boxes.boxes) {
    EXPECT_EQ(box.region, 0);
    EXPECT_EQ(box.tile, -1);
    EXPECT_EQ(box.cx, 5);
  }
  // The neighboring region's empty tile cannot silently acquire either box.
  f.grid.region = {0, 0};
  EXPECT_FALSE(f.assess({}, &boxes).requires_replan());
}

TEST(LegrouteGate, SearchBudgetFeedbackIsSeparateFromCapacityAndHasEndpoints) {
  Fixture f;
  f.cell(5);
  f.cell(5, 1, 110);
  Net net;
  net.routed = true;
  net.pins   = {{.comp = 0}, {.comp = 1}};
  f.design.nets.push_back(net);
  Global_routes routes;
  routes.unrouted = 1;
  routes.trees.emplace_back();
  routes.trees[0].search_limited = true;
  const auto limited             = f.assess(routes);
  EXPECT_TRUE(limited.requires_replan());
  EXPECT_EQ(limited.search_limited, 1u);
  EXPECT_EQ(limited.routing_dense_edges, 0u);
  EXPECT_EQ(limited.cell_dense_regions, 0u);
  EXPECT_EQ(limited.overflow_tracks, 0u);
  EXPECT_EQ(limited.affected_components, (std::vector<size_t>{0, 1}));
  EXPECT_FALSE(limited.hotspots.empty());
  EXPECT_THROW(throw Legroute_replan(limited), Legroute_replan);
}

TEST(LegrouteGate, WindowFitFlagsOnlyTheOverfullCoarseWindow) {
  Fixture f;
  f.cell(95);  // centre in tile 0: 95 of its 100 free units
  // window 1 is one tile here (the tile is much wider than tall): two windows, one over 0.90
  auto fit = assess_window_fit(f.design, f.grid, .90, 1);
  EXPECT_EQ(fit.cols, 1);
  EXPECT_EQ(fit.rows, 1);
  EXPECT_EQ(fit.windows, 2u);
  EXPECT_EQ(fit.dense_windows, 1u);
  EXPECT_DOUBLE_EQ(fit.max_density, .95);
  EXPECT_DOUBLE_EQ(fit.tile_pressure[0], .95 / .90);
  EXPECT_DOUBLE_EQ(fit.tile_pressure[1], 1);  // the empty neighbour gets no pressure
  // Under a looser regional ceiling the region gate passes it: the window check is what catches it.
  f.options.density_ceiling = .95;
  EXPECT_FALSE(f.assess().requires_replan());
  f.options.density_ceiling = .90;
  // At or under the cap, and with the check disabled, nothing is dense.
  EXPECT_EQ(assess_window_fit(f.design, f.grid, .95, 1).dense_windows, 0u);
  EXPECT_EQ(assess_window_fit(f.design, f.grid, 0, 1).dense_windows, 0u);
  // A window as wide as the core sees the average.
  const auto wide = assess_window_fit(f.design, f.grid, .90, 2);
  EXPECT_EQ(wide.cols, 2);
  EXPECT_EQ(wide.dense_windows, 0u);
  EXPECT_DOUBLE_EQ(wide.max_density, .475);
}

TEST(LegrouteGate, WindowFitTreatsCellsWithoutFreeSitesAsOverfull) {
  Fixture f;
  f.grid.placeable_sites = {0, 100};
  f.cell(10);
  const auto fit = assess_window_fit(f.design, f.grid, .90, 1);
  EXPECT_EQ(fit.dense_windows, 1u);
  EXPECT_TRUE(std::isinf(fit.max_density));
  EXPECT_GT(fit.tile_pressure[0], 1);
}

TEST(LegrouteGate, BoxFitRejectsCollidingFixedBoxesAndPigeonholeCounts) {
  Fixture f;
  f.grid.blocked = {0, 0};
  f.cell(10);
  f.cell(10);
  Boxes boxes;
  boxes.box_of = {0, 1};
  for (int i = 0; i < 2; ++i) {
    boxes.boxes.emplace_back();
    boxes.boxes[size_t(i)].cells = {i};
    boxes.boxes[size_t(i)].sites = 10;
    boxes.boxes[size_t(i)].fixed = true;
    boxes.boxes[size_t(i)].tile  = 0;  // both claim tile 0
  }
  auto fit = match_box_fit(f.design, f.grid, boxes, false);
  EXPECT_FALSE(fit.fits);
  EXPECT_EQ(fit.reason, "fixed");
  // Distinct fixed tiles fit, with nothing to move.
  boxes.boxes[1].tile = 1;
  fit                 = match_box_fit(f.design, f.grid, boxes, false);
  EXPECT_TRUE(fit.fits) << fit.reason;
  EXPECT_EQ(fit.movable_boxes, 0u);
  // Two movable boxes on a one-tile-free grid: a pigeonhole, rejected without search.
  boxes.boxes[0].fixed = boxes.boxes[1].fixed = false;
  f.grid.blocked       = {0, 1};
  fit                  = match_box_fit(f.design, f.grid, boxes, false);
  EXPECT_EQ(fit.reason, "count");
  EXPECT_EQ(fit.movable_boxes, 2u);
  EXPECT_EQ(fit.free_tiles, 1u);
  EXPECT_TRUE(fit.pressure_boxes.empty());  // a pigeonhole names no box: the caller spreads everything
}
TEST(LegrouteGate, BoxFitRescuesAtFourWindowsWhatTwoCannotPlaceWithinTheQuota) {
  Fixture f;
  f.options.window_tiles = 1;
  f.grid.cols = f.grid.regions = 110;
  f.grid.region.assign(110, 0);
  f.grid.regions = 1;
  f.grid.key.assign(110, 0);
  f.grid.placeable_sites.assign(110, 100);
  f.grid.blocked.assign(110, 0);
  f.grid.blocked[1] = f.grid.blocked[2] = 1;  // the two tiles next to home 0
  Boxes boxes;
  const auto add = [&](int home) {
    f.cell(10);
    boxes.box_of.push_back(int(boxes.boxes.size()));
    boxes.boxes.emplace_back();
    auto& box = boxes.boxes.back();
    box.cells = {int(f.design.movable.size()) - 1};
    box.sites = 10;
    box.cx    = double(f.grid.cx(home));
    box.cy    = double(f.grid.cy(0));
  };
  add(0);
  add(0);  // one of the two must leave home 0: tiles 1-2 are blocked, tile 3 is three windows away
  for (int home = 10; home < 108; ++home) {
    add(home);
  }
  auto fit = match_box_fit(f.design, f.grid, boxes, false);
  EXPECT_TRUE(fit.fits) << fit.reason;
  EXPECT_EQ(fit.movable_boxes, 100u);
  EXPECT_EQ(fit.far_boxes, 1u);  // within 1% of 100
  EXPECT_EQ(fit.max_move, 3u);   // beyond 2r, within 4r
  // a third box at home 0 is a second far box: over the quota
  add(0);
  fit = match_box_fit(f.design, f.grid, boxes, false);
  EXPECT_FALSE(fit.fits);
  EXPECT_FALSE(fit.applied);
}

TEST(LegrouteGate, EdgeLimitIsTheNarrowerOfCapScaleAndTheCeiling) {
  Legroute_options o;
  EXPECT_EQ(edge_limit(20, o), 18);  // C20 admits 18, rejects 19
  o.cap_scale = .8;
  EXPECT_EQ(edge_limit(20, o), 16);  // not 14: the ceiling does not multiply cap_scale
  o.cap_scale    = 1;
  o.global_share = .5;
  EXPECT_DOUBLE_EQ(admission_share(o), .5);  // a more conservative share stays itself, not .45
  EXPECT_EQ(edge_limit(1, o), 0);
}

TEST(LegrouteGate, ClockTreeChargeMatchesTheTreeClockSpinesBuilds) {
  Fixture f;
  f.grid.cols    = 2;
  f.grid.rows    = 5;
  f.grid.region  = std::vector<int>(10, 0);
  f.grid.regions = 1;
  f.grid.layers  = {{.name = "M2", .horizontal = false}, {.name = "M3", .horizontal = true}};
  f.options.rib_tiles = 4;
  Net clk;
  clk.clock = true;
  Boxes boxes;
  // flops in rows 1, 2, 3 of column 0 and row 1 of column 1: clock_spines centres one rib on row 3
  for (const int tile : {2, 4, 6, 3}) {
    f.cell(10);
    const int m = int(f.design.movable.size()) - 1;
    boxes.box_of.push_back(int(boxes.boxes.size()));
    boxes.boxes.emplace_back();
    boxes.boxes.back().cells = {m};
    boxes.boxes.back().tile  = tile;
    clk.pins.push_back({.comp = f.design.movable.back(), .clock = true});
  }
  f.fp.terminals.push_back({.name = "clk", .x = 0, .y = 0});
  clk.pins.insert(clk.pins.begin(), {.term = 0, .output = true});
  clk.driver = 0;
  f.design.nets.push_back(clk);
  const auto domain = plan_clock_domain(f.grid, 0, {2, 4, 6, 3}, 4);
  EXPECT_EQ(domain.ribs, std::vector<int>{3});
  EXPECT_EQ(domain.tap_rib, (std::vector<int>{3, 3, 3, 3}));
  const auto charge = clock_tree_charge(f.design, f.grid, &boxes, 4);
  Routing    R;
  R.cap_h.assign(f.grid.size(), 0);
  R.cap_v.assign(f.grid.size(), 0);
  R.layer_dem.assign(2, std::vector<int>(f.grid.size(), 0));
  clock_spines(f.design, f.grid, boxes, R);
  // plus the driver/entry joins, each an L horizontal first, as legroute shapes them after L4
  ASSERT_FALSE(R.conns.empty());
  for (const auto& c : R.conns) {
    const int ta = R.trees[size_t(c.net)].nodes[size_t(c.a)].tile, tb = R.trees[size_t(c.net)].nodes[size_t(c.b)].tile;
    const int ca = f.grid.col(ta), ra = f.grid.row(ta), cb = f.grid.col(tb), rb = f.grid.row(tb);
    for (int x = std::min(ca, cb); ca != cb && x <= std::max(ca, cb); ++x) {
      --R.cap_h[size_t(f.grid.tile(x, ra))];
    }
    for (int y = std::min(ra, rb); ra != rb && y <= std::max(ra, rb); ++y) {
      --R.cap_v[size_t(f.grid.tile(cb, y))];
    }
  }
  for (size_t t = 0; t < f.grid.size(); ++t) {
    EXPECT_EQ(charge.v[t], -R.cap_v[t]) << "tile " << t;
    EXPECT_EQ(charge.h[t], -R.cap_h[t]) << "tile " << t;
  }
  // unplaced boxes are charged at their centroid tiles
  for (auto& box : boxes.boxes) {
    box.cx  = double(f.grid.cx(f.grid.col(box.tile)));
    box.cy  = double(f.grid.cy(f.grid.row(box.tile)));
    box.tile = -1;
  }
  EXPECT_EQ(clock_tree_charge(f.design, f.grid, &boxes, 4).v, charge.v);
}
}  // namespace livehd::placement::lr
