// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <set>

#include "gtest/gtest.h"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/routing_rules.hpp"
#include "inou/lefdef/signal_via.hpp"
#include "legroute_internal.hpp"
#include "signal_router.hpp"

namespace livehd::placement::lr {
namespace {
struct Final_placement {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  Design               design{tech, fp, options};
  Grid                 grid;
  Boxes                boxes;
  Routing              routes;

  Final_placement() {
    grid.cols   = 2;
    grid.rows   = 1;
    grid.site   = 50;
    grid.row_h  = 250;
    grid.tile_w = 800;
    grid.tile_h = 500;
    grid.sites  = 16;
    grid.key    = {0, 0};
    grid.keys   = {
        {{0, 0, 16}, {1, 0, 16}}
    };
    for (int i = 0; i < 2; ++i) {
      physical::Component cell;
      cell.name         = "c" + std::to_string(i);
      cell.macro        = "gate";
      cell.place.x      = i ? 350 : 100;
      cell.place.y      = 0;
      cell.place.width  = i ? 150 : 100;
      cell.place.height = 250;
      cell.place.status = physical::Status::PLACED;
      fp.components.push_back(cell);
    }
    design.movable = {0, 1};
    design.width   = {100, 150};
    design.tall    = {false, false};
    design.nets_of.resize(2);
    Box box;
    box.cells  = {0, 1};
    box.tile   = 0;
    box.cx     = 315;
    box.cy     = 125;
    box.placed = true;
    boxes.boxes.push_back(box);
    boxes.box_of = {0, 0};
  }
};

// Isolate final's structured tree from SAT/placement. Both endpoints are actual
// clock ports, so a broken tree cannot be hidden by a cell-to-cell whole route.
struct Final_io {
  physical::Technology   tech;
  physical::Floorplan    fp;
  physical::Tech_profile profile;
  Legroute_options       options;
  Design                 design{tech, fp, options, &profile};
  Grid                   grid;
  Routing                routes;

  Final_io(int64_t x0, int64_t y0) {
    tech.dbu = fp.dbu       = 1000;
    tech.manufacturing_grid = .001;
    fp.die                  = {double(x0), double(y0), double(x0 + 1600), double(y0 + 1500)};
    for (int k = 1; k <= 4; ++k) {
      const auto name  = "M" + std::to_string(k);
      const bool h     = k % 2 == 0;
      auto&      layer = tech.layers[name];
      layer.width      = .02;
      layer.direction  = h ? "HORIZONTAL" : "VERTICAL";
      layer.pitch_x = layer.pitch_y = .05;
      layer.rules.loaded            = true;
      layer.rules.spacing           = {
          {.02, {}, {}}
      };
      layer.rules.properties["LEF58_RIGHTWAYONGRIDONLY"] = {"RIGHTWAYONGRIDONLY ;"};
      tech.routing_layers.push_back(name);
      fp.tracks.push_back({h ? "Y" : "X", name, (h ? y0 : x0) + 25, h ? 30 : 32, 50});
      if (k >= 3) {
        grid.layers.push_back({name, "", h, 50, (h ? y0 : x0) + 25, 20});
      }
      if (k > 1) {
        const auto lower = "M" + std::to_string(k - 1), cut = "V" + std::to_string(k - 1);
        tech.cut_layers[cut].width   = .02;
        tech.cut_layers[cut].spacing = {.02};
        auto& via                    = tech.vias[cut + "_DEF"];
        via.is_default               = true;
        via.shapes                   = {
            {lower, h ? physical::Rect{-.01, -.02, .01, .02} : physical::Rect{-.02, -.01, .02, .01}},
            {  cut,                                                          {-.01, -.01, .01, .01}},
            { name, h ? physical::Rect{-.02, -.01, .02, .01} : physical::Rect{-.01, -.02, .01, .02}}
        };
      }
    }
    profile.route.access        = {"M1", "M2", "V1"};
    profile.route.branch_layer  = "M3";
    profile.route.branch_cut    = "V2";
    profile.route.signal_layers = {"M2", "M3", "M4"};
    options.threads             = 1;
    options.route_passes        = 1;
    grid.cols                   = 2;
    grid.rows                   = 3;
    grid.x0                     = x0;
    grid.y0                     = y0;
    grid.tile_w                 = 800;
    grid.tile_h                 = 500;
    grid.box_layer              = "M2";
    grid.die                    = fp.die;
    // Asymmetric local rectangle + nonzero pin origin: center x136 is off
    // the M3 grid, while x125 is a legal access inside the unchanged pin.
    fp.terminals.push_back({"clk_in",
                            "clk",
                            "INPUT",
                            x0 + 121,
                            y0 + 376,
                            physical::Orientation::N,
                            physical::Status::FIXED,
                            {{"M4", {-26, -11, 56, 9}}}});
    fp.terminals.push_back({"clk_out",
                            "clk",
                            "OUTPUT",
                            x0 + 1321,
                            y0 + 1126,
                            physical::Orientation::N,
                            physical::Status::FIXED,
                            {{"M4", {-26, -11, 56, 9}}}});
    fp.nets.push_back({
        "clk",
        {{"PIN", "clk_in"}, {"PIN", "clk_out"}},
        {},
        "CLOCK"
    });
    design.nets.resize(1);
    design.nets[0].clock = true;
    design.nets[0].pins  = {{.term = 0}, {.term = 1}};
    routes.trees.resize(1);
    routes.trees[0].nodes = {
        {.tile = 0, .term = 0, .pins = {0}, .riser_layer = 0,  .riser_track = 4, .stub_track = 10},
        {.tile = 5, .term = 1, .pins = {1}, .riser_layer = 0, .riser_track = 24, .stub_track = 20}
    };
    routes.segments.push_back({.net = 0, .layer = 1, .band = 1, .lo = 0, .hi = 1, .node_lo = 0, .node_hi = 1, .track = 15});
  }
};
}  // namespace

TEST(LegrouteFinal, PendingCutsRejectConflictingPiecesAtomicallyAndAllowLaterCandidates) {
  Final_io f(0, 0);
  f.tech.cut_layers["V3"].spacing      = {.034};
  f.tech.vias["V3_DEF"].shapes[1].rect = {-.009, -.012, .009, .012};
  const auto        path               = [](int64_t y) { return physical::via_path("M3", 125, y, "V3_DEF"); };
  const std::vector first{path(100)}, near{path(148)};
  // Each piece is valid alone; their cut edge gap is 24 DBU, below 34.
  for (const auto& paths : {first, near}) {
    Final_cut_state alone(f.tech, f.fp);
    EXPECT_TRUE(alone.add(paths).empty());
  }
  Final_cut_state pending(f.tech, f.fp);
  ASSERT_TRUE(pending.add(first).empty());
  EXPECT_EQ(pending.add(near), "self cut spacing across route pieces");
  // The failed piece first adds a distant legal via, then the conflicting one.
  // Its geometry and identity insertions must both roll back.
  const std::vector rejected{path(1000), path(148)};
  EXPECT_EQ(pending.add(rejected), "self cut spacing across route pieces");
  const std::vector later{path(1048)};  // would conflict with leaked y1000
  EXPECT_TRUE(pending.add(later).empty());
  EXPECT_TRUE(pending.add(first).empty());  // exact reuse of the accepted piece
  EXPECT_EQ(pending.add(near), "self cut spacing across route pieces");
  const std::vector threshold{path(158)};  // gap == 34 is permitted
  EXPECT_TRUE(pending.add(threshold).empty());
  const auto context = pending.vias();
  ASSERT_EQ(context.size(), 3u);
  EXPECT_EQ(context[0].via.name, "V3_DEF");
  EXPECT_EQ(context[0].via.orientation, physical::Orientation::N);
  EXPECT_EQ(context[0].y, 100);
  EXPECT_EQ(context[1].y, 158);
  EXPECT_EQ(context[2].y, 1048);  // rejected y1000/y148 never became context
}

TEST(LegrouteFinal, PendingCutsDeduplicateOnlyExactViaIdentity) {
  Final_io f(0, 0);
  f.tech.vias["V3_ALIAS"] = f.tech.vias.at("V3_DEF");
  Final_cut_state   pending(f.tech, f.fp);
  const std::vector first{physical::via_path("M3", 125, 375, "V3_DEF")};
  ASSERT_TRUE(pending.add(first).empty());
  EXPECT_TRUE(pending.add(first).empty());
  auto alternate                                            = first;
  std::get<physical::Route_via>(alternate[0].steps[1]).name = "V3_ALIAS";
  EXPECT_EQ(pending.add(alternate), "self cut spacing across route pieces");
  alternate                                                        = first;
  std::get<physical::Route_via>(alternate[0].steps[1]).orientation = physical::Orientation::S;
  EXPECT_EQ(pending.add(alternate), "self cut spacing across route pieces");
  alternate                                                 = first;
  std::get<physical::Route_point>(alternate[0].steps[0]).x += 1;
  EXPECT_EQ(pending.add(alternate), "self cut spacing across route pieces");
  EXPECT_TRUE(pending.add(first).empty());
}

TEST(LegrouteFinal, PendingCutContextFindsALegalTreeInsteadOfFailingAtFinalUnion) {
  Final_io f(0, 0);
  // Independently legal joins at x125 and x225 on y525 leave 80 DBU
  // between their 20-DBU cuts. Neither proposal sees the other's occupancy.
  f.tech.cut_layers["V3"].spacing = {.09};
  Legroute_report report;
  final_wires(f.design, f.grid, {}, f.routes, {}, report);
  EXPECT_EQ(report.tree_fallbacks, 0u);
  ASSERT_TRUE(report.wires_emitted);
  EXPECT_EQ(report.clock_failed_nets, 0u);
  EXPECT_EQ(report.clock_connected_nets, 1u);
  EXPECT_TRUE(report.failure_sample.empty());
  EXPECT_GT(report.final_prior_cut_rejections, 0u);
  EXPECT_EQ(report.final_pending_cut_rejections, 0u);
  EXPECT_EQ(report.final_union_cut_rejections, 0u);
  EXPECT_TRUE(physical::check_route_connectivity(f.tech, f.fp, f.fp.nets[0]).connected);
  // Validate every emitted cut independently of Final_cut_state and the
  // search's prefix screen. Only exact same-instance reuse is deduplicated.
  physical::Signal_cut_set                                                   cuts(f.tech, f.fp);
  std::set<std::tuple<std::string, physical::Orientation, int64_t, int64_t>> seen;
  for (const auto& wire : f.fp.nets[0].wires) {
    for (const auto& path : wire.paths) {
      physical::Route_point point;
      for (const auto& step : path.steps) {
        if (const auto* at = std::get_if<physical::Route_point>(&step)) {
          point = *at;
        } else if (const auto* via = std::get_if<physical::Route_via>(&step)) {
          if (seen.emplace(via->name, via->orientation, point.x, point.y).second) {
            EXPECT_TRUE(cuts.add(*via, point.x, point.y)) << via->name << " at " << point.x << ',' << point.y;
          }
        }
      }
    }
  }
  EXPECT_GT(seen.size(), 2u);
}

TEST(LegrouteFinal, FailedContextSearchDoesNotPublishPendingGeometry) {
  Final_io f(0, 0);
  f.tech.cut_layers["V3"].spacing = {.09};
  f.options.route_patterns        = 1;
  const auto      before          = f.fp;
  Legroute_report report;
  final_wires(f.design, f.grid, {}, f.routes, {}, report);
  EXPECT_FALSE(report.wires_emitted);
  EXPECT_EQ(report.clock_failed_nets, 1u);
  EXPECT_TRUE(f.fp.nets[0].wires.empty());
  EXPECT_EQ(f.fp.nondefault_rules, before.nondefault_rules);
  EXPECT_EQ(f.fp.components, before.components);
  EXPECT_EQ(f.fp.terminals, before.terminals);
}

TEST(LegrouteFinal, OffTrackIoCenterAttachesToExplicitTracksWithoutMovingThePin) {
  for (const auto& [x0, y0] : {
           std::pair<int64_t, int64_t>{   0,     0},
           {1700,  2300},
           {-500, -1000}
  }) {
    Final_io f(x0, y0);
    // A second finite pattern supplies access; extrapolating the first is invalid.
    f.fp.tracks[2].count = 2;
    f.fp.tracks.push_back({"X", "M3", x0 + 125, 30, 50});
    const auto      pins = f.fp.terminals;
    Legroute_report report;
    final_wires(f.design, f.grid, {}, f.routes, {}, report);
    ASSERT_TRUE(report.wires_emitted) << (report.failure_sample.empty() ? "" : report.failure_sample.back());
    EXPECT_EQ(report.clock_connected_nets, 1u);
    EXPECT_EQ(report.tree_fallbacks, 0u);
    EXPECT_EQ(report.global_conflict_nets, 0u);
    EXPECT_EQ(f.fp.terminals, pins);
    EXPECT_TRUE(physical::check_route_connectivity(f.tech, f.fp, f.fp.nets[0]).connected);
    const physical::Signal_metal_rules rules(f.tech, f.fp);
    bool                               on_selected_track = false;
    for (const auto& shape : physical::route_geometry(f.tech, f.fp, f.fp.nets[0])) {
      EXPECT_GE(shape.rect.xl, f.fp.die.xl);
      EXPECT_GE(shape.rect.yl, f.fp.die.yl);
      EXPECT_LE(shape.rect.xh, f.fp.die.xh);
      EXPECT_LE(shape.rect.yh, f.fp.die.yh);
      if (f.tech.layers.contains(shape.layer)) {
        const auto check = rules.rectangle(shape);
        EXPECT_TRUE(check.legal) << shape.layer << ": " << check.reason;
      }
      on_selected_track |= shape.layer == "M3" && (shape.rect.xl + shape.rect.xh) / 2 == x0 + 125 && shape.rect.yl <= y0 + 375
                           && shape.rect.yh >= y0 + 525;
    }
    EXPECT_TRUE(on_selected_track);
  }
}

TEST(LegrouteFinal, NoExplicitIoTrackFallsBackWithoutPublishingPartialStubMetal) {
  for (bool finite_count : {false, true}) {
    Final_io f(1700, 2300);
    if (finite_count) {
      f.fp.tracks[2].count = 1;  // an extrapolated x1825 is not an actual track
    } else {
      // x1800..1810 contains no M3 track (1825 is the next one).
      f.fp.terminals[0].shapes[0].rect = {-21, -11, -11, 9};
    }
    const auto      pins = f.fp.terminals;
    Legroute_report report;
    final_wires(f.design, f.grid, {}, f.routes, {}, report);
    EXPECT_EQ(f.fp.terminals, pins);
    EXPECT_EQ(report.tree_fallbacks, 1u);
    EXPECT_EQ(report.clock_failed_nets, 1u);
    EXPECT_EQ(report.clock_unchecked_nets, 0u);
    EXPECT_FALSE(report.wires_emitted);
    EXPECT_TRUE(f.fp.nets[0].wires.empty());
    ASSERT_FALSE(report.failure_sample.empty());
    EXPECT_NE(report.failure_sample.front().find("no explicit preferred track"), std::string::npos);
  }
}

TEST(LegrouteFinal, RejectsOffTrackOrOutsideDieStructuredMetalBeforeUsingItAsAnAnchor) {
  for (bool outside_die : {false, true}) {
    Final_io f(0, 0);
    if (outside_die) {
      f.routes.trees[0].nodes[0].riser_track = 36;
    } else {
      ++f.grid.layers[0].offset;  // assigned grid disagrees with the actual DEF tracks
    }
    const auto      pins = f.fp.terminals;
    Legroute_report report;
    final_wires(f.design, f.grid, {}, f.routes, {}, report);
    EXPECT_EQ(f.fp.terminals, pins);
    EXPECT_EQ(report.global_conflict_nets, 1u);
    EXPECT_EQ(report.clock_failed_nets, 1u);
    EXPECT_FALSE(report.wires_emitted);
    EXPECT_TRUE(f.fp.nets[0].wires.empty());
    ASSERT_FALSE(report.failure_sample.empty());
    EXPECT_NE(report.failure_sample.front().find(outside_die ? "outside die" : "preferred-direction track"), std::string::npos);
  }
}

TEST(LegrouteFinal, KeepsTheExactPlacementOfAnUnmovedLegalBox) {
  Final_placement f;
  const auto      before = f.fp.components;
  final_place(f.design, f.grid, f.boxes, f.routes, 2);
  EXPECT_EQ(f.fp.components, before);
}

TEST(LegrouteFinal, StillOptimizesABoxThatWasMovedToAnotherTile) {
  Final_placement f;
  f.boxes.boxes[0].tile = 1;
  for (auto& cell : f.fp.components) {
    cell.place.x += f.grid.tile_w;  // interim packing on the SAT-selected tile
  }
  const auto before = f.fp.components;
  final_place(f.design, f.grid, f.boxes, f.routes, 2);
  EXPECT_NE(f.fp.components, before);
  for (const auto& cell : f.fp.components) {
    EXPECT_GE(cell.place.x, f.grid.tile_w);
    EXPECT_LE(cell.place.x + cell.place.width, 2 * f.grid.tile_w);
    EXPECT_EQ(cell.place.y, 0);
  }
}

TEST(LegrouteFinal, CountsAnUnroutedSignalEvenWhenItsGlobalMetalIsMissing) {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  options.threads = 1;
  Design design{tech, fp, options};
  fp.nets.push_back({"missing_tree", {}, {}, "SIGNAL"});
  design.nets.resize(1);
  design.nets[0].frame  = 0;
  design.nets[0].routed = true;  // a signal already selected by build_design
  Routing routes;
  routes.trees.resize(1);
  Grid grid;
  grid.layers = {
      {"M3", "", false, 100, 50, 10},
      {"M4", "",  true, 100, 50, 10}
  };
  Boxes           boxes;
  Legroute_report report;
  final_wires(design, grid, boxes, routes, {}, report);
  EXPECT_EQ(report.routed_nets, 1u);
  EXPECT_EQ(report.failed_nets, 1u);  // no router profile is available; omission is not success
  EXPECT_FALSE(report.wires_emitted);
  EXPECT_TRUE(fp.nets[0].wires.empty());
}
TEST(LegrouteFinal, DiagnosticLimitCountsUncheckedNetsWithoutDroppingTheDesign) {
  for (unsigned limit : {0u, 1u, 3u}) {
    physical::Technology tech;
    physical::Floorplan  fp;
    Legroute_options     options;
    options.threads         = 1;
    options.final_net_limit = limit;
    Design design{tech, fp, options};
    design.nets.resize(3);
    for (int n = 0; n < 3; ++n) {
      fp.nets.push_back({"signal" + std::to_string(n), {}, {}, "SIGNAL"});
      design.nets[n].frame  = n;
      design.nets[n].routed = true;
    }
    Routing routes;
    routes.trees.resize(3);
    Grid grid;
    grid.layers = {
        {"M3", "", false, 100, 50, 10},
        {"M4", "",  true, 100, 50, 10}
    };
    Boxes           boxes;
    Legroute_report report;
    final_wires(design, grid, boxes, routes, {}, report);
    const size_t attempted = limit ? limit : 3;
    EXPECT_EQ(report.routed_nets, 3u);
    EXPECT_EQ(report.failed_nets, attempted);  // no profile is an attempted failure, not an unchecked net
    EXPECT_EQ(report.unchecked_nets, 3u - attempted);
    EXPECT_EQ(report.connected_nets, 0u);
    EXPECT_FALSE(report.wires_emitted);
    for (const auto& net : fp.nets) {
      EXPECT_TRUE(net.wires.empty());
    }
  }
}
TEST(LegrouteFinal, MissingClockMetalCannotDisappearFromClosureOrDiagnosticAccounting) {
  for (unsigned limit : {0u, 1u}) {
    physical::Technology tech;
    physical::Floorplan  fp;
    Legroute_options     options;
    options.threads         = 1;
    options.final_net_limit = limit;
    Design design{tech, fp, options};
    design.nets.resize(2);
    fp.nets.push_back({"signal", {}, {}, "SIGNAL"});
    design.nets[0].frame  = 0;
    design.nets[0].routed = true;
    fp.nets.push_back({"clock", {}, {}, "CLOCK"});
    design.nets[1].frame = 1;
    design.nets[1].clock = true;
    design.nets[1].pins.resize(2);
    // build_design intentionally excludes clocks from the coarse signal request set.
    ASSERT_FALSE(design.nets[1].routed);
    Routing routes;
    routes.trees.resize(2);
    Grid grid;
    grid.layers = {
        {"M3", "", false, 100, 50, 10},
        {"M4", "",  true, 100, 50, 10}
    };
    Legroute_report report;
    final_wires(design, grid, {}, routes, {}, report);
    EXPECT_EQ(report.routed_nets, 2u);
    EXPECT_EQ(report.clock_required_nets, 1u);
    EXPECT_EQ(report.failed_nets, limit ? 1u : 2u);
    EXPECT_EQ(report.clock_failed_nets, limit ? 0u : 1u);
    EXPECT_EQ(report.unchecked_nets, limit ? 1u : 0u);
    EXPECT_EQ(report.clock_unchecked_nets, limit ? 1u : 0u);
    EXPECT_EQ(report.clock_connected_nets, 0u);
    EXPECT_FALSE(report.wires_emitted);
    EXPECT_TRUE(fp.nets[1].wires.empty());
  }
}
}  // namespace livehd::placement::lr

namespace livehd::placement::lr {
TEST(LegrouteFinal, PrecommitConnectivityRejectsADetachedPlannedIslandBeforeCommit) {
  // Advisor fixture: an extra planned M4 segment on track 0 (y25) with no join or node reach is a route-only island
  // while the two IO pins still connect. By default the late F4 check rejects it; with final_precommit_connectivity
  // the complete-net check rejects it before anything is committed.
  for (bool precommit : {false, true}) {
    Final_io f(0, 0);
    f.options.final_precommit_connectivity = precommit;
    f.routes.segments.push_back({.net = 0, .layer = 1, .band = 0, .lo = 0, .hi = 1, .track = 0});
    Legroute_report report;
    final_wires(f.design, f.grid, {}, f.routes, {}, report);
    EXPECT_EQ(report.clock_connected_nets, 0u);
    std::string reasons;
    for (const auto& r : report.failure_sample) {
      reasons += r + " | ";
    }
    if (precommit) {
      EXPECT_GE(report.final_precommit_disconnected, 1u);
      EXPECT_NE(reasons.find("precommit connectivity"), std::string::npos) << reasons;
      EXPECT_EQ(reasons.find("disconnected:"), std::string::npos) << reasons;
    } else {
      EXPECT_EQ(report.final_precommit_disconnected, 0u);
      EXPECT_NE(reasons.find("disconnected"), std::string::npos) << reasons;
    }
  }
}
TEST(LegrouteFinal, PrecommitConnectivityKeepsTheConnectedTree) {
  Final_io f(0, 0);
  f.options.final_precommit_connectivity = true;
  Legroute_report report;
  final_wires(f.design, f.grid, {}, f.routes, {}, report);
  ASSERT_TRUE(report.wires_emitted);
  EXPECT_EQ(report.clock_connected_nets, 1u);
  EXPECT_EQ(report.final_precommit_disconnected, 0u);
}
}  // namespace livehd::placement::lr

namespace livehd::placement::lr {
TEST(LegrouteFinal, AnIoStubOnThePinRowJoinsThePinDirectlyWithoutAPhantomLowerLayerStack) {
  // Advisor lfsr rst witness: the M4 IO pin's access row is the M4 stub track (ys == py), so no vertical M3 piece is
  // drawn. The planned joins must not record M4->M3 and M3->M4 at that point (there is no M3 metal to land on).
  Final_io f(0, 0);
  f.routes.trees[0].nodes[0].stub_track = 7;  // y 375: the clk_in pin row
  const auto geo = final_planned_geometry(f.design, f.grid, {}, f.routes);
  ASSERT_EQ(geo.joins.size(), 1u);
  for (const auto& [a, b, x, y] : geo.joins[0]) {
    // x125 is the pin access column; the riser join at x225 (M4 stub -> M3 riser) is real
    EXPECT_FALSE(x == 125 && y == 375 && (a == "M3" || b == "M3")) << a << "->" << b << " at " << x << "," << y;
  }
  Legroute_report report;
  final_wires(f.design, f.grid, {}, f.routes, {}, report);
  EXPECT_EQ(report.join_failures, 0u);
  EXPECT_EQ(report.clock_connected_nets, 1u);
}
}  // namespace livehd::placement::lr

namespace livehd::placement::lr {
TEST(LegrouteFinal, ASingleRibClockSpineRecordsNoJoinToItsUndrawnTrunk) {
  // minimax witness ("join: no M7 metal"): a spine with one drawn rib draws no trunk wire (ylo == yhi), so its
  // rib->trunk join would name trunk metal that does not exist. With two ribs the trunk and both joins remain.
  for (int ribs : {1, 2}) {
    Final_io f(0, 0);
    const int trunk = int(f.routes.segments.size());
    f.routes.segments.push_back({.net = 0, .layer = 0, .band = 0, .lo = 0, .hi = 2, .track = 5, .clock = true});
    Spine sp{.net = 0, .region = 0, .trunk = trunk, .node = -1};
    for (int r = 0; r < ribs; ++r) {
      sp.ribs.push_back(int(f.routes.segments.size()));
      f.routes.segments.push_back({.net = 0, .layer = 1, .band = r * 2, .lo = 0, .hi = 1, .track = 5 + 20 * r, .clock = true});
    }
    f.routes.spines.push_back(sp);
    const auto geo = final_planned_geometry(f.design, f.grid, {}, f.routes);
    const auto tx  = f.grid.track_coord(0, 5);
    size_t     trunk_joins = 0;
    for (const auto& [a, b, x, y] : geo.joins[0]) {
      trunk_joins += a == "M4" && b == "M3" && x == tx;
    }
    EXPECT_EQ(trunk_joins, ribs == 1 ? 0u : 2u) << ribs << " rib(s)";
  }
}
}  // namespace livehd::placement::lr
