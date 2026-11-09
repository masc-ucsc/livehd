// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

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
    grid.critical_box_of_tile.assign(grid.size(), -1);
    grid.critical_reserved_h.resize(grid.size());
    grid.critical_reserved_v.resize(grid.size());
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
    for (auto& node : routes.trees[0].nodes) {
      node.riser_state = node.stub_state = node.stub_access_state = uint8_t(Track_state::checked);
    }
    routes.segments[0].track_state = uint8_t(Track_state::checked);
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

TEST(LegrouteFinal, FixedLocalJoinsPublishACompleteCutLegalTree) {
  Final_io f(0, 0);
  // Keep the stringent cut rule, but provide a legal immutable Local tree.
  // Final must not relocate conflicting fixed cuts through the deleted route
  // search. The separate fixed-conflict test below verifies rejection.
  f.tech.cut_layers["V3"].spacing        = {.09};
  f.routes.trees[0].nodes[0].riser_track = 5;   // x275: 130-DBU cut edge gap from x125
  f.routes.trees[0].nodes[1].riser_track = 23;  // x1175
  f.routes.trees[0].nodes[1].stub_track  = 22;  // native output-pin row y1125, no phantom access stack
  Legroute_report report;
  final_wires(f.design, f.grid, {}, f.routes, {}, report);
  EXPECT_EQ(report.tree_fallbacks, 0u);
  ASSERT_TRUE(report.wires_emitted);
  EXPECT_EQ(report.clock_failed_nets, 0u);
  EXPECT_EQ(report.clock_connected_nets, 1u);
  EXPECT_TRUE(report.failure_sample.empty());
  EXPECT_EQ(report.final_prior_cut_rejections, 0u);
  EXPECT_EQ(report.final_pending_cut_rejections, 0u);
  EXPECT_EQ(report.final_union_cut_rejections, 0u);
  EXPECT_TRUE(physical::check_route_connectivity(f.tech, f.fp, f.fp.nets[0]).connected);
  // Validate every emitted cut independently of Final_cut_state. Only exact
  // same-instance reuse is deduplicated.
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

TEST(LegrouteFinal, OriginalFixedSourceMetalIsMandatoryAndKeepsItsMetadata) {
  for (int mode : {0, 1, 2}) {
    SCOPED_TRACE(mode);
    Final_io   f(0, 0);
    const auto native = final_planned_geometry(f.design, f.grid, {}, f.routes);
    auto       paths  = native.paths.at(0);
    paths.insert(paths.end(), native.adjacent_join_vias.at(0).begin(), native.adjacent_join_vias.at(0).end());
    f.fp.nets[0].wires = {
        {"FIXED", paths}
    };
    ASSERT_TRUE(physical::check_route_connectivity(f.tech, f.fp, f.fp.nets[0]).connected);
    if (mode) {
      // This original component is mandatory. It may neither disappear nor be
      // credited merely because the real pins connect through another tree.
      const double y = mode == 1 ? 25 : 26;
      f.fp.nets[0].wires[0].paths.push_back(physical::flush_wire_path(
          {
              "M4",
              {400, y - 10, 500, y + 10}
      },
          true));
    }
    f.routes = {};
    f.routes.trees.resize(1);
    const auto      before = f.fp;
    Legroute_report report;
    final_wires(f.design, f.grid, {}, f.routes, {}, report);
    std::string reasons;
    for (const auto& reason : report.failure_sample) {
      reasons += reason + " | ";
    }
    EXPECT_EQ(report.wires_emitted, mode == 0) << reasons;
    EXPECT_EQ(report.connected_nets, mode == 0 ? 1u : 0u) << reasons;
    EXPECT_EQ(f.fp.nets, before.nets);  // FIXED stays FIXED; no dummy ROUTED record.
    EXPECT_EQ(f.fp.components, before.components);
    EXPECT_EQ(f.fp.terminals, before.terminals);
    EXPECT_EQ(f.fp.nondefault_rules, before.nondefault_rules);
    if (mode) {
      EXPECT_EQ(report.failed_nets, 1u);
      EXPECT_NE(reasons.find(mode == 1 ? "disconnected" : "preferred-direction track"), std::string::npos) << reasons;
    }
  }
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

TEST(LegrouteFinal, NoExplicitIoTrackFailsWithoutPublishingPartialStubMetal) {
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
    EXPECT_EQ(report.tree_fallbacks, 0u);
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
  validate_final_membership(f.design, f.grid, f.boxes);
  EXPECT_EQ(f.fp.components, before);
}

TEST(LegrouteFinal, CriticalFourRowWideBoxKeepsEveryAdmittedPose) {
  Final_placement f;
  f.options.cellbox_full_flow = true;
  f.grid.rows_per_tile        = 4;
  f.grid.tile_h               = 1000;
  f.grid.keys                 = {
      {{0, 0, 16}, {1, 0, 16}, {2, 0, 16}, {3, 0, 16}}
  };
  auto& box          = f.boxes.boxes[0];
  box.critical_fixed = box.fixed = true;
  box.span_columns               = 2;
  box.cx                         = 800;  // The footprint center lies in its second tile.
  box.cy                         = 500;
  f.fp.components[1].place.x     = 1100;
  f.fp.components[1].place.y     = 750;
  const auto before              = f.fp.components;
  validate_final_membership(f.design, f.grid, f.boxes);
  EXPECT_EQ(f.fp.components, before);
}

TEST(LegrouteFinal, CriticalFinalRejectsAStaleFootprintWithoutMovingCells) {
  Final_placement f;
  f.options.cellbox_full_flow = true;
  auto& box                   = f.boxes.boxes[0];
  box.critical_fixed = box.fixed = true;
  f.fp.components[1].place.x     = f.grid.tile_w;
  const auto before              = f.fp.components;
  EXPECT_THROW(validate_final_membership(f.design, f.grid, f.boxes), std::logic_error);
  EXPECT_EQ(f.fp.components, before);
}

TEST(LegrouteFinal, MembershipValidationDoesNotPlaceBeforeJointSat) {
  Final_placement f;
  f.boxes.boxes[0].tile = 1;
  for (auto& cell : f.fp.components) {
    cell.place.x += f.grid.tile_w;  // interim packing on the SAT-selected tile
  }
  const auto before = f.fp.components;
  validate_final_membership(f.design, f.grid, f.boxes);
  EXPECT_EQ(f.fp.components, before);
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
    // Both modes require connectivity before the single placement+metal commit.
    EXPECT_GE(report.final_precommit_disconnected, 1u);
    EXPECT_NE(reasons.find("precommit connectivity"), std::string::npos) << reasons;
    EXPECT_FALSE(report.wires_emitted);
    EXPECT_TRUE(f.fp.nets[0].wires.empty());
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

TEST(LegrouteFinal, JointSatMovesCellsRoutesInternalNetAndExtendsIncomingOrRollsBack) {
  // 0 success; 1 first-box routing failure; 2 later-box failure after the
  // first box solved; 3 report I/O failure after solving; 4 wildcard PG
  // ownership; 5 a small native via plate contained in a legal source pin.
  for (const int mode : {0, 1, 2, 3, 4, 5, 6, 7}) {
    SCOPED_TRACE(mode);
    Final_io f(0, 0);
    f.options.cellbox_full_flow = true;
    f.options.route_ms          = 3000;
    f.grid.site                 = 50;
    f.grid.row_h                = 250;
    f.grid.sites                = 16;
    f.grid.key.assign(f.grid.size(), 0);
    f.grid.keys = {
        {{0, 0, 16}, {1, 0, 16}}
    };
    f.grid.row_present.assign(6, 1);
    f.grid.row_orient.assign(6, physical::Orientation::N);
    f.tech.sites["core"].width  = .05;
    f.tech.sites["core"].height = .25;
    for (int row = 0; row < 6; ++row) {
      f.fp.rows.push_back({"row" + std::to_string(row), "core", 0, row * 250, 32, 50, physical::Orientation::N});
    }
    auto& master     = f.tech.macros["dual"];
    master.klass     = "CORE";
    master.site      = "core";
    master.width     = .2;
    master.height    = .25;
    master.pins["A"] = {"INPUT", "SIGNAL", {{"M1", {.015, .015, .035, .235}}}};
    // This short real pin can reach H275 only after the cell moves to row1.
    master.pins["Y"] = {"OUTPUT", "SIGNAL", {{"M1", {.065, .005, .085, .045}}}};
    if (mode == 4) {
      // The added lower rail makes the base fixture's moved Y/A pins only
      // 5/15 DBU away, below the unchanged native 20-DBU spacing. Use native
      // interior accesses in this PG-only variant; both cells still have to
      // move to row1 to reach its two admitted horizontal tracks.
      Ann_place             moved(200, 250, 200, 250);
      const physical::Shape lower_rail{
          "M1",
          {0, 240, 1600, 250}
      };
      const physical::Signal_metal_rules rules(f.tech, f.fp);
      for (const auto* name : {"A", "Y"}) {
        const auto& pin = master.pins.at(name).shapes[0];
        EXPECT_FALSE(rules.spacing({pin.layer, physical::placed_rect(master, moved, f.fp.dbu, pin.rect)}, lower_rail).legal);
      }
      master.pins["A"].shapes[0].rect.yl = .025;
      master.pins["A"].shapes[0].rect.yh = .215;
      master.pins["Y"].shapes[0].rect.yl = .055;
      master.pins["Y"].shapes[0].rect.yh = .095;
      for (const auto* name : {"A", "Y"}) {
        const auto& pin = master.pins.at(name).shapes[0];
        EXPECT_TRUE(rules.spacing({pin.layer, physical::placed_rect(master, moved, f.fp.dbu, pin.rect)}, lower_rail).legal);
      }
      const auto y_pin = physical::placed_rect(master, moved, f.fp.dbu, master.pins.at("Y").shapes[0].rect);
      for (auto plate : physical::via_geometry(f.tech, f.fp, {"V1_DEF"})) {
        if (plate.layer == "M1") {
          plate.rect = plate.rect.translated(275, 325);
          EXPECT_TRUE(y_pin.contains(plate.rect));
          EXPECT_TRUE(rules.spacing(plate, lower_rail).legal);
        }
      }
      master.pins["VDD"] = {"INOUT", "POWER", {{"M1", {0, .24, .2, .25}}}};
      physical::Net supply;
      supply.name        = "VDD";
      supply.use         = "POWER";
      supply.connections = {
          {"*", "VDD"}
      };
      supply.wires.push_back({"ROUTED", {}});
      auto& rail = supply.wires.back();
      for (int row = 0; row < 2; ++row) {
        rail.paths.push_back(physical::flush_wire_path(
            {
                "M1",
                {0, double(row * 250 + 240), 1600, double(row * 250 + 250)}
        },
            true));
        rail.paths.back().width = 10;  // SPECIALNETS requires an explicit physical width.
      }
      f.fp.special_nets.push_back(std::move(supply));
    }
    if (mode == 5) {
      f.tech.layers["M1"].rules.area     = .0009;
      master.pins["Y"].shapes[0].rect.yh = .055;
    }
    if (mode == 7) {
      // The old row0 Y pin overlaps a real native PG rectangle. No Local
      // carrier uses M1, and SAT must be allowed to choose the required
      // row1 pose before that movable pin is checked against the supply.
      const physical::Shape rail{
          "M1",
          {265, 15, 285, 215}
      };
      const auto&                        pin = master.pins.at("Y").shapes[0];
      const physical::Signal_metal_rules rules(f.tech, f.fp);
      EXPECT_FALSE(
          rules.spacing({pin.layer, physical::placed_rect(master, Ann_place(200, 0, 200, 250), f.fp.dbu, pin.rect)}, rail).legal);
      EXPECT_TRUE(
          rules.spacing({pin.layer, physical::placed_rect(master, Ann_place(200, 250, 200, 250), f.fp.dbu, pin.rect)}, rail).legal);
      physical::Net supply;
      supply.name  = "VDD";
      supply.use   = "POWER";
      supply.wires = {
          {"ROUTED", {physical::flush_wire_path(rail, false)}}
      };
      supply.wires[0].paths[0].width = 20;
      f.fp.special_nets.push_back(std::move(supply));
    }
    for (int k = 0; k < 2; ++k) {
      Ann_place pose(k ? 500 : 200, 0, 200, 250);
      pose.status = physical::Status::PLACED;
      f.fp.components.push_back({"c" + std::to_string(k), "dual", pose});
    }
    f.fp.terminals = {
        {"incoming",
         "external", "INPUT",
         1200, 125,
         physical::Orientation::N,
         physical::Status::FIXED,
         {{"M4", {-10, -10, 10, 10}}}}
    };
    f.fp.nets = {
        {"internal",         {{"c0", "A"}, {"c1", "A"}}, {}, "SIGNAL"},
        {"external", {{"c0", "Y"}, {"PIN", "incoming"}}, {}, "SIGNAL"}
    };
    f.design.cell    = {0, 1};
    f.design.movable = {0, 1};
    f.design.width   = {200, 200};
    f.design.tall    = {false, false};
    f.design.nets_of = {
        {0, 1},
        {0}
    };
    f.design.nets.assign(2, {});
    f.design.nets[0].frame = 0;
    f.design.nets[0].pins  = {
        {.comp = 0, .lef = &master.pins.at("A"), .name = "A"},
        {.comp = 1, .lef = &master.pins.at("A"), .name = "A"}
    };
    f.design.nets[1].frame  = 1;
    f.design.nets[1].routed = true;
    f.design.nets[1].pins   = {
        {.comp = 0, .lef = &master.pins.at("Y"), .name = "Y"},
        {.term = 0, .name = "incoming"}
    };
    f.routes = {};
    f.routes.trees.resize(2);
    f.routes.local = {1, 0};
    f.routes.trees[1].nodes.resize(2);
    auto& left               = f.routes.trees[1].nodes[0];
    left.tile                = 0;
    left.box                 = 0;
    left.pins                = {0};
    left.direct_carrier_tap  = true;
    auto& right              = f.routes.trees[1].nodes[1];
    right.tile               = 1;
    right.pins               = {1};
    right.direct_carrier_tap = true;
    Segment segment;
    segment.net         = 1;
    segment.layer       = 1;
    segment.band        = 0;
    segment.lo          = 0;
    segment.hi          = 1;
    segment.node_lo     = 0;
    segment.node_hi     = 1;
    segment.track       = 2;
    segment.track_state = uint8_t(Track_state::checked);
    f.routes.segments.push_back(segment);
    Boxes boxes;
    boxes.box_of = {0, 0};
    Box box;
    box.cells          = {0, 1};
    box.tile           = 0;
    box.sites          = 8;
    box.critical_fixed = box.fixed = box.placed = true;
    box.reserved_h                              = {275, 375};
    if (mode == 4) {
      box.reserved_h = {325, 425};  // Same two finite native tracks, with real PG landing clearance.
    }
    box.reserved_v = {225};
    boxes.boxes.push_back(box);
    if (mode == 2 || mode == 6) {
      // The first box's valid transaction must remain staged if the later
      // box cannot route either of its only admitted horizontal tracks.
      for (int k = 2; k < 4; ++k) {
        Ann_place pose(k == 2 ? 1000 : 1300, 0, 200, 250);
        pose.status = physical::Status::PLACED;
        f.fp.components.push_back({"c" + std::to_string(k), "dual", pose});
        f.design.cell.push_back(k);
        f.design.movable.push_back(k);
        f.design.width.push_back(200);
        f.design.tall.push_back(false);
        f.design.nets_of.push_back({2});
      }
      f.fp.nets.push_back({
          "later",
          {{"c2", "A"}, {"c3", "A"}},
          {},
          "SIGNAL"
      });
      Net later;
      later.frame = 2;
      later.pins  = {
          {.comp = 2, .lef = &master.pins.at("A"), .name = "A"},
          {.comp = 3, .lef = &master.pins.at("A"), .name = "A"}
      };
      f.design.nets.push_back(std::move(later));
      f.routes.trees.resize(3);
      f.routes.local.push_back(1);
      auto later_box       = box;
      later_box.cells      = {2, 3};
      later_box.tile       = 1;
      later_box.reserved_v = {1025};
      boxes.boxes.push_back(std::move(later_box));
      boxes.box_of.insert(boxes.box_of.end(), {1, 1});
      if (mode == 6) {
        // Both boxes tap the same admitted external backbone. A bounded
        // failure in the first box must not become an upstream rejection
        // which prevents the second box's independent SAT attempt.
        f.fp.nets[1].connections.push_back({"c2", "Y"});
        f.design.nets[1].pins.push_back({.comp = 2, .lef = &master.pins.at("Y"), .name = "Y"});
        f.design.nets_of[2].push_back(1);
        f.routes.trees[1].nodes[1].pins.push_back(2);
        f.routes.trees[1].nodes[1].box = 1;
      }
    }
    const auto    before   = f.fp;
    const auto    incoming = final_planned_geometry(f.design, f.grid, boxes, f.routes);
    physical::Net carrier;
    carrier.name  = "external";
    carrier.wires = {
        {"ROUTED", incoming.paths[1]}
    };
    double old_left = 1e9;
    for (const auto& shape : physical::route_geometry(f.tech, f.fp, carrier)) {
      if (shape.layer == "M4") {
        old_left = std::min(old_left, shape.rect.xl);
      }
    }
    ASSERT_GT(old_left, 225);
    Legroute_regions regions;
    if (mode == 1 || mode == 6) {
      regions.obstacles.push_back({
          "M3",
          {215, 100, 235, 400}
      });
    }
    if (mode == 2) {
      regions.obstacles.push_back({
          "M2",
          {800, 265, 1600, 285}
      });
      regions.obstacles.push_back({
          "M2",
          {800, 365, 1600, 385}
      });
    }
    std::filesystem::path report_dir;
    if (mode == 3 || mode == 6) {
      report_dir = std::filesystem::temp_directory_path()
                   / ("legroute_sat_report_failure_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      std::filesystem::create_directories(mode == 3 ? report_dir / "final_runtime.json" : report_dir);
      f.options.output_dir = report_dir.string();
    }
    Legroute_report report;
    if (mode == 3) {
      EXPECT_THROW(final_wires(f.design, f.grid, boxes, f.routes, regions, report), std::runtime_error);
      EXPECT_EQ(report.connected_nets, 2u);
      std::filesystem::remove_all(report_dir);
    } else {
      final_wires(f.design, f.grid, boxes, f.routes, regions, report);
    }
    if (mode == 6) {
      std::ifstream timing(report_dir / "final_box_times.tsv");
      std::string   line;
      ASSERT_TRUE(std::getline(timing, line));
      for (int box_index = 0; box_index < 2; ++box_index) {
        ASSERT_TRUE(std::getline(timing, line));
        std::istringstream       row(line);
        std::vector<std::string> fields;
        std::string              field;
        while (std::getline(row, field, '\t')) {
          fields.push_back(field);
        }
        ASSERT_GE(fields.size(), 5u);
        EXPECT_EQ(fields[0], std::to_string(box_index));
        EXPECT_EQ(fields[4], "1") << line;  // Both boxes actually entered SAT.
        if (box_index == 0) {
          EXPECT_NE(fields[1], "0") << line;
        } else {
          EXPECT_EQ(fields[1], "0") << line;  // The unaffected box still closes.
          EXPECT_EQ(fields[3], "0") << line;  // Its shared net is globally incomplete.
        }
      }
      std::filesystem::remove_all(report_dir);
    }
    std::string reason;
    for (const auto& failure : report.failure_sample) {
      reason += failure + " | ";
    }
    if (mode == 1 || mode == 2 || mode == 3 || mode == 6) {
      EXPECT_FALSE(report.wires_emitted);
      EXPECT_EQ(f.fp.components, before.components);
      EXPECT_EQ(f.fp.nets, before.nets);
      EXPECT_EQ(f.fp.nondefault_rules, before.nondefault_rules);
      if (mode == 2) {
        EXPECT_EQ(report.connected_nets, 2u) << reason;
        EXPECT_EQ(report.failed_nets, 1u) << reason;
      }
    } else {
      ASSERT_TRUE(report.wires_emitted) << reason;
      EXPECT_EQ(f.fp.components[0].place.y, 250);
      EXPECT_EQ(f.fp.components[1].place.y, 250);
      EXPECT_EQ(report.connected_nets, 2u);
      for (const auto& net : f.fp.nets) {
        EXPECT_TRUE(physical::check_route_connectivity(f.tech, f.fp, net).connected);
      }
      bool extended = false;
      for (const auto& shape : physical::route_geometry(f.tech, f.fp, f.fp.nets[1])) {
        extended |= shape.layer == "M4" && shape.rect.xl < old_left;
      }
      EXPECT_TRUE(extended);
    }
    EXPECT_EQ(f.fp.terminals, before.terminals);
    EXPECT_EQ(f.fp.special_nets, before.special_nets);
    EXPECT_EQ(final_planned_geometry(f.design, f.grid, boxes, f.routes).paths, incoming.paths);
  }
}

TEST(LegrouteFinal, TouchingActualPinsNeedNoArtificialWireRecord) {
  Final_io f(0, 0);
  f.options.cellbox_full_flow = true;
  f.options.route_ms          = 1000;
  f.grid.site                 = 50;
  f.grid.row_h                = 250;
  f.grid.sites                = 16;
  f.grid.key.assign(f.grid.size(), 0);
  f.grid.keys = {
      {{0, 0, 16}, {1, 0, 16}}
  };
  f.grid.row_present.assign(6, 1);
  f.grid.row_orient.assign(6, physical::Orientation::N);
  f.tech.sites["core"].width  = .05;
  f.tech.sites["core"].height = .25;
  for (int row = 0; row < 2; ++row) {
    f.fp.rows.push_back({"row" + std::to_string(row), "core", 0, row * 250, 32, 50, physical::Orientation::N});
  }
  auto& master     = f.tech.macros["touching"];
  master.klass     = "CORE";
  master.site      = "core";
  master.width     = .1;
  master.height    = .25;
  master.pins["A"] = {"INPUT", "SIGNAL", {{"M1", {.015, .015, .035, .215}}}};
  master.pins["B"] = {"INPUT", "SIGNAL", {{"M1", {.015, .015, .035, .215}}}};
  Ann_place pose(0, 0, 100, 250);
  pose.status     = physical::Status::PLACED;
  f.fp.components = {
      {"c", "touching", pose}
  };
  f.fp.terminals.clear();
  f.fp.nets = {
      {"intrinsic", {{"c", "A"}, {"c", "B"}}, {}, "SIGNAL"}
  };
  f.design.cell    = {0};
  f.design.movable = {0};
  f.design.width   = {100};
  f.design.tall    = {false};
  f.design.nets_of = {{0}};
  f.design.nets.assign(1, {});
  f.design.nets[0].frame = 0;
  f.design.nets[0].pins  = {
      {.comp = 0, .lef = &master.pins.at("A"), .name = "A"},
      {.comp = 0, .lef = &master.pins.at("B"), .name = "B"}
  };
  f.routes = {};
  f.routes.trees.resize(1);
  f.routes.local = {1};
  Boxes boxes;
  boxes.box_of = {0};
  Box box;
  box.cells          = {0};
  box.tile           = 0;
  box.sites          = 2;
  box.critical_fixed = box.fixed = box.placed = true;
  box.reserved_h                              = {25, 325};
  box.reserved_v                              = {25};
  boxes.boxes.push_back(box);
  const auto      connections = f.fp.nets[0].connections;
  Legroute_report report;
  final_wires(f.design, f.grid, boxes, f.routes, {}, report);
  EXPECT_TRUE(report.wires_emitted);
  EXPECT_EQ(report.connected_nets, 1u);
  EXPECT_EQ(report.final_connected_pins, 2u);
  EXPECT_EQ(f.fp.nets[0].connections, connections);
  EXPECT_TRUE(f.fp.nets[0].wires.empty());
}

TEST(LegrouteFinal, CriticalInternalNetRoutesOnlyOnItsReservedTracks) {
  // Diagnostic runs must retain rejected Local owners in the denominator,
  // route eligible internal nets, and never publish even an all-eligible scene.
  for (const int mode : {0, 1, 2}) {
    Final_io f(0, 0);
    f.options.cellbox_full_flow        = true;
    f.options.cellbox_diagnostic_final = mode != 0;
    f.grid.sites                       = 16;
    f.grid.site                        = 50;
    f.grid.row_h                       = 250;
    const auto terminals               = f.fp.terminals;
    const auto global_source           = f.fp.nets.front();
    const auto global_net              = f.design.nets.front();
    const auto global_tree             = f.routes.trees.front();
    const auto global_segment          = f.routes.segments.front();
    f.fp.terminals.clear();
    f.fp.nets.clear();
    auto& master                = f.tech.macros["gate"];
    master.klass                = "CORE";
    master.width                = .2;
    master.height               = .25;
    master.site                 = "core";
    f.tech.sites["core"].width  = .05;
    f.tech.sites["core"].height = .25;
    f.grid.row_present.assign(6, 1);
    f.grid.row_orient.assign(6, physical::Orientation::N);
    f.grid.key.assign(f.grid.size(), 0);
    f.grid.keys = {
        {{0, 0, 16}, {1, 0, 16}}
    };
    for (int row = 0; row < 6; ++row) {
      f.fp.rows.push_back({"row" + std::to_string(row), "core", 0, row * 250, 32, 50, physical::Orientation::N});
    }
    f.options.route_ms = 2000;
    master.pins["A"]   = {"INPUT", "SIGNAL", {{"M1", {.015, .015, .035, .235}}}};
    for (int i = 0; i < 2; ++i) {
      Ann_place pose(i ? 500 : 200, 0, 200, 250);
      pose.status = physical::Status::PLACED;
      f.fp.components.push_back({"c" + std::to_string(i), "gate", pose});
    }
    f.fp.nets.push_back({
        "internal",
        {{"c0", "A"}, {"c1", "A"}},
        {},
        "SIGNAL"
    });
    f.design.cell    = {0, 1};
    f.design.movable = {0, 1};
    f.design.width   = {200, 200};
    f.design.tall    = {false, false};
    auto& net        = f.design.nets[0];
    net              = {};
    net.frame        = 0;
    net.pins         = {
        {.comp = 0, .lef = &master.pins.at("A"), .name = "A"},
        {.comp = 1, .lef = &master.pins.at("A"), .name = "A"}
    };
    f.routes = {};
    f.routes.trees.resize(1);
    f.routes.local = {1};
    if (mode != 0) {
      f.routes.final_local_rejected = {""};
    }
    if (mode == 2) {
      f.fp.terminals = terminals;
      f.fp.nets.push_back(global_source);
      f.fp.nets.back().use = "SIGNAL";
      f.design.nets.push_back(global_net);
      auto& rejected = f.design.nets.back();
      rejected.frame = 1;
      rejected.clock = false;
      for (auto& pin : rejected.pins) {
        pin.name = f.fp.terminals[pin.term].name;
      }
      f.routes.trees.push_back(global_tree);
      f.routes.segments.push_back(global_segment);
      f.routes.segments.back().net   = 1;
      f.routes.segments.back().track = -1;
      f.routes.local.push_back(0);
      f.routes.final_local_rejected.push_back("unassigned Local segment");
    }
    Boxes boxes;
    boxes.box_of = {0, 0};
    Box box;
    box.cells  = {0, 1};
    box.sites  = 8;
    box.tile   = 0;
    box.placed = box.fixed = box.critical_fixed = true;
    box.reserved_h                              = {125, 225};
    box.reserved_v                              = {225, 525};
    boxes.boxes.push_back(box);
    Legroute_report report;
    final_wires(f.design, f.grid, boxes, f.routes, {}, report);
    std::string reasons;
    for (const auto& why : report.failure_sample) {
      reasons += why + " | ";
    }
    EXPECT_EQ(report.wires_emitted, mode == 0) << reasons;
    EXPECT_EQ(report.final_diagnostic_only, mode != 0);
    EXPECT_EQ(report.routed_nets, mode == 2 ? 2u : 1u);
    EXPECT_EQ(report.connected_nets, 1u);
    EXPECT_EQ(report.final_required_pins, mode == 2 ? 4u : 2u);
    EXPECT_EQ(report.final_connected_pins, 2u);
    EXPECT_EQ(report.final_local_rejected_nets, mode == 2 ? 1u : 0u);
    EXPECT_EQ(report.final_local_rejected_pins, mode == 2 ? 2u : 0u);
    EXPECT_EQ(report.failed_nets, mode == 2 ? 1u : 0u);
    EXPECT_EQ(report.final_detail_failed_nets, 0u) << reasons;
    EXPECT_EQ(report.final_detail_failed_pins, 0u);
    EXPECT_EQ(report.unchecked_nets, 0u);
    EXPECT_EQ(report.final_detail_unattempted_pins, 0u);
    EXPECT_EQ(report.final_domain_rejections, 0u);
    if (mode == 2) {
      EXPECT_EQ(report.failure_reasons.at("Local rejected before Final"), 1u);
      EXPECT_FALSE(report.failure_reasons.contains("Local rejected before Final: unassigned Local segment"));
    }
    if (mode != 0) {
      for (const auto& source : f.fp.nets) {
        EXPECT_TRUE(source.wires.empty());
      }
    }
    for (const auto& shape : physical::route_geometry(f.tech, f.fp, f.fp.nets[0])) {
      if (shape.layer == "M2" && shape.rect.xh - shape.rect.xl > shape.rect.yh - shape.rect.yl) {
        const auto y = (shape.rect.yl + shape.rect.yh) / 2;
        EXPECT_TRUE(y == 125 || y == 225) << y;
      }
    }
  }
}

TEST(LegrouteFinal, DiagnosticFinalRequiresCompleteLocalAdmissionRecords) {
  Final_io f(0, 0);
  f.options.cellbox_full_flow        = true;
  f.options.cellbox_diagnostic_final = true;
  Legroute_report report;
  EXPECT_THROW(final_wires(f.design, f.grid, {}, f.routes, {}, report), std::invalid_argument);
  EXPECT_TRUE(f.fp.nets[0].wires.empty());
}

TEST(LegrouteFinal, CriticalLeafCarriersEndAtBoxEdgesWithoutAuxiliaryRisers) {
  for (const bool horizontal : {false, true}) {
    Final_io f(0, 0);
    f.options.cellbox_full_flow = true;
    f.fp.terminals.clear();
    f.design.nets[0].pins.clear();
    f.design.nets[0].clock = false;
    f.routes.trees[0].nodes.clear();
    for (int i = 0; i < 2; ++i) {
      Node node;
      node.tile               = i ? (horizontal ? 1 : 2) : 0;
      node.box                = i;
      node.riser_layer        = 0;
      node.riser_track        = 4;
      node.direct_carrier_tap = true;
      f.routes.trees[0].nodes.push_back(node);
    }
    f.routes.segments = {
        {.net = 0, .layer = horizontal ? 1 : 0, .band = 0, .lo = 0, .hi = 1, .node_lo = 0, .node_hi = 1, .track = 4}
    };
    const auto geometry = final_planned_geometry(f.design, f.grid, {}, f.routes);
    ASSERT_EQ(geometry.paths.size(), 1u);
    ASSERT_EQ(geometry.paths[0].size(), 1u);
    EXPECT_TRUE(geometry.joins[0].empty());
    physical::Net emitted;
    emitted.name = "leaf_carrier";
    emitted.wires.push_back({"ROUTED", geometry.paths[0]});
    const auto metal = physical::route_geometry(f.tech, f.fp, emitted);
    ASSERT_EQ(metal.size(), 1u);
    EXPECT_EQ(metal[0].layer, horizontal ? "M4" : "M3");
    // Box taps stop at the facing box edges, one site (grid.site) inside each box; Final extends the carrier into
    // each box. Adjacent boxes share that edge, so the carrier straddles it: centerline +-site, 10 DBU caps.
    const double site = std::max<int64_t>(1, f.grid.site);
    EXPECT_DOUBLE_EQ(horizontal ? metal[0].rect.xl : metal[0].rect.yl, (horizontal ? 800 : 500) - site - 10);
    EXPECT_DOUBLE_EQ(horizontal ? metal[0].rect.xh : metal[0].rect.yh, (horizontal ? 800 : 500) + site + 10);
  }
}

TEST(LegrouteFinal, CachedEmitterUsesCurrentTracksAndRejectsTopologyChanges) {
  Final_io               f(0, 0);
  const Boxes            boxes;
  Final_geometry_emitter emitter(f.design, f.grid, boxes, f.routes);
  const auto             before = emitter.emit(0);
  ASSERT_FALSE(before.paths.empty());
  ASSERT_EQ(before.paths[0].layer, "M4");
  ASSERT_TRUE(std::holds_alternative<physical::Route_point>(before.paths[0].steps[0]));
  EXPECT_EQ(std::get<physical::Route_point>(before.paths[0].steps[0]).y, 775);
  f.routes.segments[0].track = 16;
  const auto after           = emitter.emit(0);
  ASSERT_FALSE(after.paths.empty());
  EXPECT_EQ(std::get<physical::Route_point>(after.paths[0].steps[0]).y, 825);
  EXPECT_EQ(std::get<physical::Route_point>(before.paths[0].steps[0]).y, 775);
  const auto fresh = final_planned_geometry(f.design, f.grid, boxes, f.routes);
  EXPECT_EQ(after.joins, fresh.joins[0]);
  EXPECT_EQ(after.path_state, fresh.path_state[0]);
  EXPECT_EQ(after.io_stub_failure, fresh.io_stub_failure[0]);
  f.routes.trees[0].nodes[0].riser_track = 5;
  const auto moved_riser                 = emitter.emit(0);
  const auto rebuilt                     = final_planned_geometry(f.design, f.grid, boxes, f.routes);
  const auto rectangles                  = [&](const std::vector<physical::Route_path>& paths) {
    physical::Net net;
    net.name = "cached_emission";
    net.wires.push_back({"ROUTED", paths});
    std::vector<std::tuple<std::string, double, double, double, double>> result;
    for (const auto& shape : physical::route_geometry(f.tech, f.fp, net)) {
      result.emplace_back(shape.layer, shape.rect.xl, shape.rect.yl, shape.rect.xh, shape.rect.yh);
    }
    return result;
  };
  EXPECT_EQ(rectangles(moved_riser.paths), rectangles(rebuilt.paths[0]));
  EXPECT_NE(rectangles(moved_riser.paths), rectangles(after.paths));
  EXPECT_EQ(moved_riser.joins, rebuilt.joins[0]);
  EXPECT_THROW(emitter.emit(1), std::out_of_range);
  f.routes.segments.push_back(f.routes.segments.front());
  EXPECT_THROW(emitter.emit(0), std::logic_error);
}

TEST(LegrouteFinal, CachedEmitterClearsPartialMetalOnInvalidIoAccess) {
  Final_io f(0, 0);
  std::erase_if(f.fp.tracks, [](const auto& track) { return track.layer == "M4"; });
  const Boxes            boxes;
  Final_geometry_emitter emitter(f.design, f.grid, boxes, f.routes);
  for (int attempt = 0; attempt < 2; ++attempt) {
    const auto geometry = emitter.emit(0);
    EXPECT_FALSE(geometry.io_stub_failure.empty());
    EXPECT_TRUE(geometry.paths.empty());
    EXPECT_TRUE(geometry.joins.empty());
    EXPECT_TRUE(geometry.path_state.empty());
  }
}

TEST(LegrouteFinal, CriticalAdjacentJoinLandingsAreExplicitProducerMetal) {
  Final_io   f(0, 0);
  const auto original_terminals    = f.fp.terminals;
  f.tech.vias["V3_DEF"].is_default = false;
  const auto old                   = final_planned_geometry(f.design, f.grid, {}, f.routes);
  EXPECT_TRUE(old.adjacent_join_vias[0].empty());
  f.tech.vias["V3_DEF"].is_default = true;
  f.options.cellbox_full_flow      = true;
  const auto grown                 = final_planned_geometry(f.design, f.grid, {}, f.routes);
  ASSERT_EQ(grown.paths[0].size(), old.paths[0].size());
  EXPECT_EQ(grown.path_state, old.path_state);
  EXPECT_EQ(grown.joins, old.joins);
  ASSERT_FALSE(grown.adjacent_join_vias[0].empty());
  const auto decode = [&](const auto& paths) {
    physical::Net net;
    net.name = "clk";
    net.wires.push_back({"ROUTED", paths});
    return physical::route_geometry(f.tech, f.fp, net);
  };
  const auto before = decode(old.paths[0]), after = decode(grown.paths[0]);
  ASSERT_EQ(before.size(), after.size());
  size_t extensions = 0;
  for (size_t k = 0; k < after.size(); ++k) {
    EXPECT_EQ(before[k].layer, after[k].layer);
    EXPECT_TRUE(after[k].rect.contains(before[k].rect));
    const bool h = f.tech.layers.at(after[k].layer).direction == "HORIZONTAL";
    EXPECT_DOUBLE_EQ(h ? before[k].rect.yl : before[k].rect.xl, h ? after[k].rect.yl : after[k].rect.xl);
    EXPECT_DOUBLE_EQ(h ? before[k].rect.yh : before[k].rect.xh, h ? after[k].rect.yh : after[k].rect.xh);
    extensions += before[k].rect != after[k].rect;
  }
  EXPECT_GT(extensions, 0u);
  auto frozen_coverage = after;
  for (const auto& terminal : f.fp.terminals) {
    for (const auto& shape : terminal.shapes) {
      frozen_coverage.push_back(
          {shape.layer, physical::orient_rect(shape.rect, 0, 0, terminal.orientation).translated(terminal.x, terminal.y)});
    }
  }
  for (const auto& path : grown.adjacent_join_vias[0]) {
    ASSERT_EQ(path.steps.size(), 2u);
    const auto& at  = std::get<physical::Route_point>(path.steps[0]);
    const auto& via = std::get<physical::Route_via>(path.steps[1]);
    EXPECT_TRUE(f.tech.vias.at(via.name).is_default);
    for (auto plate : physical::via_geometry(f.tech, f.fp, via)) {
      if (!f.tech.layers.contains(plate.layer)) {
        continue;
      }
      plate.rect = plate.rect.translated(at.x, at.y);
      EXPECT_TRUE(std::ranges::any_of(frozen_coverage, [&](const auto& shape) {
        return shape.layer == plate.layer && shape.rect.contains(plate.rect);
      }));
    }
  }
  // No suitable DEFAULT definition: do not silently authorize alternative
  // wider landings or extra upper-layer geometry.
  f.tech.vias["V3_DEF"].is_default = false;
  const auto unsupported           = final_planned_geometry(f.design, f.grid, {}, f.routes);
  EXPECT_TRUE(unsupported.adjacent_join_vias[0].empty());
  // The full flow itself extends risers over the whole node tile, so the baseline is the full-flow geometry with
  // landings: dropping the DEFAULT via must remove only the landing growth (same shapes, each contained in the grown one,
  // at least one strictly smaller) and add nothing.
  const auto without = decode(unsupported.paths[0]);
  ASSERT_EQ(without.size(), after.size());
  size_t shrunk = 0;
  for (size_t k = 0; k < after.size(); ++k) {
    EXPECT_EQ(without[k].layer, after[k].layer);
    EXPECT_TRUE(after[k].rect.contains(without[k].rect));
    EXPECT_TRUE(without[k].rect.contains(before[k].rect) || before[k].layer != without[k].layer);
    shrunk += without[k].rect != after[k].rect;
  }
  EXPECT_GT(shrunk, 0u);
  EXPECT_EQ(f.fp.terminals, original_terminals);
}

TEST(LegrouteFinal, NonadjacentJoinsConsumeTheSelectedTouchingNativeChain) {
  for (bool missing_chain : {false, true}) {
    SCOPED_TRACE(missing_chain);
    Final_io f(0, 0);
    f.options.cellbox_full_flow = true;
    for (int k = 5; k <= 6; ++k) {
      const auto name       = "M" + std::to_string(k);
      const bool horizontal = k % 2 == 0;
      f.tech.layers[name]   = f.tech.layers.at(horizontal ? "M4" : "M3");
      f.tech.routing_layers.push_back(name);
      f.fp.tracks.push_back({horizontal ? "Y" : "X", name, 25, horizontal ? 30 : 32, 50});
      f.grid.layers.push_back({name, "", horizontal, 50, 25, 20});
      f.profile.route.signal_layers.push_back(name);
      const auto lower = "M" + std::to_string(k - 1), cut = "V" + std::to_string(k - 1);
      f.tech.cut_layers[cut].width   = .02;
      f.tech.cut_layers[cut].spacing = {.02};
      auto& via                      = f.tech.vias[cut + "_DEF"];
      via.is_default                 = true;
      via.shapes                     = {
          {lower, horizontal ? physical::Rect{-.01, -.02, .01, .02} : physical::Rect{-.02, -.01, .02, .01}},
          {  cut,                                                                   {-.01, -.01, .01, .01}},
          { name, horizontal ? physical::Rect{-.02, -.01, .02, .01} : physical::Rect{-.01, -.02, .01, .02}}
      };
    }
    // This valid DEFAULT sorts first, but its displaced M5 plate cannot
    // connect to V5_DEF at the same join. The producer must choose V4_DEF;
    // Final must not independently replace that selected native witness.
    auto& shifted      = f.tech.vias["A_shifted_V4"];
    shifted.is_default = true;
    shifted.shapes     = {
        {"M4", {-.02, -.01, .07, .01}},
        {"V4",  {.04, -.01, .06, .01}},
        {"M5",  {.04, -.02, .06, .02}}
    };
    const std::vector shifted_only{physical::via_path("M4", 225, 775, "A_shifted_V4")};
    ASSERT_TRUE(physical::check_conductor_connectivity(f.tech, f.fp, shifted_only, {}, {}).connected);
    if (missing_chain) {
      f.tech.vias["V4_DEF"].is_default = false;
    }
    f.routes.segments[0].layer = 3;  // M6 main carrier; native M3 endpoint risers.
    for (auto& pin : f.design.nets[0].pins) {
      pin.name = f.fp.terminals[pin.term].name;
    }
    const auto before   = f.fp;
    const auto selected = final_planned_geometry(f.design, f.grid, {}, f.routes);
    size_t     middle   = 0;
    for (const auto& path : selected.adjacent_join_vias.at(0)) {
      const auto& via = std::get<physical::Route_via>(path.steps.at(1));
      EXPECT_NE(via.name, "A_shifted_V4");
      middle += via.name == "V4_DEF";
    }
    EXPECT_EQ(middle, missing_chain ? 0u : 2u);
    Legroute_report report;
    final_wires(f.design, f.grid, {}, f.routes, {}, report);
    EXPECT_EQ(report.wires_emitted, !missing_chain) << ::testing::PrintToString(report.failure_sample);
    if (missing_chain) {
      EXPECT_EQ(f.fp.nets, before.nets);
      EXPECT_EQ(f.fp.components, before.components);
      EXPECT_EQ(f.fp.nondefault_rules, before.nondefault_rules);
      EXPECT_EQ(report.failed_nets, 1u);
    } else {
      EXPECT_TRUE(physical::check_route_connectivity(f.tech, f.fp, f.fp.nets[0]).connected);
      EXPECT_EQ(report.final_union_cut_rejections, 0u);
      for (const auto& wire : f.fp.nets[0].wires) {
        for (const auto& path : wire.paths) {
          for (const auto& step : path.steps) {
            if (const auto* via = std::get_if<physical::Route_via>(&step)) {
              EXPECT_NE(via->name, "A_shifted_V4");
            }
          }
        }
      }
    }
    EXPECT_EQ(f.fp.terminals, before.terminals);
    EXPECT_EQ(final_planned_geometry(f.design, f.grid, {}, f.routes).paths, selected.paths);
  }
}

TEST(LegrouteFinal, CriticalAdjacentViaUsesFrozenCoverageAndRejectsNativeCapConflict) {
  for (const bool obstruct : {false, true}) {
    Final_io f(0, 0);
    f.options.cellbox_full_flow = true;
    if (obstruct) {
      // The main M4 starts at x225: old width cap215, selected native
      // plate cap205. A foreign terminal ending190 is legal25DBU from the
      // old cap but too close15DBU to the new cap (native spacing20).
      const physical::Shape obstacle{
          "M4",
          {170, 765, 190, 785}
      };
      const physical::Signal_metal_rules rules(f.tech, f.fp);
      EXPECT_TRUE(rules
                      .spacing(
                          {
                              "M4",
                              {215, 765, 1235, 785}
      },
                          obstacle)
                      .legal);
      EXPECT_FALSE(rules
                       .spacing(
                           {
                               "M4",
                               {205, 765, 1245, 785}
      },
                           obstacle)
                       .legal);
      f.fp.terminals.push_back(
          {"blocker", "foreign", "INPUT", 0, 0, physical::Orientation::N, physical::Status::FIXED, {obstacle}});
    }
    for (auto& pin : f.design.nets[0].pins) {
      pin.name = f.fp.terminals[pin.term].name;
    }
    Legroute_report report;
    final_wires(f.design, f.grid, {}, f.routes, {}, report);
    std::string reasons;
    for (const auto& reason : report.failure_sample) {
      reasons += reason + " | ";
    }
    EXPECT_EQ(report.wires_emitted, !obstruct) << reasons;
    EXPECT_EQ(report.connected_nets, obstruct ? 0u : 1u) << reasons;
    EXPECT_EQ(report.global_conflict_nets, obstruct ? 1u : 0u) << reasons;
    if (obstruct) {
      EXPECT_TRUE(f.fp.nets[0].wires.empty());
    } else {
      EXPECT_TRUE(physical::check_route_connectivity(f.tech, f.fp, f.fp.nets[0]).connected);
      EXPECT_EQ(report.final_domain_rejections, 0u);
    }
  }
}

TEST(LegrouteFinal, IoLandingAddsOnlyAnOwnedAxialPadAndRejectsBlockedGrowth) {
  for (const bool obstruct : {false, true}) {
    SCOPED_TRACE(obstruct);
    Final_io f(0, 0);
    f.options.cellbox_full_flow = true;
    // The real M4 pin covers x115..135 at y375. VIA34 needs x105..145;
    // its native M3 access stem already exists, but no Local M4 wire does.
    f.fp.terminals[0].shapes    = {
        {"M4", {-6, -11, 14, 9}}
    };
    const physical::Shape original_pin{
        "M4",
        {115, 365, 135, 385}
    };
    const physical::Shape landing{
        "M4",
        {105, 365, 145, 385}
    };
    ASSERT_FALSE(original_pin.rect.contains(landing.rect));
    f.fp.nets[0].wires = {
        {"FIXED", {physical::flush_wire_path({"M4", {115, 365, 170, 385}}, true)}}
    };
    if (obstruct) {
      const physical::Shape obstacle{
          "M4",
          {70, 365, 90, 385}
      };
      const physical::Signal_metal_rules rules(f.tech, f.fp);
      ASSERT_TRUE(rules.spacing(original_pin, obstacle).legal);
      ASSERT_FALSE(rules.spacing(landing, obstacle).legal);
      f.fp.terminals.push_back(
          {"blocker", "foreign", "INPUT", 0, 0, physical::Orientation::N, physical::Status::FIXED, {obstacle}});
    }
    for (auto& pin : f.design.nets[0].pins) {
      pin.name = f.fp.terminals[pin.term].name;
    }
    const auto before   = f.fp;
    const auto selected = final_planned_geometry(f.design, f.grid, {}, f.routes);
    auto       proposed = f.fp.nets[0];
    proposed.wires      = {
        {"ROUTED", selected.paths.at(0)}
    };
    const auto metal = physical::route_geometry(f.tech, f.fp, proposed);
    ASSERT_TRUE(std::ranges::any_of(metal, [&](const auto& shape) { return shape == landing; }));
    Legroute_report report;
    final_wires(f.design, f.grid, {}, f.routes, {}, report);
    EXPECT_EQ(report.wires_emitted, !obstruct) << ::testing::PrintToString(report.failure_sample);
    EXPECT_EQ(f.fp.terminals, before.terminals);
    EXPECT_EQ(f.fp.components, before.components);
    if (obstruct) {
      EXPECT_EQ(f.fp.nets, before.nets);
      EXPECT_EQ(f.fp.nondefault_rules, before.nondefault_rules);
    } else {
      ASSERT_FALSE(f.fp.nets[0].wires.empty());
      EXPECT_EQ(f.fp.nets[0].wires.front(), before.nets[0].wires.front());
      EXPECT_TRUE(physical::check_route_connectivity(f.tech, f.fp, f.fp.nets[0]).connected);
      EXPECT_EQ(report.connected_nets, 1u);
    }
  }
}

TEST(LegrouteFinal, CriticalFixedJoinRejectsCutConflictsWithoutRoutingFallback) {
  Final_io f(0, 0);
  f.options.cellbox_full_flow     = true;
  f.tech.cut_layers["V3"].spacing = {.09};
  for (auto& pin : f.design.nets[0].pins) {
    pin.name = f.fp.terminals[pin.term].name;
  }
  Legroute_report report;
  final_wires(f.design, f.grid, {}, f.routes, {}, report);
  // Fixed Local joins cannot invoke another router to evade a native conflict.
  EXPECT_FALSE(report.wires_emitted);
  EXPECT_EQ(report.failed_nets, 1u);
  EXPECT_TRUE(f.fp.nets[0].wires.empty());
  EXPECT_FALSE(report.failure_sample.empty());
}
}  // namespace livehd::placement::lr

namespace livehd::placement::lr {
TEST(LegrouteFinal, AnIoStubOnThePinRowJoinsThePinDirectlyWithoutAPhantomLowerLayerStack) {
  // Advisor lfsr rst witness: the M4 IO pin's access row is the M4 stub track (ys == py), so no vertical M3 piece is
  // drawn. The planned joins must not record M4->M3 and M3->M4 at that point (there is no M3 metal to land on).
  Final_io f(0, 0);
  f.routes.trees[0].nodes[0].stub_track = 7;  // y 375: the clk_in pin row
  const auto geo                        = final_planned_geometry(f.design, f.grid, {}, f.routes);
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
    Final_io  f(0, 0);
    const int trunk = int(f.routes.segments.size());
    f.routes.segments.push_back({.net = 0, .layer = 0, .band = 0, .lo = 0, .hi = 2, .track = 5, .clock = true});
    Spine sp{.net = 0, .region = 0, .trunk = trunk, .node = -1};
    for (int r = 0; r < ribs; ++r) {
      sp.ribs.push_back(int(f.routes.segments.size()));
      f.routes.segments.push_back({.net = 0, .layer = 1, .band = r * 2, .lo = 0, .hi = 1, .track = 5 + 20 * r, .clock = true});
    }
    f.routes.spines.push_back(sp);
    const auto geo         = final_planned_geometry(f.design, f.grid, {}, f.routes);
    const auto tx          = f.grid.track_coord(0, 5);
    size_t     trunk_joins = 0;
    for (const auto& [a, b, x, y] : geo.joins[0]) {
      trunk_joins += a == "M4" && b == "M3" && x == tx;
    }
    EXPECT_EQ(trunk_joins, ribs == 1 ? 0u : 2u) << ribs << " rib(s)";
  }
}
}  // namespace livehd::placement::lr
