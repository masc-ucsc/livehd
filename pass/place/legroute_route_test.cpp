// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <set>

#include "gtest/gtest.h"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/routing_rules.hpp"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
struct Track_fixture {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  Design               design{tech, fp, options};
  Grid                 grid;
  Routing              routes;

  explicit Track_fixture(bool same_net) {
    options.reserved_tracks = 0;
    grid.x0 = grid.y0 = 100;
    grid.cols         = 1;
    grid.rows         = 3;
    grid.tile_w = grid.tile_h = 100;
    grid.die = fp.die = {0, 0, 1000, 1000};
    grid.layers       = {
        {"M3", "", false, 100, 50, 10},
        {"M4", "",  true, 100, 50, 10}
    };
    grid.obstacle_band.resize(2);
    grid.obstacle_band[0].resize(1);
    grid.obstacle_band[1].resize(3);
    grid.obstacles.resize(2);
    for (const auto& layer : grid.layers) {
      auto& l     = tech.layers[layer.name];
      l.direction = layer.horizontal ? "HORIZONTAL" : "VERTICAL";
      l.width     = .01;
      l.pitch_x = l.pitch_y = .1;
      l.rules.loaded        = true;
      l.rules.spacing       = {
          {.01, {}, {}}
      };
      tech.routing_layers.push_back(layer.name);
    }
    routes.trees.resize(same_net ? 1 : 2);
    design.nets.resize(routes.trees.size());
    for (auto& tree : routes.trees) {
      tree.nodes.resize(3);
      for (int i = 0; i < 3; ++i) {
        tree.nodes[i].tile = i;
      }
    }
    for (int i = 0; i < 2; ++i) {
      Segment s;
      s.net     = same_net ? 0 : i;
      s.layer   = 0;
      s.band    = 0;
      s.lo      = i;
      s.hi      = i + 1;
      s.node_lo = i;
      s.node_hi = i + 1;
      routes.segments.push_back(s);
    }
  }

  // Build the real Plan resource grid with the same two M3 tracks as the manual fixture.
  void build_reservation_grid(bool power_on_reserved_track = false) {
    tech.layers["M1"]   = tech.layers.at("M3");
    tech.layers["M2"]   = tech.layers.at("M4");
    tech.routing_layers = {"M1", "M2", "M3", "M4"};
    tech.sites["site"]  = {.05, .05, true};
    for (int row = 0; row < 6; ++row) {
      fp.rows.push_back({"r" + std::to_string(row), "site", 100, 100 + row * 50, 4, 50, physical::Orientation::N});
    }
    design.widest = 200;
    if (power_on_reserved_track) {
      physical::Net power;
      power.name = "VDD";
      power.use  = "POWER";
      power.wires.push_back(
          {"FIXED", {{"M3", 10, {}, {physical::Route_point{150, 90, {}}, physical::Route_point{150, 310, {}}}, {}}}});
      fp.special_nets.push_back(std::move(power));
    }
    grid = build_grid(design, {});
  }

  void add_upper_layers() {
    grid.layers.push_back({"M5", "", false, 80, 30, 10});
    grid.layers.push_back({"M6", "", true, 80, 30, 10});
    tech.layers["M5"] = tech.layers.at("M3");
    tech.layers["M6"] = tech.layers.at("M4");
    for (const auto* name : {"M5", "M6"}) {
      tech.layers[name].pitch_x = tech.layers[name].pitch_y = .08;
      tech.layers[name].has_offset                          = true;
      tech.layers[name].offset_x = tech.layers[name].offset_y = .03;
      tech.routing_layers.push_back(name);
    }
    grid.obstacles.resize(grid.layers.size());
    grid.obstacle_band.resize(grid.layers.size());
    for (size_t l = 0; l < grid.layers.size(); ++l) {
      const auto& layer = grid.layers[l];
      grid.obstacle_band[l].resize(layer.horizontal ? grid.rows : grid.cols);
      fp.tracks.push_back({layer.horizontal ? "Y" : "X", layer.name, layer.offset, 20, layer.pitch});
    }
  }

  void block_layer(int layer, physical::Rect obstacle) {
    const auto index = int(grid.obstacles[layer].size());
    grid.obstacles[layer].push_back(obstacle);
    for (auto& band : grid.obstacle_band[layer]) {
      band.push_back(index);
    }
  }

  // Seed the resolver's shared contract so these tests isolate Local admission from box construction.
  void paired_budget(int tile, unsigned tracks, std::vector<int> destination_nets = {}) {
    options.paired_pin_reserve = true;
    options.legal_boxes        = true;
    if (grid.paired_pin_reservation_of_tile.empty()) {
      grid.paired_pin_reservation_of_tile.assign(grid.size(), -1);
    }
    auto&                      catalog  = grid.resources.layers[0];
    auto&                      resource = catalog.tiles[size_t(tile)];
    Paired_pin_box_reservation reservation;
    reservation.box               = int(grid.paired_pin_reservations.size());
    reservation.tile              = tile;
    reservation.vertical_layer    = 0;
    reservation.vertical_tracks   = tracks;
    reservation.vertical_usable   = unsigned(catalog.represented_capacity(size_t(tile)));
    reservation.horizontal_usable = {1, 1};  // Resolver-certified row supply, independent of this V-track fixture.
    for (int net : destination_nets) {
      reservation.external_pins.push_back({net, 0, 0});
    }
    resource.cellbox_budget                           += tracks;
    resource.coarse_capacity                           = catalog.represented_capacity(size_t(tile));
    grid.cap[0][size_t(tile)]                          = int16_t(resource.coarse_capacity);
    grid.paired_pin_reservation_of_tile[size_t(tile)]  = int(grid.paired_pin_reservations.size());
    grid.paired_pin_reservations.push_back(std::move(reservation));
  }

  void build_repair_row(bool impossible = false) {
    grid.cols   = 8;
    grid.rows   = 1;
    grid.tile_h = 200;
    grid.die = fp.die = {0, 0, 4000, 1000};
    grid.obstacle_band[0].resize(8);
    grid.obstacle_band[1].resize(1);
    routes.trees.resize(2);
    design.nets.resize(2);
    for (auto& tree : routes.trees) {
      tree.nodes.clear();
    }
    routes.segments.clear();
    Segment a;
    a.net   = 0;
    a.layer = 1;
    a.lo    = 0;
    a.hi    = 4;  // [100,600], sorted first by length, initially on y=150.
    routes.segments.push_back(a);
    Segment b;
    b.net   = 1;
    b.layer = 1;
    b.lo    = 4;
    b.hi    = 7;  // [500,900]: only y=150 avoids the obstacle, so A must move to y=250.
    routes.segments.push_back(b);
    block_layer(1, {800, 245, 900, 255});
    if (impossible) {
      block_layer(1, {800, 145, 900, 155});
    }
  }

  void enable_critical_repair() {
    options.cellbox_full_flow  = true;
    options.l5_repair_ms       = 1000;
    options.l5_fallback_shadow = true;
    grid.critical_reserved_h.resize(grid.size());
    grid.critical_reserved_v.resize(grid.size());
    for (int layer = 0; layer < int(grid.layers.size()); ++layer) {
      const auto&          metal = grid.layers[layer];
      Track_resource_layer resource;
      resource.name       = metal.name;
      resource.horizontal = metal.horizontal;
      resource.grid_layer = layer;
      resource.bands.resize(metal.horizontal ? grid.rows : grid.cols);
      for (int band = 0; band < int(resource.bands.size()); ++band) {
        const auto [first, last] = grid.tracks(layer, band);
        for (auto track = first; track <= last; ++track) {
          resource.bands[band].push_back(grid.track_coord(layer, track));
        }
      }
      resource.tiles.resize(grid.size());
      for (size_t tile = 0; tile < grid.size(); ++tile) {
        resource.tiles[tile].band = metal.horizontal ? grid.row(int(tile)) : grid.col(int(tile));
      }
      grid.resources.layers.push_back(std::move(resource));
      fp.tracks.push_back({metal.horizontal ? "Y" : "X", metal.name, metal.offset, 50, metal.pitch});
    }
    for (size_t net = 0; net < design.nets.size(); ++net) {
      physical::Net source;
      source.name = "n" + std::to_string(net);
      fp.nets.push_back(source);
      design.nets[net].frame = int(net);
    }
  }

  void build_stable_horizontal_row(bool near_ends = false) {
    grid.cols   = 2;
    grid.rows   = 1;
    grid.tile_w = 500;
    grid.tile_h = 200;
    grid.die = fp.die     = {0, 0, 1200, 500};
    grid.layers[0].pitch  = 20;
    grid.layers[0].offset = 10;
    grid.obstacle_band[0].resize(2);
    grid.obstacle_band[1].resize(1);
    tech.layers["M3"].pitch_x = tech.layers["M3"].pitch_y  = .020;
    // LEF EXTENSION is backward, side, forward. Side200 reaches the adjacent track's 90-DBU edge gap.
    tech.layers["M4"].rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .020 EXTENSION .050 .200 .050 CORNERONLY ;"};
    routes.segments.clear();
    for (int net = 0; net < 2; ++net) {
      auto& tree = routes.trees[net];
      tree.nodes.resize(2);
      tree.nodes[0].tile = 0;
      tree.nodes[1].tile = 1;
      Segment h;
      h.net     = net;
      h.layer   = 1;
      h.lo      = 0;
      h.hi      = 1;
      h.node_lo = 0;
      h.node_hi = 1;
      routes.segments.push_back(h);
    }
    // Fixed V arrivals establish stable, electrically separate endpoints. The two H spans overlap but their
    // ends differ by 200 DBU (legal) or 20 DBU (inside the actual corner-only EOL keepout).
    for (int net = 0; net < 2; ++net) {
      for (int end = 0; end < 2; ++end) {
        Segment v;
        v.net     = net;
        v.layer   = 0;
        v.band    = end;
        v.node_lo = end;
        v.track   = (end ? 32 : 6) + (net ? (near_ends ? 1 : 10) : 0);
        routes.segments.push_back(v);
      }
    }
  }
};
}  // namespace

TEST(LegrouteRoute, CriticalGlobalEndpointTilesConsumeLayerCapacityOncePerNet) {
  for (bool full_flow : {false, true}) {
    Track_fixture f(false);
    f.options.cellbox_full_flow = full_flow;
    f.add_upper_layers();
    f.routes.segments.clear();
    f.grid.cap.assign(f.grid.layers.size(), std::vector<int16_t>(f.grid.size(), 1));
    f.routes.layer_dem.assign(f.grid.layers.size(), std::vector<int32_t>(f.grid.size(), 0));
    for (int net : {0, 1, 0}) {
      Conn c;
      c.net  = net;
      c.a    = 0;
      c.b    = 1;
      c.poly = {
          {0, 0},
          {0, 1}
      };
      f.routes.conns.push_back(c);
    }
    assign_layers(f.design, f.grid, f.routes);
    ASSERT_EQ(f.routes.segments.size(), 3u);
    EXPECT_EQ(f.routes.layer_overflow_segments, 0u);
    EXPECT_EQ(f.routes.segments[0].layer, 2);
    EXPECT_EQ(f.routes.segments[1].layer, full_flow ? 0 : 2);
    EXPECT_EQ(f.routes.segments[2].layer, 2);  // same net pays once, so it reuses its preferred layer
    for (int tile : {0, 1}) {
      EXPECT_EQ(f.routes.layer_dem[2][tile], full_flow ? 1 : 0);
      EXPECT_EQ(f.routes.layer_dem[0][tile], full_flow ? 1 : 0);
    }
  }
}

TEST(LegrouteRoute, CriticalTracksUseNativeEndRulesWithoutWideWireEnvelopeFalseRejection) {
  for (bool full_flow : {false, true}) {
    for (bool illegal_ends : {false, true}) {
      Track_fixture f(false);
      f.tech.layers["M3"].rules.parallel = {
          {{0, .1}, {0}, {{.01}, {.25}}}
      };
      f.tech.layers["M3"].rules.properties["LEF58_EOLKEEPOUT"]
          = {illegal_ends ? "EOLKEEPOUT .020 EXTENSION .020 .100 .020 CORNERONLY ;"
                          : "EOLKEEPOUT .020 EXTENSION .020 .020 .020 CORNERONLY ;"};
      f.build_reservation_grid();
      f.options.cellbox_full_flow = full_flow;
      f.grid.critical_reserved_h.resize(f.grid.size());
      f.grid.critical_reserved_v.resize(f.grid.size());
      f.routes.segments[1].lo = f.routes.segments[0].lo;
      f.routes.segments[1].hi = f.routes.segments[0].hi;
      const physical::Signal_metal_rules rules(f.tech, f.fp);
      ASSERT_EQ(rules.clearance("M3"), 250);
      EXPECT_EQ(rules
                    .spacing(
                        {
                            "M3",
                            {145, 90, 155, 310}
      },
                        {"M3", {245, 90, 255, 310}})
                    .legal,
                !illegal_ends);
      assign_tracks(f.design, f.grid, f.routes, 1);
      if (full_flow && !illegal_ends) {
        EXPECT_EQ(f.routes.unassigned, 0u);
        EXPECT_NE(f.routes.segments[0].track, f.routes.segments[1].track);
      } else {
        EXPECT_GT(f.routes.unassigned, 0u);
      }
    }
  }
}

TEST(LegrouteRoute, CriticalVerticalLeafSpansReuseTracksBeyondTheirActualEndpoints) {
  for (bool direct : {false, true}) {
    Track_fixture f(false);
    f.options.reserved_tracks = 1;  // one of the two native V tracks is unavailable
    f.tech.layers["M1"]       = f.tech.layers.at("M3");
    f.tech.layers["M2"]       = f.tech.layers.at("M4");
    f.tech.routing_layers     = {"M1", "M2", "M3", "M4"};
    f.tech.sites["site"]      = {.05, .1, true};
    for (int row = 0; row < 8; ++row) {
      f.fp.rows.push_back({"r" + std::to_string(row), "site", 100, 100 + row * 100, 4, 50, physical::Orientation::N});
    }
    f.fp.tracks = {
        {"X", "M3", 50, 10, 100},
        {"Y", "M4", 50, 10, 100}
    };
    f.design.widest = 200;
    f.grid          = build_grid(f.design, {});
    ASSERT_EQ(f.grid.rows, 4);
    ASSERT_EQ(f.grid.tile_h, 200);
    f.options.cellbox_full_flow = true;
    f.grid.critical_reserved_h.resize(f.grid.size());
    f.grid.critical_reserved_v.resize(f.grid.size());
    f.routes.segments.clear();
    for (int net = 0; net < 2; ++net) {
      auto& nodes = f.routes.trees[net].nodes;
      nodes.resize(2);
      nodes[0].tile               = net * 2;
      nodes[1].tile               = net * 2 + 1;
      nodes[0].direct_carrier_tap = nodes[1].direct_carrier_tap = direct;
      Segment v;
      v.net     = net;
      v.layer   = 0;
      v.lo      = net * 2;
      v.hi      = net * 2 + 1;
      v.node_lo = 0;
      v.node_hi = 1;
      f.routes.segments.push_back(v);
    }
    // Actual leaf carriers are [200,400] and [600,800]. Whole-end-tile envelopes [100,500] and
    // [500,900] collide unnecessarily. The gap between the real wires exceeds native end spacing.
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned, direct ? 0u : 1u);
    if (direct) {
      EXPECT_EQ(f.routes.segments[0].track, f.routes.segments[1].track);
      EXPECT_EQ(f.routes.segments[0].track_state, uint8_t(Track_state::checked));
      EXPECT_EQ(f.routes.segments[1].track_state, uint8_t(Track_state::checked));
    }
  }
}

TEST(LegrouteRoute, CriticalHorizontalLeafNeedsNoGlobalVerticalRiser) {
  Track_fixture f(false);
  f.tech.layers["M1"]   = f.tech.layers.at("M3");
  f.tech.layers["M2"]   = f.tech.layers.at("M4");
  f.tech.routing_layers = {"M1", "M2", "M3", "M4"};
  f.tech.sites["site"]  = {.05, .05, true};
  for (int row = 0; row < 4; ++row) {
    f.fp.rows.push_back({"r" + std::to_string(row), "site", 100, 100 + row * 50, 8, 50, physical::Orientation::N});
  }
  f.fp.tracks = {
      {"X", "M3", 50, 10, 100},
      {"Y", "M4", 50, 10, 100}
  };
  f.design.widest = 200;
  f.grid          = build_grid(f.design, {});
  ASSERT_EQ(f.grid.cols, 2);
  f.options.cellbox_full_flow = true;
  f.grid.critical_reserved_h.resize(f.grid.size());
  f.grid.critical_reserved_v.resize(f.grid.size());
  f.routes.segments.clear();
  auto& nodes = f.routes.trees[0].nodes;
  nodes.resize(2);
  for (int i = 0; i < 2; ++i) {
    nodes[i].tile               = i;
    nodes[i].direct_carrier_tap = true;
  }
  Segment h;
  h.net     = 0;
  h.layer   = 1;
  h.hi      = 1;
  h.node_lo = 0;
  h.node_hi = 1;
  f.routes.segments.push_back(h);
  assign_tracks(f.design, f.grid, f.routes, 1);
  EXPECT_EQ(f.routes.unassigned, 0u);
  EXPECT_EQ(f.routes.segments[0].track_state, uint8_t(Track_state::checked));
  for (const auto& node : nodes) {
    EXPECT_LT(node.riser_track, 0);
    EXPECT_EQ(node.riser_state, uint8_t(Track_state::none));
  }
}

TEST(LegrouteRoute, CriticalEndSpacingUsesTheEmittedHalfWidthCaps) {
  for (int backward : {5, 15}) {
    Track_fixture f(false);
    f.tech.layers["M3"].rules.properties["LEF58_EOLKEEPOUT"]
        = {backward == 5 ? "EOLKEEPOUT .020 EXTENSION .005 .100 .005 CORNERONLY ;"
                         : "EOLKEEPOUT .020 EXTENSION .015 .100 .015 CORNERONLY ;"};
    f.build_reservation_grid();
    f.options.cellbox_full_flow = true;
    f.grid.critical_reserved_h.resize(f.grid.size());
    f.grid.critical_reserved_v.resize(f.grid.size());
    for (auto& tree : f.routes.trees) {
      for (auto& node : tree.nodes) {
        node.direct_carrier_tap = true;
      }
    }
    const physical::Signal_metal_rules rules(f.tech, f.fp);
    const auto                         geometry = planned_wire_geometry(f.tech, f.grid.dbu, f.grid.die, "M3");
    const auto                         first    = planned_wire_shape(geometry, false, 150, 250, 150);
    const auto                         second   = planned_wire_shape(geometry, false, 250, 350, 250);
    EXPECT_EQ(rules.spacing(first, second).legal, backward == 5);
    // Doubled caps hide the 15-DBU native endpoint keepout by moving the corners too far apart.
    ASSERT_TRUE(rules
                    .spacing(
                        {
                            "M3",
                            {145, 140, 155, 260}
    },
                        {"M3", {245, 240, 255, 360}})
                    .legal);
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned, backward == 5 ? 0u : 1u);
  }
}

TEST(LegrouteRoute, AdjacentBranchesOfTheSameNetShareTheirTrack) {
  Track_fixture f(true);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(f.routes.unassigned, 0u);
  ASSERT_GE(f.routes.segments[0].track, 0);
  EXPECT_EQ(f.routes.segments[0].track, f.routes.segments[1].track);
}

TEST(LegrouteRoute, DensityReservationMatchesPhysicalTrackSelectionAndLayerMask) {
  for (bool reserve_vertical : {false, true}) {
    Track_fixture f(false);
    f.options.reserved_tracks_per_um = 1;
    f.options.reserved_layers        = reserve_vertical ? "" : "M4";
    f.grid.x0                        = 900;
    f.grid.tile_w                    = 200;  // the two tracks950 and1050 straddle the absolute1um boundary
    f.grid.die = f.fp.die = {0, 0, 2000, 1000};
    const Track_reservations policy(f.options, f.tech, f.grid.dbu);
    const auto [a, b] = f.grid.tracks(0, 0);
    ASSERT_EQ(b - a + 1, 2);
    int available = 0;
    for (auto k = a; k <= b; ++k) {
      const auto x  = f.grid.track_coord(0, k);
      available    += !policy.selected("M3", x, x - f.grid.layers[0].pitch, k - a);
    }
    EXPECT_EQ(available, reserve_vertical ? 1 : 2);
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned, reserve_vertical ? 1u : 0u);
    std::set<int64_t> assigned;
    for (const auto& segment : f.routes.segments) {
      // An unassigned segment may retain a least-overlap candidate. Even that
      // candidate must never use a hard-reserved physical track.
      if (segment.track < 0) {
        continue;
      }
      const auto x = f.grid.track_coord(segment.layer, segment.track);
      EXPECT_FALSE(policy.selected("M3", x, x - f.grid.layers[0].pitch, segment.track - a));
      assigned.insert(x);
    }
    EXPECT_EQ(assigned.size(), size_t(available));
  }
}

TEST(LegrouteRoute, EmptyReservedTrackDoesNotCreateNeighborEndOfLineMetal) {
  for (bool same_net : {false, true}) {
    Track_fixture f(same_net);
    f.options.reserved_tracks                                = 1;
    f.options.reserved_layers                                = "M3";
    f.options.l5_fallback_shadow                             = true;
    f.tech.layers["M3"].rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .020 EXTENSION .020 .100 .020 CORNERONLY ;"};
    f.build_reservation_grid();
    const auto [first, last] = f.grid.tracks(0, 0);
    ASSERT_EQ(last - first + 1, 2);
    for (int capacity : f.grid.cap[0]) {
      EXPECT_EQ(capacity, 1);  // Plan excludes exactly the reserved physical track.
    }
    const Track_reservations policy(f.options, f.tech, f.grid.dbu);
    const auto               x = f.grid.track_coord(0, first);
    ASSERT_TRUE(policy.selected("M3", x, x - f.grid.layers[0].pitch, 0));
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned, same_net ? 0u : 1u);
    EXPECT_EQ(f.routes.segments[0].track_state, uint8_t(Track_state::checked));
    EXPECT_EQ(f.routes.segments[1].track_state, uint8_t(same_net ? Track_state::checked : Track_state::fallback));
    for (const auto& segment : f.routes.segments) {
      EXPECT_EQ(segment.track, last);  // Even a fallback cannot take the reserved center.
    }
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
    EXPECT_GT(f.routes.track_fallback_shadow["ledger:bands"], 0u);
  }
}

TEST(LegrouteRoute, RealPowerOnAReservedTrackStillBlocksNeighborEndOfLineAccess) {
  Track_fixture f(true);
  f.options.reserved_tracks                                = 1;
  f.options.reserved_layers                                = "M3";
  f.options.l5_fallback_shadow                             = true;
  f.tech.layers["M3"].rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .020 EXTENSION .020 .100 .020 CORNERONLY ;"};
  f.build_reservation_grid(true);
  const auto [first, last] = f.grid.tracks(0, 0);
  ASSERT_EQ(last - first + 1, 2);
  for (int capacity : f.grid.cap[0]) {
    EXPECT_EQ(capacity, 1);  // Real PG and the reservation share one excluded center, without double counting.
  }
  ASSERT_FALSE(f.grid.obstacles[0].empty());
  const physical::Signal_metal_rules rules(f.tech, f.fp);
  ASSERT_FALSE(rules
                   .spacing(
                       {
                           "M3",
                           {145, 90, 155, 310}
  },
                       {"M3", {245, 90, 255, 310}})
                   .legal);
  assign_tracks(f.design, f.grid, f.routes, 1);
  EXPECT_EQ(f.routes.unassigned, 2u);
  for (const auto& segment : f.routes.segments) {
    EXPECT_EQ(segment.track, last);
    EXPECT_EQ(segment.track_state, uint8_t(Track_state::fallback));
  }
  ASSERT_FALSE(f.routes.track_failure_samples.empty());
  EXPECT_NE(f.routes.track_failure_samples[0].find("envelope t-1 -1 [90,310] hard"), std::string::npos)
      << f.routes.track_failure_samples[0];
  EXPECT_EQ(f.routes.track_fallback_shadow["M3:segment_final:shadow_free"], 0u);
  EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
}

TEST(LegrouteRoute, PairedAllowanceLimitsForeignTracksWithoutChoosingReservedCoordinates) {
  for (bool paired : {false, true}) {
    Track_fixture f(false);
    f.options.l5_fallback_shadow = true;
    f.build_reservation_grid();
    if (paired) {
      f.paired_budget(1, 1);
    }
    assign_tracks(f.design, f.grid, f.routes, 2);
    EXPECT_EQ(f.routes.unassigned, paired ? 1u : 0u);
    EXPECT_EQ(f.routes.segments[0].track_state, uint8_t(Track_state::checked));
    EXPECT_EQ(f.routes.segments[1].track_state, uint8_t(paired ? Track_state::fallback : Track_state::checked));
    if (paired) {
      EXPECT_EQ(f.routes.segments[0].track, f.routes.segments[1].track);  // A fallback cannot take the promised second track.
      EXPECT_GT(f.routes.track_pick_rejections["M3:cellbox_allowance"], 0u);
    } else {
      EXPECT_NE(f.routes.segments[0].track, f.routes.segments[1].track);
    }
    EXPECT_EQ(f.routes.paired_allowance_short_tiles, 0u);
    EXPECT_EQ(f.routes.paired_allowance_short_tracks, 0u);
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
  }
}

TEST(LegrouteRoute, PairedAllowanceKeepsDifferentDestinationNetsElectricallySeparate) {
  Track_fixture f(false);
  f.build_reservation_grid();
  f.paired_budget(1, 2, {0, 1});
  for (auto& tree : f.routes.trees) {
    tree.nodes[1].box = 0;
  }
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(f.routes.unassigned, 0u);
  EXPECT_NE(f.routes.segments[0].track, f.routes.segments[1].track);
  for (const auto& segment : f.routes.segments) {
    EXPECT_EQ(segment.track_state, uint8_t(Track_state::checked));
  }
  EXPECT_EQ(f.routes.paired_allowance_short_tracks, 0u);
}

TEST(LegrouteRoute, PairedAllowanceDoesNotExemptAnIncidentNetPassingThroughItsBox) {
  Track_fixture f(true);
  f.build_reservation_grid();
  f.paired_budget(1, 2, {0});
  f.routes.trees[0].nodes[1].box = 0;
  auto crossing                  = f.routes.segments[0];
  crossing.hi                    = 2;
  crossing.node_hi               = 2;  // This piece passes the middle box without ending at its node.
  f.routes.segments.push_back(crossing);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(f.routes.unassigned, 1u);
  EXPECT_EQ(f.routes.segments[0].track_state, uint8_t(Track_state::checked));
  EXPECT_EQ(f.routes.segments[1].track_state, uint8_t(Track_state::checked));
  EXPECT_EQ(f.routes.segments[2].track_state, uint8_t(Track_state::fallback));
  EXPECT_EQ(f.routes.segments[2].track, -1);
  EXPECT_EQ(f.routes.paired_allowance_short_tracks, 0u);
}

TEST(LegrouteRoute, PairedAllowanceDistinguishesOwnAndForeignIndependentRisers) {
  for (bool own_box : {false, true}) {
    Track_fixture f(true);
    f.build_reservation_grid();
    f.paired_budget(0, 2, {0});
    f.routes.trees[0].nodes[0].box = own_box ? 0 : -1;
    f.routes.segments.resize(1);
    auto& segment = f.routes.segments[0];
    segment.layer = 1;
    segment.lo = segment.hi = 0;
    segment.node_lo = segment.node_hi = 0;
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned, own_box ? 0u : 1u);
    EXPECT_EQ(f.routes.track_failures["riser"], own_box ? 0u : 1u);
    EXPECT_EQ(f.routes.trees[0].nodes[0].riser_state, track_state(own_box));
    EXPECT_EQ(f.routes.paired_allowance_short_tracks, 0u);
  }
}

TEST(LegrouteRoute, PairedAllowanceDiagnosesPrescribedOveruseInTheSameAdmissionAudit) {
  Track_fixture f(false);
  f.build_reservation_grid();
  f.paired_budget(1, 2);
  f.routes.segments[0].track = 1;
  f.routes.segments[1].track = 2;
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(f.routes.unassigned, 2u);
  EXPECT_EQ(f.routes.track_failures["fixed"], 2u);
  for (const auto& segment : f.routes.segments) {
    EXPECT_EQ(segment.track_state, uint8_t(Track_state::fallback));
  }
  EXPECT_EQ(f.routes.paired_allowance_short_tiles, 1u);
  EXPECT_EQ(f.routes.paired_allowance_short_tracks, 2u);
}

TEST(LegrouteRoute, PairedAllowanceDeduplicatesPowerAndGenericExclusions) {
  Track_fixture f(false);
  f.options.reserved_tracks = 1;
  f.options.reserved_layers = "M3";
  f.build_reservation_grid(true);
  f.design.widest = 300;
  f.grid          = build_grid(f.design, {});
  ASSERT_EQ(f.grid.resources.layers[0].bands[0].size(), 3u);
  ASSERT_EQ(f.grid.resources.layers[0].tiles[1].exclusions.size(), 1u);  // PG and generic reservation share x=150.
  f.paired_budget(1, 1);
  EXPECT_EQ(f.grid.paired_pin_reservations[0].vertical_usable, 2u);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(f.routes.unassigned, 1u);
  EXPECT_EQ(f.routes.paired_allowance_short_tracks, 0u);
  for (const auto& segment : f.routes.segments) {
    EXPECT_NE(segment.track, 1);
  }
}

TEST(LegrouteRoute, PairedAllowanceRejectsTracksOutsideTheFiniteCatalog) {
  for (int box_tile : {1, 2}) {
    Track_fixture f(true);
    f.build_reservation_grid();
    auto& catalog = f.grid.resources.layers[0];
    catalog.bands[0].resize(1);  // Only x=150 exists; Grid::tracks still proposes x=250 from its pitch.
    for (size_t tile = 0; tile < f.grid.size(); ++tile) {
      catalog.tiles[tile].coarse_capacity = 1;
      f.grid.cap[0][tile]                 = 1;
    }
    f.paired_budget(box_tile, 1, {0});
    f.routes.segments.resize(1);  // Spans tiles 0..1: box_tile=2 also tests transit-only catalog admission.
    f.routes.trees[0].nodes[box_tile].box = 0;
    f.block_layer(0, {145, 90, 155, 310});  // The one represented track is physically blocked.
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned, 1u);
    EXPECT_EQ(f.routes.segments[0].track, -1);
    EXPECT_EQ(f.routes.segments[0].track_state, uint8_t(Track_state::fallback));
    EXPECT_EQ(f.routes.paired_allowance_short_tracks, 0u);
  }
}

TEST(LegrouteRoute, PairedAllowanceRejectsExcludedPrescribedTracksWithoutChargingPowerTwice) {
  Track_fixture f(true);
  f.options.reserved_tracks = 1;
  f.options.reserved_layers = "M3";
  f.build_reservation_grid(true);
  f.paired_budget(1, 1, {0});
  f.routes.segments.resize(1);
  f.routes.trees[0].nodes[1].box = 0;
  f.routes.segments[0].track     = 1;  // x=150 is both PG-occupied and generically reserved.
  assign_tracks(f.design, f.grid, f.routes, 1);
  EXPECT_EQ(f.routes.unassigned, 1u);
  EXPECT_EQ(f.routes.track_failures["fixed"], 1u);
  EXPECT_EQ(f.routes.segments[0].track_state, uint8_t(Track_state::fallback));
  EXPECT_EQ(f.routes.paired_allowance_short_tracks, 0u);
}

TEST(LegrouteRoute, PairedAllowanceReplacesTheApproximateBoxDemandCharge) {
  Track_fixture f(true);
  f.build_reservation_grid();
  f.paired_budget(1, 1);
  f.design.cell           = {0, 1};
  f.design.nets[0].routed = true;
  f.design.nets[0].pins   = {{.comp = 0}, {.comp = 1}};
  Boxes boxes;
  boxes.box_of = {0, 1};
  boxes.boxes.resize(2);
  boxes.boxes[0].tile = 0;
  boxes.boxes[1].tile = 2;
  box_demand(f.design, f.grid, boxes, f.routes);
  const auto expected = transit_capacity(f.grid);
  EXPECT_EQ(f.routes.cap_h, expected.first);
  EXPECT_EQ(f.routes.cap_v, expected.second);
  EXPECT_EQ(f.routes.box_dem_v, std::vector<double>(f.grid.size(), 0));
}

TEST(LegrouteRoute, PairedAllowanceKeepsTheSeparateInternalOnlyVerticalEstimate) {
  Track_fixture f(false);
  f.build_reservation_grid();
  f.paired_budget(1, 0);  // No external pins: the nominal paired allowance is zero.
  auto& reservation                      = f.grid.paired_pin_reservations[0];
  reservation.internal_nets              = {2};
  reservation.internal_vertical_estimate = 1;
  f.design.nets.resize(3);
  f.routes.trees.resize(3);
  auto& catalog                    = f.grid.resources.layers[0];
  catalog.tiles[1].cellbox_budget  = 1;
  catalog.tiles[1].coarse_capacity = catalog.represented_capacity(1);
  f.grid.cap[0][1]                 = int16_t(catalog.tiles[1].coarse_capacity);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(reservation.vertical_tracks, 0u);
  EXPECT_EQ(f.routes.unassigned, 1u);
  EXPECT_EQ(f.routes.segments[0].track, f.routes.segments[1].track);
  EXPECT_EQ(f.routes.paired_allowance_short_tracks, 0u);
}

TEST(LegrouteRoute, DifferentNetsCannotShareTheOverlappingTrack) {
  Track_fixture f(false);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_GT(f.routes.unassigned, 0u);
}

TEST(LegrouteRoute, StableHorizontalRepairUsesCanonicalStaggeredEndGeometry) {
  for (bool repair : {false, true}) {
    Track_fixture f(false);
    f.build_stable_horizontal_row();
    f.options.l5_repair_ms       = repair ? 1000 : 0;
    f.options.l5_fallback_shadow = true;
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned, repair ? 0u : 1u);
    EXPECT_EQ(f.routes.segments[0].track, 1);
    EXPECT_EQ(f.routes.segments[1].track_state, track_state(repair));
    if (repair) {
      EXPECT_EQ(f.routes.segments[1].track, 2);
    }
    EXPECT_EQ(f.routes.l5_repair_successes, repair ? 1u : 0u);
    EXPECT_EQ(f.routes.track_fallback_shadow["repair:stable_h_wire_only"], repair ? 1u : 0u);
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
    for (int net = 0; net < 2; ++net) {
      EXPECT_EQ(f.routes.trees[net].nodes[0].riser_track, 6 + net * 10);
      EXPECT_EQ(f.routes.trees[net].nodes[1].riser_track, 32 + net * 10);
    }
    if (repair) {
      const auto                         materialized = final_planned_geometry(f.design, f.grid, {}, f.routes);
      std::vector<physical::Shape>       wires;
      const physical::Signal_metal_rules rules(f.tech, f.fp);
      for (int net = 0; net < 2; ++net) {
        physical::Net physical_net;
        physical_net.wires.push_back({"ROUTED", materialized.paths[net]});
        for (const auto& shape : physical::route_geometry(f.tech, f.fp, physical_net)) {
          if (shape.layer == "M4") {
            EXPECT_TRUE(rules.rectangle(shape).legal);
            wires.push_back(shape);
          }
        }
        const auto x0 = f.grid.track_coord(0, f.routes.trees[net].nodes[0].riser_track);
        const auto x1 = f.grid.track_coord(0, f.routes.trees[net].nodes[1].riser_track);
        const auto y  = f.grid.track_coord(1, f.routes.segments[net].track);
        const std::set<std::tuple<std::string, std::string, int64_t, int64_t>> joins(materialized.joins[net].begin(),
                                                                                     materialized.joins[net].end());
        EXPECT_TRUE(joins.contains({"M4", "M3", x0, y}));
        EXPECT_TRUE(joins.contains({"M4", "M3", x1, y}));
      }
      ASSERT_EQ(wires.size(), 2u);
      EXPECT_TRUE(rules.spacing(wires[0], wires[1]).legal);
      // These are planned joins, not a via selection or a physical connectivity certificate.
    }
  }
}

TEST(LegrouteRoute, StableHorizontalRepairChecksActualPowerGeometryInsteadOfProjectedEnds) {
  for (bool repair : {false, true}) {
    Track_fixture f(false);
    f.build_stable_horizontal_row();
    f.grid.tile_h = 300;  // The third physical track carries a short fixed power landing.
    const physical::Shape power{
        "M4",
        {470, 345, 510, 355}
    };
    f.block_layer(1, power.rect);
    f.options.l5_repair_ms                     = repair ? 1000 : 0;
    const auto                         profile = planned_wire_geometry(f.tech, f.grid.dbu, f.grid.die, "M4");
    const physical::Signal_metal_rules rules(f.tech, f.fp);
    ASSERT_TRUE(rules.spacing(planned_wire_shape(profile, true, 130, 650, 150), power).legal);
    ASSERT_TRUE(rules.spacing(planned_wire_shape(profile, true, 330, 850, 250), power).legal);
    assign_tracks(f.design, f.grid, f.routes, 1);
    if (!repair) {
      EXPECT_GT(f.routes.unassigned, 0u);
      continue;
    }
    EXPECT_EQ(f.routes.unassigned, 0u);
    for (int net = 0; net < 2; ++net) {
      const auto& segment = f.routes.segments[net];
      EXPECT_EQ(segment.track_state, uint8_t(Track_state::checked));
      const auto shape = planned_wire_shape(profile, true, 130 + net * 200, 650 + net * 200, f.grid.track_coord(1, segment.track));
      EXPECT_TRUE(rules.spacing(shape, power).legal);
    }
  }
}

TEST(LegrouteRoute, StableHorizontalRepairRejectsAnActualAdjacentPowerCornerConflict) {
  Track_fixture f(false);
  f.build_stable_horizontal_row();
  f.grid.tile_h = 300;
  const physical::Shape power{
      "M4",
      {640, 345, 680, 355}
  };
  f.block_layer(1, power.rect);
  f.options.l5_repair_ms                     = 1000;
  const auto                         profile = planned_wire_geometry(f.tech, f.grid.dbu, f.grid.die, "M4");
  const physical::Signal_metal_rules rules(f.tech, f.fp);
  // The landing projects only onto t3. A's t1 center track is clear, but its real EOL corner is not.
  ASSERT_FALSE(rules.spacing(planned_wire_shape(profile, true, 130, 650, 150), power).legal);
  assign_tracks(f.design, f.grid, f.routes, 1);
  EXPECT_GT(f.routes.unassigned, 0u);
  EXPECT_EQ(f.routes.segments[0].track_state, uint8_t(Track_state::fallback));
}

TEST(LegrouteRoute, StableHorizontalRepairCanUseAnAlternateLayerWithoutMovingRisersOrThePlanPath) {
  for (bool repair : {false, true}) {
    Track_fixture f(false);
    f.build_stable_horizontal_row();
    f.add_upper_layers();
    for (auto& tracks : f.fp.tracks) {
      tracks.count = 100;
    }
    f.block_layer(1, {100, 100, 1100, 300});  // Original M4 is unavailable; M6 has a different pitch and offset.
    f.options.l5_repair_ms       = repair ? 1000 : 0;
    f.options.l5_fallback_shadow = true;
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned, repair ? 0u : 1u);
    EXPECT_EQ(f.routes.segments[0].layer, 3);  // Ordinary compatible-layer retry placed A on M6 first.
    EXPECT_EQ(f.routes.segments[0].track, 2);
    const auto& target = f.routes.segments[1];
    EXPECT_EQ(target.layer, repair ? 3 : 1);
    EXPECT_EQ(target.track_state, track_state(repair));
    EXPECT_EQ(target.band, 0);
    EXPECT_EQ(target.lo, 0);
    EXPECT_EQ(target.hi, 1);
    EXPECT_EQ(f.routes.l5_repair_successes, repair ? 1u : 0u);
    EXPECT_EQ(f.routes.track_fallback_shadow["repair:stable_h_other_layer"], repair ? 1u : 0u);
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
    for (int n = 0; n < 2; ++n) {
      EXPECT_EQ(f.routes.trees[n].nodes[0].riser_layer, 0);
      EXPECT_EQ(f.routes.trees[n].nodes[1].riser_layer, 0);
      EXPECT_EQ(f.routes.trees[n].nodes[0].riser_track, 6 + n * 10);
      EXPECT_EQ(f.routes.trees[n].nodes[1].riser_track, 32 + n * 10);
    }
    if (repair) {
      EXPECT_EQ(target.track, 1);
      const auto y = f.grid.track_coord(3, target.track);
      EXPECT_EQ(y, 110);
      const auto geometry = final_planned_geometry(f.design, f.grid, {}, f.routes);
      const std::set<std::tuple<std::string, std::string, int64_t, int64_t>> joins(geometry.joins[1].begin(),
                                                                                   geometry.joins[1].end());
      EXPECT_TRUE(joins.contains({"M6", "M3", 330, y}));
      EXPECT_TRUE(joins.contains({"M6", "M3", 850, y}));
      physical::Net net;
      net.wires.push_back({"ROUTED", geometry.paths[1]});
      bool       found    = false;
      const auto expected = planned_wire_shape(planned_wire_geometry(f.tech, f.grid.dbu, f.grid.die, "M6"), true, 330, 850, y);
      for (const auto& shape : physical::route_geometry(f.tech, f.fp, net)) {
        if (shape.layer == "M6") {
          EXPECT_EQ(shape, expected);
          found = true;
        }
      }
      EXPECT_TRUE(found);
    }
  }
}

TEST(LegrouteRoute, BlockedAlternateHorizontalRepairPreservesTheOriginalFallbackAndCheckedWires) {
  Track_fixture off(false), on(false);
  for (auto* fixture : {&off, &on}) {
    fixture->build_stable_horizontal_row();
    fixture->add_upper_layers();
    for (auto& tracks : fixture->fp.tracks) {
      tracks.count = 100;
    }
    fixture->block_layer(1, {100, 100, 1100, 300});
    fixture->block_layer(3, {1000, 100, 1100, 300});  // Beyond A's margin, but blocks B on every M6 track.
    fixture->options.l5_fallback_shadow = true;
  }
  on.options.l5_repair_ms = 1000;
  assign_tracks(off.design, off.grid, off.routes, 1);
  assign_tracks(on.design, on.grid, on.routes, 1);
  ASSERT_EQ(off.routes.unassigned, 1u);
  EXPECT_EQ(on.routes.unassigned, 1u);
  EXPECT_EQ(on.routes.l5_repair_successes, 0u);
  EXPECT_EQ(on.routes.segments[1].layer, 1);
  EXPECT_EQ(on.routes.segments[1].track_state, uint8_t(Track_state::fallback));
  EXPECT_EQ(on.routes.track_fallback_shadow["ledger:mismatched"], 0u);
  for (size_t i = 0; i < off.routes.segments.size(); ++i) {
    EXPECT_EQ(on.routes.segments[i].layer, off.routes.segments[i].layer);
    EXPECT_EQ(on.routes.segments[i].track, off.routes.segments[i].track);
    EXPECT_EQ(on.routes.segments[i].track_state, off.routes.segments[i].track_state);
  }
  EXPECT_EQ(on.routes.track_failures, off.routes.track_failures);
}

TEST(LegrouteRoute, StableHorizontalRepairRetainsRealEndOfLineAndUnknownBackgroundConstraints) {
  for (int mode = 0; mode < 3; ++mode) {
    SCOPED_TRACE(mode);
    Track_fixture f(false);
    f.build_stable_horizontal_row(mode == 0);
    f.options.l5_repair_ms       = 1000;
    f.options.l5_fallback_shadow = true;
    if (mode == 1) {
      f.routes.segments[0].track = 1;  // Prescribed H is immutable background, not a known regular-H request.
    }
    if (mode == 2) {
      f.block_layer(1, {1050, 245, 1100, 255});  // Beyond A's 340-DBU end margin, inside B's margin.
    }
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned, 1u);
    EXPECT_EQ(f.routes.l5_repair_successes, 0u);
    EXPECT_EQ(f.routes.segments[0].track, 1);
    EXPECT_EQ(f.routes.segments[1].track_state, uint8_t(Track_state::fallback));
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
    if (mode == 0) {
      const auto                         profile = planned_wire_geometry(f.tech, f.grid.dbu, f.grid.die, "M4");
      const physical::Signal_metal_rules rules(f.tech, f.fp);
      EXPECT_FALSE(
          rules.spacing(planned_wire_shape(profile, true, 130, 650, 150), planned_wire_shape(profile, true, 150, 670, 250)).legal);
    }
  }
}

TEST(LegrouteRoute, CanonicalHorizontalShapeMatchesMaterializationWithMinimumAreaAndDieEdges) {
  for (int mode = 0; mode < 3; ++mode) {
    Track_fixture f(true);
    f.grid.cols                    = 2;
    f.grid.rows                    = 1;
    f.grid.layers[0].offset        = mode == 2 ? 10 : 50;
    f.tech.layers["M4"].rules.area = mode ? .004 : 0;
    f.routes.trees[0].nodes.resize(2);
    for (int end = 0; end < 2; ++end) {
      auto& node       = f.routes.trees[0].nodes[end];
      node.tile        = end;
      node.riser_layer = 0;
      node.riser_track = (mode == 2 ? 0 : 1) + end;
      node.riser_state = uint8_t(Track_state::checked);
    }
    f.routes.segments.resize(1);
    auto& h                    = f.routes.segments[0];
    h.layer                    = 1;
    h.band                     = 0;
    h.node_lo                  = 0;
    h.node_hi                  = 1;
    h.track                    = 1;
    h.track_state              = uint8_t(Track_state::checked);
    const auto    x0           = f.grid.track_coord(0, f.routes.trees[0].nodes[0].riser_track);
    const auto    x1           = f.grid.track_coord(0, f.routes.trees[0].nodes[1].riser_track);
    const auto    expected     = planned_wire_shape(planned_wire_geometry(f.tech, f.grid.dbu, f.grid.die, "M4"), true, x0, x1, 150);
    const auto    materialized = final_planned_geometry(f.design, f.grid, {}, f.routes);
    physical::Net net;
    net.wires.push_back({"ROUTED", materialized.paths[0]});
    bool found = false;
    for (const auto& shape : physical::route_geometry(f.tech, f.fp, net)) {
      if (shape.layer == "M4") {
        EXPECT_EQ(shape, expected);
        found = true;
      }
    }
    EXPECT_TRUE(found);
    EXPECT_EQ(expected.rect.xh - expected.rect.xl, mode ? 400 : 110);
    if (mode == 2) {
      EXPECT_EQ(expected.rect.xl, 0);
    }
  }
}

TEST(LegrouteRoute, BoundedHorizontalRepairMovesACheckedWireToRecoverAGreedyFailure) {
  for (bool repair : {false, true}) {
    Track_fixture f(false);
    f.build_repair_row();
    f.options.l5_fallback_shadow = true;
    f.options.l5_repair_ms       = repair ? 1000 : 0;
    assign_tracks(f.design, f.grid, f.routes, 2);
    EXPECT_EQ(f.routes.unassigned, repair ? 0u : 1u);
    EXPECT_EQ(f.routes.segments[0].track, repair ? 2 : 1);
    EXPECT_EQ(f.routes.segments[1].track, 1);
    EXPECT_EQ(f.routes.segments[0].track_state, uint8_t(Track_state::checked));
    EXPECT_EQ(f.routes.segments[1].track_state, track_state(repair));
    EXPECT_EQ(f.routes.track_failures["horizontal"], repair ? 0u : 1u);
    EXPECT_EQ(f.routes.l5_repair_attempts, repair ? 1u : 0u);
    EXPECT_EQ(f.routes.l5_repair_successes, repair ? 1u : 0u);
    EXPECT_EQ(f.routes.l5_repair_unknown, 0u);
    EXPECT_EQ(f.routes.l5_repair_unsat, 0u);
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
  }
}

TEST(LegrouteRoute, CriticalSameTrackUsesNativeRulesAndKeepsHardAndUnsupportedRules) {
  for (int mode = 0; mode < 4; ++mode) {
    SCOPED_TRACE(mode);
    Track_fixture f(false);
    f.grid.cols             = 12;
    f.grid.rows             = 1;
    f.grid.tile_w           = 56;
    f.grid.tile_h           = 48;
    f.grid.layers[1].pitch  = 48;
    f.grid.layers[1].offset = 12;
    f.grid.layers[1].width  = 24;
    f.grid.obstacle_band[0].resize(12);
    f.grid.obstacle_band[1].resize(1);
    auto& layer   = f.tech.layers["M4"];
    layer.width   = .024;
    layer.pitch_x = layer.pitch_y = .048;
    layer.rules.area              = .002;
    layer.rules.spacing           = {
        {.024, {}, {}}
    };
    layer.rules.parallel = {
        {{0., .025}, {0.}, {{.024}, {.072}}}
    };
    layer.rules.properties["LEF58_SPACING"]            = {"SPACING .024 ENDOFLINE .025 WITHIN .04 ENDTOEND .04 ;"};
    layer.rules.properties["LEF58_EOLKEEPOUT"]         = {"EOLKEEPOUT .025 EXTENSION .048 .02425 .048 CORNERONLY ;"};
    layer.rules.properties["LEF58_CORNERSPACING"]      = {"CORNERSPACING CONVEXCORNER CORNERONLY .048 WIDTH 0.0 SPACING .04 ;"};
    layer.rules.properties["LEF58_WIDTHTABLE"]         = {"WIDTHTABLE .024 .12 .216 .312 .408 ;"};
    layer.rules.properties["LEF58_RIGHTWAYONGRIDONLY"] = {"RIGHTWAYONGRIDONLY ;"};
    if (mode == 3) {
      layer.rules.properties["LEF58_UNKNOWN"] = {"UNKNOWN ;"};
    }
    for (int net = 0; net < 2; ++net) {
      auto& segment   = f.routes.segments[net];
      segment.layer   = 1;
      segment.band    = 0;
      segment.lo      = net == 0 ? 0 : mode == 1 ? 5 : 7;
      segment.hi      = segment.lo + 4;
      segment.node_lo = 0;
      segment.node_hi = 1;
      auto& nodes     = f.routes.trees[net].nodes;
      nodes.resize(2);
      nodes[0].tile               = segment.lo;
      nodes[1].tile               = segment.hi;
      nodes[0].direct_carrier_tap = nodes[1].direct_carrier_tap = true;
    }
    f.enable_critical_repair();
    f.options.l5_repair_ms = 0;
    if (mode == 2) {
      f.block_layer(1, {480, 96, 490, 120});  // Remains hard even though signal-only spacing is now exact.
    }
    const auto                         geometry = planned_wire_geometry(f.tech, f.grid.dbu, f.grid.die, "M4");
    const physical::Signal_metal_rules rules(f.tech, f.fp);
    const auto                         a = planned_wire_shape(geometry, true, f.grid.cx(0), f.grid.cx(4), 108);
    const auto b = planned_wire_shape(geometry, true, f.grid.cx(mode == 1 ? 5 : 7), f.grid.cx(mode == 1 ? 9 : 11), 108);
    if (mode != 3) {
      EXPECT_EQ(*rules.clearance("M4"), 146);
      EXPECT_EQ(b.rect.xl - a.rect.xh, mode == 1 ? 32 : 144);
      EXPECT_EQ(rules.spacing(a, b).legal, mode != 1);
    }
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned == 0, mode == 0);
    if (mode == 0) {
      EXPECT_EQ(f.routes.segments[0].track, f.routes.segments[1].track);
    }
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
  }
}

TEST(LegrouteRoute, CriticalNeighborBandsUseFiniteNativeRulesWithDeterministicPhases) {
  for (int mode = 0; mode < 4; ++mode) {
    SCOPED_TRACE(mode);
    std::vector<std::pair<int64_t, uint8_t>> serial;
    for (int threads : {1, 4}) {
      Track_fixture f(false);
      f.grid.cols             = 7;
      f.grid.rows             = 3;
      f.grid.tile_w           = 48;
      f.grid.layers[0].pitch  = 48;
      f.grid.layers[0].offset = 12;
      f.grid.layers[0].width  = 24;
      f.grid.obstacle_band[0].resize(7);
      auto& layer   = f.tech.layers["M3"];
      layer.width   = .024;
      layer.pitch_x = layer.pitch_y = .048;
      layer.rules.spacing           = {
          {.024, {}, {}}
      };
      layer.rules.properties["LEF58_EOLKEEPOUT"] = {mode < 2 ? "EOLKEEPOUT .025 EXTENSION .048 .02425 .048 CORNERONLY ;"
                                                             : "EOLKEEPOUT .025 EXTENSION .048 .200 .048 CORNERONLY ;"};
      const int other_band                       = mode == 0 ? 1 : mode == 1 ? 2 : mode == 2 ? 3 : 6;
      for (int net = 0; net < 2; ++net) {
        auto& segment   = f.routes.segments[net];
        segment.band    = net ? other_band : 0;
        segment.lo      = 0;
        segment.hi      = 2;
        segment.node_lo = 0;
        segment.node_hi = 1;
        auto& nodes     = f.routes.trees[net].nodes;
        nodes.resize(2);
        nodes[0].tile               = f.grid.tile(segment.band, 0);
        nodes[1].tile               = f.grid.tile(segment.band, 2);
        nodes[0].direct_carrier_tap = nodes[1].direct_carrier_tap = true;
      }
      f.enable_critical_repair();
      f.options.l5_repair_ms                     = 0;
      const auto                         profile = planned_wire_geometry(f.tech, f.grid.dbu, f.grid.die, "M3");
      const auto                         a       = planned_wire_shape(profile, false, 150, 350, 108);
      const auto                         b       = planned_wire_shape(profile, false, 150, 350, 108 + 48 * other_band);
      const physical::Signal_metal_rules rules(f.tech, f.fp);
      const bool                         legal = mode == 1 || mode == 3;
      ASSERT_EQ(rules.spacing(a, b).legal, legal);
      assign_tracks(f.design, f.grid, f.routes, threads);
      EXPECT_EQ(f.routes.unassigned, legal ? 0u : 1u);
      EXPECT_EQ(f.routes.segments[0].track_state, uint8_t(Track_state::checked));
      EXPECT_EQ(f.routes.segments[1].track_state, track_state(legal));
      EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
      std::vector<std::pair<int64_t, uint8_t>> actual;
      for (const auto& segment : f.routes.segments) {
        actual.emplace_back(segment.track, segment.track_state);
      }
      if (threads == 1) {
        serial = actual;
      } else {
        EXPECT_EQ(actual, serial);
      }
    }
  }
}

TEST(LegrouteRoute, CriticalZeroEdgeIoBuildsCheckedCarrierWithoutInventingForestSegments) {
  for (int terminals : {1, 2}) {
    SCOPED_TRACE(terminals);
    Track_fixture f(true);
    f.grid.cols = f.grid.rows = 1;
    f.grid.tile_w             = 500;
    f.grid.tile_h             = 200;
    f.grid.obstacle_band[1].resize(1);
    f.routes.segments.clear();
    f.routes.trees[0].nodes.resize(1);
    auto& node = f.routes.trees[0].nodes[0];
    node.tile = node.box = node.term = 0;
    f.fp.terminals.reserve(2);
    for (int i = 0; i < terminals; ++i) {
      physical::Terminal terminal;
      terminal.name   = "source_io" + std::to_string(i);
      terminal.net    = "n0";
      terminal.x      = 250 + 200 * i;
      terminal.y      = 150;
      terminal.shapes = {
          {"M4", {-10, -5, 10, 5}}
      };
      f.fp.terminals.push_back(terminal);
      f.design.nets[0].pins.push_back({.term = i, .name = f.fp.terminals.back().name});
      node.pins.push_back(i);
    }
    auto& master = f.tech.macros["cell"];
    master.width = master.height = .05;
    master.pins["A"].shapes      = {
        {"M1", {.01, .01, .02, .02}}
    };
    physical::Component cell;
    cell.name    = "c0";
    cell.macro   = "cell";
    cell.place.x = cell.place.y = 100;
    f.fp.components.push_back(cell);
    f.design.cell  = {0};
    f.design.macro = {&master};
    f.design.nets[0].pins.push_back({.comp = 0, .lef = &master.pins["A"], .name = "A"});
    node.pins.push_back(terminals);
    f.enable_critical_repair();
    f.options.l5_repair_ms = 0;
    for (int i = 0; i < terminals; ++i) {
      f.fp.nets[0].connections.push_back({"PIN", f.fp.terminals[i].name});
    }
    f.fp.nets[0].connections.push_back({"c0", "A"});
    const auto source = f.fp.nets[0].connections;
    assign_tracks(f.design, f.grid, f.routes, 4);
    EXPECT_TRUE(f.routes.segments.empty());
    EXPECT_EQ(f.routes.unassigned, 0u);
    EXPECT_EQ(node.riser_state, uint8_t(Track_state::checked));
    EXPECT_EQ(node.stub_state, uint8_t(Track_state::checked));
    EXPECT_EQ(node.stub_access_state, uint8_t(Track_state::checked));
    EXPECT_EQ(node.pins.size(), size_t(terminals + 1));
    EXPECT_EQ(f.fp.nets[0].connections, source);
    const auto geometry = final_planned_geometry(f.design, f.grid, {}, f.routes);
    EXPECT_FALSE(geometry.paths[0].empty());
    EXPECT_TRUE(geometry.io_stub_failure[0].empty());
    EXPECT_EQ(geometry.path_state[0].size(), geometry.paths[0].size());
    EXPECT_TRUE(std::ranges::all_of(geometry.path_state[0], [](auto state) { return state == uint8_t(Track_state::checked); }));
  }
}

TEST(LegrouteRoute, CriticalSameTrackMinimumAreaCannotMissHardOrSameOwnerOverhangs) {
  for (bool same_owner_overhang : {false, true}) {
    SCOPED_TRACE(same_owner_overhang);
    Track_fixture f(false);
    f.grid.cols   = 110;
    f.grid.rows   = 1;
    f.grid.tile_w = 20;
    f.grid.tile_h = 100;
    f.grid.die = f.fp.die = {0, 0, 4000, 1000};
    f.grid.obstacle_band[0].resize(110);
    f.grid.obstacle_band[1].resize(1);
    f.tech.layers["M4"].rules.area = .005;  // Width10 needs500DBU of finite metal, far beyond the old margin.
    f.routes.segments.clear();
    f.routes.trees.resize(same_owner_overhang ? 2 : 1);
    f.design.nets.resize(f.routes.trees.size());
    const auto add = [&](int owner, int lo, int hi, bool prescribed) {
      auto&     nodes = f.routes.trees[owner].nodes;
      const int first = int(nodes.size());
      nodes.resize(nodes.size() + 2);
      nodes[first].tile               = lo;
      nodes[first + 1].tile           = hi;
      nodes[first].direct_carrier_tap = nodes[first + 1].direct_carrier_tap = true;
      Segment segment;
      segment.net     = owner;
      segment.layer   = 1;
      segment.lo      = lo;
      segment.hi      = hi;
      segment.node_lo = first;
      segment.node_hi = first + 1;
      segment.track   = prescribed ? 1 : -1;
      f.routes.segments.push_back(segment);
    };
    for (auto& tree : f.routes.trees) {
      tree.nodes.clear();
    }
    if (same_owner_overhang) {
      add(0, 70, 100, true);
      add(0, 70, 71, true);  // Its finite left overhang must survive merging with the long same-owner span.
      add(1, 20, 60, false);
    } else {
      add(0, 0, 1, false);
      f.block_layer(1, {400, 145, 420, 155});  // Outside [lo-margin,hi+margin], inside canonical minimum-area growth.
    }
    f.enable_critical_repair();
    f.options.l5_repair_ms = 0;
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_GT(f.routes.unassigned, 0u);
    EXPECT_NE(f.routes.segments.back().track_state, uint8_t(Track_state::checked));
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
  }
}

TEST(LegrouteRoute, CriticalSatRepairValidatesNativeOwnersBeforePublishingMoves) {
  for (bool hidden_native_blocker : {false, true}) {
    Track_fixture f(false);
    f.build_repair_row();
    f.enable_critical_repair();
    for (int net = 0; net < 2; ++net) {
      auto& nodes = f.routes.trees[net].nodes;
      nodes.resize(2);
      nodes[0].tile               = net == 0 ? 0 : 4;
      nodes[1].tile               = net == 0 ? 4 : 7;
      nodes[0].direct_carrier_tap = nodes[1].direct_carrier_tap = true;
      f.routes.segments[net].node_lo                            = 0;
      f.routes.segments[net].node_hi                            = 1;
    }
    if (hidden_native_blocker) {
      // Not represented by this fixture's conservative band occupancy: only complete native validation sees it.
      physical::Net power;
      power.name = "VDD";
      power.use  = "POWER";
      power.wires.push_back(
          {"FIXED", {{"M4", 10, {}, {physical::Route_point{400, 250, {}}, physical::Route_point{450, 250, {}}}, {}}}});
      f.fp.special_nets.push_back(std::move(power));
    }
    const auto source_before = f.fp.nets;
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_GT(f.routes.l5_repair_sat_calls, 0u);
    EXPECT_GT(f.routes.l5_repair_sat_variables, 0u);
    EXPECT_EQ(f.routes.l5_repair_successes, hidden_native_blocker ? 0u : 1u);
    EXPECT_EQ(f.routes.unassigned, hidden_native_blocker ? 1u : 0u);
    EXPECT_EQ(f.routes.segments[0].track, hidden_native_blocker ? 1 : 2);
    EXPECT_EQ(f.routes.segments[1].track_state, track_state(!hidden_native_blocker));
    EXPECT_EQ(f.fp.nets, source_before);
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
    if (hidden_native_blocker) {
      EXPECT_GT(f.routes.track_fallback_shadow["repair:unknown:native_owner_validation"], 0u);
      EXPECT_EQ(f.routes.track_fallback_shadow["repair:native_reject:spacing"], 1u);
      EXPECT_TRUE(std::ranges::any_of(f.routes.track_failure_samples, [](const auto& text) {
        return text.find("Local SAT native rejection spacing") != std::string::npos && text.find("VDD") != std::string::npos
               && text.find("choices") != std::string::npos;
      }));
    }
  }
}

TEST(LegrouteRoute, CriticalSatRepairMovesTheActualBlockerInsteadOfNearerUnrelatedWires) {
  for (bool repair : {false, true}) {
    Track_fixture f(false);
    f.build_repair_row();
    f.grid.cols = 18;
    f.grid.obstacles[1].clear();
    f.grid.obstacle_band[1][0].clear();
    f.add_upper_layers();
    f.grid.layers[3].pitch      = 20;
    f.grid.layers[3].offset     = 10;
    f.tech.layers["M6"].pitch_x = f.tech.layers["M6"].pitch_y = .02;
    f.tech.layers["M6"].offset_x = f.tech.layers["M6"].offset_y = .01;
    f.fp.tracks.clear();
    f.block_layer(1, {1800, 245, 1900, 255});
    f.block_layer(3, {100, 100, 200, 300});  // The long blocker cannot escape to M6 either.
    f.block_layer(3, {1800, 100, 1900, 300});
    f.routes.trees.resize(8);
    f.design.nets.resize(8);
    f.routes.segments.resize(8);
    for (int net = 0; net < 8; ++net) {
      auto& segment   = f.routes.segments[net];
      segment.net     = net;
      segment.layer   = net < 2 ? 1 : 3;
      segment.lo      = net == 0 ? 0 : net == 1 ? 14 : 13;
      segment.hi      = net == 0 ? 14 : net == 1 ? 17 : 16;
      segment.node_lo = 0;
      segment.node_hi = 1;
      auto& nodes     = f.routes.trees[net].nodes;
      nodes.resize(2);
      nodes[0].tile               = segment.lo;
      nodes[1].tile               = segment.hi;
      nodes[0].direct_carrier_tap = nodes[1].direct_carrier_tap = true;
    }
    f.enable_critical_repair();
    f.options.l5_repair_ms = repair ? 1000 : 0;
    // Six M6 wires have closer interval centers than the long M4 blocker. Moving any of them cannot
    // release the target's only usable M4 track; the upper M4 lane and every M6 lane hit fixed background.
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned, repair ? 0u : 1u);
    EXPECT_EQ(f.routes.segments[0].track, repair ? 2 : 1);
    EXPECT_EQ(f.routes.segments[1].track_state, track_state(repair));
    EXPECT_EQ(f.routes.l5_repair_successes, repair ? 1u : 0u);
    EXPECT_EQ(f.routes.track_fallback_shadow["repair:window:blockers"], repair ? 1u : 0u);
    for (int net = 2; net < 8; ++net) {
      EXPECT_EQ(f.routes.segments[net].layer, 3);
      EXPECT_EQ(f.routes.segments[net].track_state, uint8_t(Track_state::checked));
    }
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
  }
}

TEST(LegrouteRoute, FailedHorizontalRepairPreservesTheOriginalAssignmentAndShadow) {
  Track_fixture off(false), on(false);
  for (auto* fixture : {&off, &on}) {
    fixture->build_repair_row(true);
    fixture->options.l5_fallback_shadow = true;
  }
  on.options.l5_repair_ms = 1000;
  assign_tracks(off.design, off.grid, off.routes, 1);
  assign_tracks(on.design, on.grid, on.routes, 1);
  EXPECT_EQ(on.routes.unassigned, 1u);
  EXPECT_EQ(on.routes.l5_repair_attempts, 1u);
  EXPECT_EQ(on.routes.l5_repair_successes, 0u);
  EXPECT_EQ(on.routes.l5_repair_unknown, 0u);
  EXPECT_EQ(on.routes.l5_repair_unsat, 1u);  // Exhaustion of this frozen-background two-request domain only.
  for (size_t i = 0; i < off.routes.segments.size(); ++i) {
    EXPECT_EQ(on.routes.segments[i].track, off.routes.segments[i].track);
    EXPECT_EQ(on.routes.segments[i].track_state, off.routes.segments[i].track_state);
  }
  EXPECT_EQ(on.routes.track_failures, off.routes.track_failures);
  EXPECT_EQ(on.routes.track_fallback_shadow, off.routes.track_fallback_shadow);
  EXPECT_EQ(on.routes.track_failure_samples, off.routes.track_failure_samples);
}

TEST(LegrouteRoute, HorizontalRepairDoesNotEraseAnImmutableSameNetContribution) {
  Track_fixture f(false);
  f.build_repair_row();
  f.options.l5_fallback_shadow = true;
  f.options.l5_repair_ms       = 1000;
  auto fixed                   = f.routes.segments[0];
  fixed.hi                     = 7;
  fixed.track                  = 1;  // A same-net prescribed span covers [100,900], beyond the movable A request.
  f.routes.segments.push_back(fixed);
  assign_tracks(f.design, f.grid, f.routes, 1);
  EXPECT_EQ(f.routes.unassigned, 1u);
  EXPECT_EQ(f.routes.segments[0].track, 1);
  EXPECT_EQ(f.routes.segments[2].track, 1);
  EXPECT_EQ(f.routes.segments[2].track_state, uint8_t(Track_state::prescribed));
  EXPECT_EQ(f.routes.l5_repair_successes, 0u);
  EXPECT_EQ(f.routes.l5_repair_unknown, 1u);  // Same-net merging prevents a complete-domain infeasibility claim.
  EXPECT_EQ(f.routes.l5_repair_unsat, 0u);
  EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
}

TEST(LegrouteRoute, HorizontalRepairKeepsFallbackBackgroundAsAnObstacle) {
  Track_fixture f(false);
  f.build_repair_row();
  f.options.l5_fallback_shadow = true;
  f.options.l5_repair_ms       = 1000;
  f.design.nets.resize(4);
  f.routes.trees.resize(4);
  auto fixed  = f.routes.segments[0];
  fixed.net   = 2;
  fixed.hi    = 1;
  fixed.track = 1;
  f.routes.segments.push_back(fixed);
  auto failed  = f.routes.segments[0];
  failed.net   = 3;
  failed.hi    = 7;
  failed.clock = true;  // Overlaps the prescribed wire; the other track hits PG, so this is a fixed-phase fallback.
  f.routes.segments.push_back(failed);
  assign_tracks(f.design, f.grid, f.routes, 1);
  EXPECT_EQ(f.routes.unassigned, 2u);
  EXPECT_EQ(f.routes.segments[0].track, 2);
  EXPECT_EQ(f.routes.segments[3].track_state, uint8_t(Track_state::fallback));
  EXPECT_EQ(f.routes.l5_repair_successes, 0u);
  EXPECT_EQ(f.routes.l5_repair_unsat, 1u);
  EXPECT_EQ(f.routes.track_fallback_shadow["M4:segment_final:shadow_free"], 1u);
  EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
}

TEST(LegrouteRoute, HorizontalRepairClosureLimitIsUnknownAndLeavesCheckedWiresUntouched) {
  Track_fixture f(false);
  f.build_repair_row();
  f.options.l5_fallback_shadow = true;
  f.options.l5_repair_ms       = 1000;
  f.grid.cols                  = 24;
  f.grid.obstacle_band[0].resize(24);
  f.grid.obstacles[1].clear();
  f.grid.obstacle_band[1][0].clear();
  f.block_layer(1, {2400, 245, 2500, 255});
  f.design.nets.resize(7);
  f.routes.trees.resize(7);
  f.routes.segments.clear();
  for (int n = 0; n < 7; ++n) {
    Segment segment;
    segment.net      = n;
    segment.layer    = 1;
    segment.lo       = n < 6 ? 4 * n : 0;
    segment.hi       = n < 6 ? 4 * n + 1 : 23;
    segment.critical = n < 6;
    f.routes.segments.push_back(segment);
  }
  assign_tracks(f.design, f.grid, f.routes, 1);
  EXPECT_EQ(f.routes.unassigned, 1u);
  EXPECT_EQ(f.routes.l5_repair_attempts, 1u);
  EXPECT_EQ(f.routes.l5_repair_successes, 0u);
  EXPECT_EQ(f.routes.l5_repair_unknown, 1u);
  EXPECT_EQ(f.routes.l5_repair_unsat, 0u);
  for (int n = 0; n < 6; ++n) {
    EXPECT_EQ(f.routes.segments[n].track, 1);
    EXPECT_EQ(f.routes.segments[n].track_state, uint8_t(Track_state::checked));
  }
  EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
}

TEST(LegrouteRoute, SharingDoesNotBypassAHardObstacle) {
  Track_fixture f(true);
  f.grid.obstacles[0].push_back({140, 250, 160, 350});
  f.grid.obstacle_band[0][0].push_back(0);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_GT(f.routes.unassigned, 0u);
  for (const auto& segment : f.routes.segments) {
    EXPECT_EQ(segment.track, -1);
  }
}

TEST(LegrouteRoute, AShorterSharedSpanCannotEraseALongerReservation) {
  Track_fixture f(true);
  f.routes.segments[0].hi      = 2;
  f.routes.segments[0].node_hi = 2;
  f.routes.segments[1].lo      = 0;
  f.routes.segments[1].hi      = 1;
  f.routes.segments[1].node_lo = 0;
  f.routes.segments[1].node_hi = 1;
  f.routes.trees.push_back(f.routes.trees[0]);
  f.design.nets.resize(2);
  auto other    = f.routes.segments[1];
  other.net     = 1;
  other.lo      = 1;
  other.hi      = 2;
  other.node_lo = 1;
  other.node_hi = 2;
  f.routes.segments.push_back(other);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(f.routes.unassigned, 1u);
}

TEST(LegrouteRoute, FixedVerticalArrivalsDefineTheirNodeRisers) {
  Track_fixture f(true);
  for (auto& segment : f.routes.segments) {
    segment.track = 1;
  }
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(f.routes.unassigned, 0u);
  for (const auto& node : f.routes.trees[0].nodes) {
    EXPECT_EQ(node.riser_layer, 0);
    EXPECT_EQ(node.riser_track, 1);
  }
}

TEST(LegrouteRoute, IndependentRisersRetryVerticalLayersBeforeHorizontalEndpointsBind) {
  for (int blocked_layers : {0, 1, 2}) {
    Track_fixture f(true);
    f.options.l5_fallback_shadow = true;
    f.grid.cols                  = 2;
    f.grid.rows                  = 1;
    f.add_upper_layers();
    f.routes.trees[0].nodes.resize(2);
    f.routes.trees[0].nodes[0].tile = 0;
    f.routes.trees[0].nodes[1].tile = 1;
    f.routes.segments.resize(1);
    auto& segment   = f.routes.segments[0];
    segment.layer   = 1;
    segment.lo      = 0;
    segment.hi      = 1;
    segment.node_lo = 0;
    segment.node_hi = 1;
    if (blocked_layers >= 1) {
      f.block_layer(0, {100, 100, 300, 200});
    }
    if (blocked_layers == 2) {
      f.block_layer(2, {100, 100, 300, 200});
    }
    assign_tracks(f.design, f.grid, f.routes, 2);
    const bool ok = blocked_layers < 2;
    EXPECT_EQ(f.routes.unassigned, ok ? 0u : 2u);
    EXPECT_EQ(f.routes.track_failures["riser"], ok ? 0u : 2u);
    for (const auto& node : f.routes.trees[0].nodes) {
      EXPECT_EQ(node.riser_layer, blocked_layers == 1 ? 2 : 0);
      EXPECT_EQ(node.riser_state, track_state(ok));
      EXPECT_EQ(node.riser_track >= 0, ok);
    }
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
    if (ok) {
      const auto geometry = final_planned_geometry(f.design, f.grid, {}, f.routes);
      const auto x0       = f.grid.track_coord(f.routes.trees[0].nodes[0].riser_layer, f.routes.trees[0].nodes[0].riser_track);
      const auto x1       = f.grid.track_coord(f.routes.trees[0].nodes[1].riser_layer, f.routes.trees[0].nodes[1].riser_track);
      bool       found    = false;
      for (const auto& path : geometry.paths[0]) {
        if (path.layer != "M4" || path.steps.size() != 2) {
          continue;
        }
        const auto* a  = std::get_if<physical::Route_point>(&path.steps[0]);
        const auto* b  = std::get_if<physical::Route_point>(&path.steps[1]);
        found         |= a && b && a->x == x0 - 5 && b->x == x1 + 5;
      }
      EXPECT_TRUE(found);  // Materialization uses the selected riser lattice for both H endpoints.
    }
  }
}

TEST(LegrouteRoute, JogsRetryHorizontalLayersAndPreserveTheSelectedLattice) {
  for (int blocked_layers : {0, 1, 2}) {
    Track_fixture f(true);
    f.options.l5_fallback_shadow = true;
    f.grid.tile_w                = 200;
    f.add_upper_layers();
    f.routes.segments[0].track = 1;
    f.routes.segments[1].track = 2;
    if (blocked_layers >= 1) {
      f.block_layer(1, {100, 240, 300, 260});
    }
    if (blocked_layers == 2) {
      f.block_layer(3, {100, 200, 300, 300});
    }
    assign_tracks(f.design, f.grid, f.routes, 2);
    const bool  ok      = blocked_layers < 2;
    const auto& segment = f.routes.segments[1];
    EXPECT_EQ(f.routes.unassigned, ok ? 0u : 1u);
    EXPECT_EQ(f.routes.track_failures["jog"], ok ? 0u : 1u);
    EXPECT_EQ(segment.jog_lo_layer, blocked_layers == 1 ? 3 : 1);
    EXPECT_EQ(segment.jog_lo_state, track_state(ok));
    EXPECT_EQ(segment.jog_lo >= 0, ok);
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
    if (ok) {
      const auto  geometry = final_planned_geometry(f.design, f.grid, {}, f.routes);
      const auto& layer    = f.grid.layers[segment.jog_lo_layer].name;
      const auto  y        = f.grid.track_coord(segment.jog_lo_layer, segment.jog_lo);
      bool        found    = false;
      for (const auto& path : geometry.paths[0]) {
        if (path.layer != layer || path.steps.size() != 2) {
          continue;
        }
        const auto* a  = std::get_if<physical::Route_point>(&path.steps[0]);
        const auto* b  = std::get_if<physical::Route_point>(&path.steps[1]);
        found         |= a && b && a->y == y && b->y == y && std::min(a->x, b->x) == 145 && std::max(a->x, b->x) == 255;
      }
      EXPECT_TRUE(found);
      if (blocked_layers == 1) {
        const std::set<std::tuple<std::string, std::string, int64_t, int64_t>> joins(geometry.joins[0].begin(),
                                                                                     geometry.joins[0].end());
        EXPECT_TRUE(joins.contains({"M3", "M6", 250, y}));  // Arrival to the upper-layer jog.
        EXPECT_TRUE(joins.contains({"M6", "M3", 150, y}));  // Upper-layer jog to the node riser.
      }
    }
  }
}

TEST(LegrouteRoute, IoStubChecksEveryPinSpanAndRetriesHorizontalLayers) {
  for (int blocked_layers : {0, 1, 2}) {
    Track_fixture f(true);
    f.options.l5_fallback_shadow = true;
    f.grid.tile_w                = 500;
    f.add_upper_layers();
    f.routes.segments.resize(1);
    f.routes.segments[0].track = 1;  // The node riser is x=150.
    for (int x : {250, 550}) {
      physical::Terminal terminal;
      terminal.name   = "io" + std::to_string(x);
      terminal.x      = x;
      terminal.y      = 150;
      terminal.shapes = {
          {"M4", {-10, -5, 10, 5}}
      };
      const auto index = int(f.fp.terminals.size());
      f.fp.terminals.push_back(std::move(terminal));
      f.design.nets[0].pins.push_back({.term = index});
      f.routes.trees[0].nodes[0].pins.push_back(index);
    }
    if (blocked_layers >= 1) {
      // Beyond the first pin's span and its end margin: only the second pin exposes this obstruction.
      f.block_layer(1, {450, 145, 560, 155});
    }
    if (blocked_layers == 2) {
      f.block_layer(3, {450, 100, 560, 200});
    }
    assign_tracks(f.design, f.grid, f.routes, 2);
    const bool  ok   = blocked_layers < 2;
    const auto& node = f.routes.trees[0].nodes[0];
    EXPECT_EQ(f.routes.unassigned, ok ? 0u : 1u);
    EXPECT_EQ(f.routes.track_failures["io_stub"], ok ? 0u : 1u);
    EXPECT_EQ(node.stub_layer, blocked_layers == 1 ? 3 : 1);
    EXPECT_EQ(node.stub_state, track_state(ok));
    EXPECT_EQ(node.stub_access_state, track_state(ok));
    EXPECT_EQ(node.stub_track >= 0, ok);
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
    if (ok) {
      const auto geometry = final_planned_geometry(f.design, f.grid, {}, f.routes);
      EXPECT_TRUE(geometry.io_stub_failure[0].empty());
      const auto        y = f.grid.track_coord(node.stub_layer, node.stub_track);
      std::set<int64_t> reached;
      for (const auto& path : geometry.paths[0]) {
        if (path.layer != f.grid.layers[node.stub_layer].name || path.steps.size() != 2) {
          continue;
        }
        const auto* a = std::get_if<physical::Route_point>(&path.steps[0]);
        const auto* b = std::get_if<physical::Route_point>(&path.steps[1]);
        if (a && b && a->y == y && b->y == y && std::min(a->x, b->x) == 145) {
          reached.insert(std::max(a->x, b->x) - 5);
        }
      }
      EXPECT_EQ(reached, (std::set<int64_t>{250, 550}));
      if (blocked_layers == 1) {
        const std::set<std::tuple<std::string, std::string, int64_t, int64_t>> joins(geometry.joins[0].begin(),
                                                                                     geometry.joins[0].end());
        for (int64_t x : {250, 550}) {
          EXPECT_TRUE(joins.contains({"M4", "M3", x, 150}));  // Pin layer to the vertical access piece.
          EXPECT_TRUE(joins.contains({"M3", "M6", x, y}));    // Access piece to the selected upper stub.
        }
        EXPECT_TRUE(joins.contains({"M6", "M3", 150, y}));  // Both upper stubs meet the node riser.
      }
    }
  }
}

TEST(LegrouteRoute, IoStubsJointlyReserveTheirVerticalAccessWithoutMovingPins) {
  for (bool blocked_third : {false, true}) {
    Track_fixture f(false);
    f.options.l5_fallback_shadow = true;
    f.grid.cols = f.grid.rows = 1;
    f.grid.tile_w             = 500;
    f.grid.tile_h             = 400;
    f.grid.layers[0].pitch    = 20;
    f.grid.layers[0].offset   = 10;
    f.grid.obstacle_band[0].resize(1);
    f.grid.obstacle_band[1].resize(1);
    f.tech.layers["M3"].pitch_x = f.tech.layers["M3"].pitch_y = .020;
    f.fp.tracks.push_back({"X", "M3", 10, 60, 20});
    f.fp.tracks.push_back({"Y", "M4", 50, 10, 100});
    const int nets = blocked_third ? 4 : 3;
    f.routes.trees.resize(nets);
    f.design.nets.resize(nets);
    for (auto& tree : f.routes.trees) {
      tree.nodes.clear();
    }
    f.routes.segments.clear();
    Segment background;
    background.net   = 0;
    background.layer = 1;
    background.track = 1;
    f.routes.segments.push_back(background);
    const std::array<int64_t, 3> pin_y{425, 375, 400};
    for (int n = 1; n < nets; ++n) {
      physical::Terminal terminal;
      terminal.name   = "io" + std::to_string(n);
      terminal.x      = 550;
      terminal.y      = pin_y[size_t(n - 1)];
      terminal.shapes = {
          {"M3", {-5, -5, 5, 5}}
      };
      const int pin = int(f.fp.terminals.size());
      f.fp.terminals.push_back(terminal);
      f.design.nets[n].pins.push_back({.term = pin});
      f.routes.trees[n].nodes.resize(1);
      f.routes.trees[n].nodes[0].tile = 0;
      f.routes.trees[n].nodes[0].pins = {0};
      Segment arrival;
      arrival.net     = n;
      arrival.layer   = 0;
      arrival.track   = 6 + (n - 1) * 2;
      arrival.node_lo = 0;
      f.routes.segments.push_back(arrival);
    }
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.unassigned, blocked_third ? 1u : 0u);
    EXPECT_EQ(f.routes.track_failures["io_stub"], blocked_third ? 1u : 0u);
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
    EXPECT_EQ(f.routes.trees[1].nodes[0].stub_track, 4);  // Short stem 425..450.
    EXPECT_EQ(f.routes.trees[2].nodes[0].stub_track, 3);  // Short stem 350..375, disjoint from the first one.
    for (int n = 1; n < 3; ++n) {
      EXPECT_EQ(f.routes.trees[n].nodes[0].stub_state, uint8_t(Track_state::checked));
      EXPECT_EQ(f.routes.trees[n].nodes[0].stub_access_state, uint8_t(Track_state::checked));
      EXPECT_EQ(f.fp.terminals[size_t(n - 1)].x, 550);
      EXPECT_EQ(f.fp.terminals[size_t(n - 1)].y, pin_y[size_t(n - 1)]);
    }
    if (blocked_third) {
      EXPECT_EQ(f.routes.trees[3].nodes[0].stub_state, uint8_t(Track_state::fallback));
      EXPECT_EQ(f.routes.trees[3].nodes[0].stub_access_state, uint8_t(Track_state::fallback));
    }
    const auto geometry = final_planned_geometry(f.design, f.grid, {}, f.routes);
    if (blocked_third) {
      EXPECT_NE(geometry.io_stub_failure[3].find("unchecked fallback"), std::string::npos);
    }
    std::vector<physical::Shape> stems;
    for (int n = 1; n < 3; ++n) {
      physical::Net net;
      net.wires.push_back({"ROUTED", geometry.paths[n]});
      for (const auto& shape : physical::route_geometry(f.tech, f.fp, net)) {
        if (shape.layer == "M3" && shape.rect.xl == 545 && shape.rect.xh == 555) {
          stems.push_back(shape);
        }
      }
      bool checked_stem = false;
      for (size_t k = 0; k < geometry.paths[n].size(); ++k) {
        const auto& path = geometry.paths[n][k];
        if (path.layer == "M3" && !path.steps.empty()) {
          const auto* point = std::get_if<physical::Route_point>(&path.steps[0]);
          if (point && point->x == 550) {
            EXPECT_EQ(geometry.path_state[n][k], uint8_t(Track_state::checked));
            checked_stem = true;
          }
        }
      }
      EXPECT_TRUE(checked_stem);
    }
    ASSERT_EQ(stems.size(), 2u);
    const physical::Signal_metal_rules rules(f.tech, f.fp);
    EXPECT_TRUE(rules.spacing(stems[0], stems[1]).legal);
  }
}

TEST(LegrouteRoute, LateIoRepairChecksSettledMetalInsteadOfTheEarlierWholeTileRiserEnvelope) {
  for (int failure : {0, 1, 2}) {
    const bool             real_overlap = failure == 1, incomplete_scene = failure == 2;
    std::optional<int64_t> proposal;
    for (unsigned repair_ms : {0u, 2000u}) {
      Track_fixture f(false);
      f.options.l5_repair_ms       = repair_ms;
      f.options.l5_fallback_shadow = true;
      f.grid.cols = f.grid.rows = 2;
      f.grid.tile_w             = 500;
      f.grid.tile_h             = 400;
      f.grid.die = f.fp.die   = {0, 0, 1200, 1000};
      f.grid.layers[0].pitch  = 20;
      f.grid.layers[0].offset = 10;
      f.grid.obstacle_band[0].resize(2);
      f.grid.obstacle_band[1].resize(2);
      f.tech.layers["M3"].pitch_x = f.tech.layers["M3"].pitch_y = .020;
      f.fp.tracks.push_back({"X", "M3", 10, 60, 20});
      f.fp.tracks.push_back({"Y", "M4", 50, 10, 100});
      f.fp.nets.resize(2);
      for (int n = 0; n < 2; ++n) {
        f.fp.nets[n].name       = "data" + std::to_string(n);
        f.design.nets[n].frame  = size_t(n);
        f.design.nets[n].routed = true;
        physical::Terminal pin;
        pin.name   = "io" + std::to_string(n);
        pin.net    = f.fp.nets[n].name;
        pin.x      = 550;
        pin.y      = n ? 510 : 150;
        pin.status = incomplete_scene ? physical::Status::UNPLACED : physical::Status::FIXED;
        pin.shapes = {
            {"M3", {-5, -5, 5, 5}}
        };
        f.fp.terminals.push_back(pin);
        f.design.nets[n].pins.push_back({.term = n});
        f.routes.trees[n].nodes.clear();
      }
      auto& first = f.routes.trees[0];
      first.nodes.resize(2);
      first.nodes[0].tile = 0;
      first.nodes[0].pins = {0};
      first.nodes[1].tile = 1;
      auto& second        = f.routes.trees[1];
      second.nodes.resize(1);
      second.nodes[0].tile = 2;
      second.nodes[0].pins = {0};
      f.routes.segments.clear();
      Segment h;
      h.net     = 0;
      h.layer   = 1;
      h.lo      = 0;
      h.hi      = 1;
      h.node_lo = 0;
      h.node_hi = 1;
      f.routes.segments.push_back(h);
      Segment arrival;
      arrival.net   = 1;
      arrival.layer = 0;
      arrival.lo = arrival.hi = 1;
      arrival.node_lo         = 0;
      arrival.track           = 17;  // x350; the target's terminal access is at x550.
      f.routes.segments.push_back(arrival);
      if (real_overlap) {
        Segment blocker;
        blocker.net     = 0;
        blocker.layer   = 0;
        blocker.lo      = 0;
        blocker.hi      = 1;
        blocker.node_lo = 0;
        blocker.track   = 27;  // x550, with real metal extending into the target's access stem.
        f.routes.segments.push_back(blocker);
      }
      assign_tracks(f.design, f.grid, f.routes, 1);
      SCOPED_TRACE(std::format("failure={} repair_ms={} telemetry={} samples={}",
                               failure,
                               repair_ms,
                               testing::PrintToString(f.routes.track_fallback_shadow),
                               testing::PrintToString(f.routes.track_failure_samples)));
      const auto& node      = second.nodes[0];
      const bool  recovered = repair_ms && !real_overlap && !incomplete_scene;
      EXPECT_EQ(f.routes.unassigned, recovered ? 0u : 1u);
      EXPECT_EQ(f.routes.track_failures["io_stub"], recovered ? 0u : 1u);
      EXPECT_EQ(node.stub_state, track_state(recovered));
      EXPECT_EQ(node.stub_access_state, track_state(recovered));
      EXPECT_EQ(node.stub_layer, 1);
      EXPECT_EQ(node.stub_track, 5);  // The late transaction checks this exact earlier proposal; it never moves the pin/stub.
      if (proposal) {
        EXPECT_EQ(node.stub_track, *proposal);
      } else {
        proposal = node.stub_track;
      }
      EXPECT_EQ(f.fp.terminals[1].x, 550);
      EXPECT_EQ(f.fp.terminals[1].y, 510);
      EXPECT_EQ(f.routes.track_fallback_shadow["repair:io_exact_wire_only"], recovered ? 1u : 0u);
      EXPECT_EQ(f.routes.track_fallback_shadow["repair:io_exact_conflicts"], repair_ms && real_overlap ? 1u : 0u);
      EXPECT_EQ(f.routes.track_fallback_shadow["repair:io_exact_unknown"], repair_ms && incomplete_scene ? 1u : 0u);
      if (repair_ms && incomplete_scene) {
        EXPECT_TRUE(std::ranges::any_of(f.routes.track_failure_samples, [](const auto& sample) {
          return sample.find("IO exact recheck unknown: unplaced terminal in routing occupancy") != std::string::npos;
        }));
      }
      EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
      const auto geometry = final_planned_geometry(f.design, f.grid, {}, f.routes);
      EXPECT_EQ(geometry.io_stub_failure[1].empty(), recovered);
      EXPECT_TRUE(f.fp.nets[0].wires.empty());
      EXPECT_TRUE(f.fp.nets[1].wires.empty());
      physical::Net background;
      background.wires.push_back({"ROUTED", geometry.paths[0]});
      const auto stem = planned_wire_shape(planned_wire_geometry(f.tech, f.fp.dbu, f.fp.die, "M3"), false, 510, 550, 550);
      const physical::Signal_metal_rules rules(f.tech, f.fp);
      bool                               actual_conflict = false;
      for (const auto& shape : physical::route_geometry(f.tech, f.fp, background)) {
        if (shape.layer == "M3") {
          actual_conflict |= !rules.spacing(stem, shape).legal;
        }
      }
      EXPECT_EQ(actual_conflict, real_overlap);
      if (recovered) {
        EXPECT_TRUE(f.routes.track_failure_samples.empty());
      } else {
        EXPECT_NE(geometry.io_stub_failure[1].find("unchecked fallback"), std::string::npos);
      }
      bool saw_stem = false;
      for (size_t k = 0; k < geometry.paths[1].size(); ++k) {
        const auto& path = geometry.paths[1][k];
        const auto* a    = std::get_if<physical::Route_point>(&path.steps.front());
        const auto* b    = std::get_if<physical::Route_point>(&path.steps.back());
        if (path.layer == "M3" && a && b && a->x == 550) {
          saw_stem = true;
          EXPECT_EQ(std::min(a->y, b->y), 505);
          EXPECT_EQ(std::max(a->y, b->y), 555);
          EXPECT_EQ(geometry.path_state[1][k], track_state(recovered));
        }
      }
      EXPECT_TRUE(saw_stem);
    }
  }
}

TEST(LegrouteRoute, JointIoAccessReservesTheTerminalsOwnVerticalLayer) {
  Track_fixture f(true);
  f.grid.tile_w = 500;
  f.add_upper_layers();
  f.routes.segments.resize(1);
  f.routes.segments[0].track = 1;
  physical::Terminal terminal;
  terminal.name   = "upper_io";
  terminal.x      = 270;
  terminal.y      = 170;
  terminal.shapes = {
      {"M5", {-5, -5, 5, 5}}
  };
  f.fp.terminals.push_back(terminal);
  f.design.nets[0].pins.push_back({.term = 0});
  f.routes.trees[0].nodes[0].pins = {0};
  assign_tracks(f.design, f.grid, f.routes, 1);
  EXPECT_EQ(f.routes.unassigned, 0u);
  const auto& node = f.routes.trees[0].nodes[0];
  EXPECT_EQ(node.stub_access_state, uint8_t(Track_state::checked));
  const auto geometry = final_planned_geometry(f.design, f.grid, {}, f.routes);
  bool       found    = false;
  for (size_t k = 0; k < geometry.paths[0].size(); ++k) {
    const auto& path = geometry.paths[0][k];
    if (path.layer != "M5") {
      continue;
    }
    const auto* a = std::get_if<physical::Route_point>(&path.steps.front());
    const auto* b = std::get_if<physical::Route_point>(&path.steps.back());
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->x, 270);
    EXPECT_EQ(b->x, 270);
    EXPECT_EQ(std::min(a->y, b->y), 145);
    EXPECT_EQ(std::max(a->y, b->y), 175);
    EXPECT_EQ(geometry.path_state[0][k], uint8_t(Track_state::checked));
    found = true;
  }
  EXPECT_TRUE(found);
}

TEST(LegrouteRoute, JointIoAccessAccountsForAllForeignColumnsBeforeCommitting) {
  for (unsigned reserve : {1u, 2u}) {
    Track_fixture f(true);
    f.options.l5_fallback_shadow = true;
    f.options.l5_repair_ms       = 2000;  // Late repair cannot admit a proposal that never preserved the protected columns.
    f.grid.cols = f.grid.rows = 1;
    f.grid.tile_w             = 300;
    f.grid.tile_h             = 200;
    f.grid.obstacle_band[0].resize(1);
    f.grid.obstacle_band[1].resize(1);
    f.grid.resources.layers.resize(3);
    f.grid.cap.resize(2);
    for (int l = 0; l < 2; ++l) {
      auto& resource      = f.grid.resources.layers[size_t(l)];
      resource.name       = f.grid.layers[l].name;
      resource.horizontal = f.grid.layers[l].horizontal;
      resource.grid_layer = l;
      resource.bands.resize(1);
      const auto [lo, hi] = f.grid.tracks(l, 0);
      for (auto track = lo; track <= hi; ++track) {
        resource.bands[0].push_back(f.grid.track_coord(l, track));
      }
      resource.tiles.resize(1);
      resource.tiles[0].coarse_capacity = int(resource.bands[0].size());
      f.grid.cap[l]                     = {int16_t(resource.bands[0].size())};
    }
    f.grid.resources.layers[2].name  = "M2";
    f.grid.resources.layers[2].bands = {
        {150, 250}
    };
    f.grid.resources.layers[2].tiles.resize(1);
    f.paired_budget(0, reserve, {0});
    f.routes.trees[0].nodes.resize(1);
    auto& node = f.routes.trees[0].nodes[0];
    node.tile  = 0;
    node.box   = 0;
    f.routes.segments.resize(1);
    auto& riser = f.routes.segments[0];
    riser.lo = riser.hi = 0;
    riser.node_lo       = 0;
    riser.node_hi       = -1;
    riser.track         = 1;  // Own-box destination at x150 consumes none of the foreign allowance.
    f.fp.tracks.push_back({"X", "M3", 50, 10, 100});
    f.fp.tracks.push_back({"Y", "M4", 50, 10, 100});
    for (int x : {250, 350}) {
      physical::Terminal terminal;
      terminal.name   = "io" + std::to_string(x);
      terminal.x      = x;
      terminal.y      = 200;  // Between H tracks: both pins necessarily require a V access stem.
      terminal.shapes = {
          {"M3", {-5, -5, 5, 5}}
      };
      const int pin = int(f.fp.terminals.size());
      f.fp.terminals.push_back(terminal);
      f.design.nets[0].pins.push_back({.term = pin});
      node.pins.push_back(pin);
    }
    assign_tracks(f.design, f.grid, f.routes, 1);
    const bool fits = reserve == 1;
    EXPECT_EQ(f.routes.unassigned, fits ? 0u : 1u);
    EXPECT_EQ(node.stub_state, track_state(fits));
    EXPECT_EQ(node.stub_access_state, track_state(fits));
    EXPECT_EQ(f.routes.paired_allowance_short_tracks, 0u);
    EXPECT_EQ(f.routes.track_fallback_shadow["ledger:mismatched"], 0u);
    if (!fits) {
      EXPECT_EQ(node.stub_track, -1);  // No partial pair or fallback is allowed to spend the protected column.
    }
  }
}

TEST(LegrouteRoute, MissingTerminalTrackIsAnIoStubFailureWithoutAClaimedCheckedSpan) {
  Track_fixture f(true);
  f.add_upper_layers();
  f.routes.segments.resize(1);
  f.routes.segments[0].track = 1;
  physical::Terminal terminal;
  terminal.x      = 125;
  terminal.y      = 150;
  terminal.shapes = {
      {"M4", {-5, -5, 5, 5}}
  };  // No M3 preferred track lies in x=120..130.
  f.fp.terminals.push_back(std::move(terminal));
  f.design.nets[0].pins.push_back({.term = 0});
  f.routes.trees[0].nodes[0].pins.push_back(0);
  assign_tracks(f.design, f.grid, f.routes, 1);
  const auto& node = f.routes.trees[0].nodes[0];
  EXPECT_EQ(f.routes.unassigned, 1u);
  EXPECT_EQ(f.routes.track_failures["io_stub"], 1u);
  EXPECT_EQ(node.stub_layer, -1);
  EXPECT_EQ(node.stub_track, -1);
  EXPECT_EQ(node.stub_state, uint8_t(Track_state::fallback));
}

TEST(LegrouteRoute, VerticalTerminalAccessUsesItsOwnFiniteTrackAndMaterializedLayer) {
  Track_fixture f(true);
  f.add_upper_layers();
  f.routes.segments.clear();
  f.routes.trees[0].nodes.resize(1);
  auto& node       = f.routes.trees[0].nodes[0];
  node.riser_layer = 0;
  node.riser_track = 1;  // M3 x=150.
  node.stub_layer  = 1;
  node.stub_track  = 1;  // M4 y=150.
  physical::Terminal terminal;
  terminal.x      = 270;
  terminal.y      = 160;
  terminal.shapes = {
      {"M5", {-5, -20, 5, 20}}
  };  // M5 x=270 lies inside; neighboring M3 x=250 and x=350 do not.
  f.fp.terminals.push_back(terminal);
  f.design.nets[0].pins.push_back({.term = 0});
  node.pins.push_back(0);

  const auto access = planned_terminal_access(f.design, f.grid, terminal);
  ASSERT_TRUE(access);
  EXPECT_EQ(access->access_layer, "M5");
  EXPECT_EQ(access->x, 270);
  EXPECT_EQ(access->y, 160);
  const auto geometry = final_planned_geometry(f.design, f.grid, {}, f.routes);
  ASSERT_TRUE(geometry.io_stub_failure[0].empty());
  bool vertical_access_found = false;
  for (const auto& path : geometry.paths[0]) {
    if (path.layer != "M5" || path.steps.size() != 2) {
      continue;
    }
    const auto* a          = std::get_if<physical::Route_point>(&path.steps[0]);
    const auto* b          = std::get_if<physical::Route_point>(&path.steps[1]);
    vertical_access_found |= a && b && a->x == 270 && b->x == 270 && std::min(a->y, b->y) == 145 && std::max(a->y, b->y) == 165;
  }
  EXPECT_TRUE(vertical_access_found);
  const std::set<std::tuple<std::string, std::string, int64_t, int64_t>> joins(geometry.joins[0].begin(), geometry.joins[0].end());
  EXPECT_EQ(joins,
            (std::set<std::tuple<std::string, std::string, int64_t, int64_t>>{
                {"M5", "M4", 270, 150},
                {"M4", "M3", 150, 150}
  }));

  auto horizontal            = terminal;
  horizontal.shapes[0].layer = "M4";
  EXPECT_FALSE(planned_terminal_access(f.design, f.grid, horizontal));  // H access retains its lowest-V requirement.
  for (auto& track : f.fp.tracks) {
    if (track.layer == "M5" && track.axis == "X") {
      track.count = 3;  // Only x=30,110,190 exist: do not extend the finite pattern to x=270.
    }
  }
  EXPECT_FALSE(planned_terminal_access(f.design, f.grid, terminal));
  const auto missing = final_planned_geometry(f.design, f.grid, {}, f.routes);
  EXPECT_FALSE(missing.io_stub_failure[0].empty());
  EXPECT_TRUE(missing.paths[0].empty());
  EXPECT_TRUE(missing.joins[0].empty());
}

TEST(LegrouteRoute, WideWireQueryRadiusDoesNotRejectLegalThinParallelWires) {
  Track_fixture f(false);
  f.grid.tile_w                      = 200;  // Two tracks, 100 DBU apart, for ten-DBU wires.
  f.tech.layers["M3"].rules.parallel = {
      {{0, .1}, {0}, {{.01}, {.25}}}
  };
  const physical::Signal_metal_rules rules(f.tech, f.fp);
  ASSERT_EQ(rules.clearance("M3"), 250);
  ASSERT_TRUE(rules
                  .spacing(
                      {
                          "M3",
                          {145, 90, 155, 310}
  },
                      {"M3", {245, 190, 255, 410}})
                  .legal);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_EQ(f.routes.unassigned, 0u);
  EXPECT_NE(f.routes.segments[0].track, f.routes.segments[1].track);
}

TEST(LegrouteRoute, EqualParallelEndsStillRespectEndOfLineKeepouts) {
  Track_fixture f(false);
  f.grid.tile_w                                            = 200;
  f.routes.segments[1].lo                                  = f.routes.segments[0].lo;
  f.routes.segments[1].hi                                  = f.routes.segments[0].hi;
  f.tech.layers["M3"].rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .020 EXTENSION .020 .100 .020 CORNERONLY ;"};
  const physical::Signal_metal_rules rules(f.tech, f.fp);
  ASSERT_FALSE(rules
                   .spacing(
                       {
                           "M3",
                           {145, 90, 155, 310}
  },
                       {"M3", {245, 90, 255, 310}})
                   .legal);
  assign_tracks(f.design, f.grid, f.routes, 2);
  EXPECT_GT(f.routes.unassigned, 0u);
}
TEST(LegrouteRoute, FallbackShadowSeparatesFallbackBlockersAndKeepsAssignment) {
  // One M3 track over five rows. Clock X (net 0, row 0) is checked; clock Y (net 1, rows 0..3) overlaps it and lands
  // on the track as FALLBACK; signal Z (net 2, row 4) is then blocked only by Y's fallback metal.
  const auto build = [](Track_fixture& f, bool shadow) {
    f.options.l5_fallback_shadow = shadow;
    f.grid.rows                  = 5;
    f.grid.obstacle_band[1].resize(5);
    f.routes.trees.resize(3);
    f.design.nets.resize(3);
    for (auto& tree : f.routes.trees) {
      tree.nodes.resize(5);
      for (int i = 0; i < 5; ++i) {
        tree.nodes[i].tile = i;
      }
    }
    f.routes.segments.clear();
    const auto add = [&](int net, int lo, int hi, bool clock) {
      Segment s;
      s.net = net, s.layer = 0, s.band = 0, s.lo = lo, s.hi = hi, s.node_lo = lo, s.node_hi = hi, s.clock = clock;
      f.routes.segments.push_back(s);
    };
    add(0, 0, 0, true);
    add(1, 0, 3, true);
    add(2, 4, 4, false);
  };
  Track_fixture off(false), on(false);
  build(off, false);
  build(on, true);
  assign_tracks(off.design, off.grid, off.routes, 1);
  assign_tracks(on.design, on.grid, on.routes, 1);
  ASSERT_EQ(on.routes.segments.size(), off.routes.segments.size());
  for (size_t i = 0; i < on.routes.segments.size(); ++i) {  // the diagnostic never changes the assignment
    EXPECT_EQ(on.routes.segments[i].track, off.routes.segments[i].track);
    EXPECT_EQ(on.routes.segments[i].track_state, off.routes.segments[i].track_state);
  }
  EXPECT_EQ(on.routes.unassigned, off.routes.unassigned);
  EXPECT_TRUE(off.routes.track_fallback_shadow.empty());
  EXPECT_EQ(on.routes.segments[0].track_state, uint8_t(Track_state::checked));
  EXPECT_EQ(on.routes.segments[1].track_state, uint8_t(Track_state::fallback));
  auto& c = on.routes.track_fallback_shadow;
  EXPECT_EQ(c["ledger:mismatched"], 0u);
  EXPECT_GT(c["ledger:bands"], 0u);
  EXPECT_EQ(c["M3:fixed:failed"], 1u);  // Y is blocked by checked X: the shadow keeps X
  EXPECT_EQ(c["M3:fixed:shadow_free"], 0u);
  EXPECT_EQ(c["M3:segment_first:failed"], 1u);  // Z is blocked only by Y's fallback
  EXPECT_EQ(c["M3:segment_first:shadow_free"], 1u);
  EXPECT_EQ(c["M3:segment_final:failed"], 1u);
  EXPECT_EQ(c["M3:segment_final:shadow_free"], 1u);
}
TEST(LegrouteRoute, FallbackShadowKeepsTheCheckedSpanOfASameNetFallback) {
  // One M3 track over four rows. Net 0 has checked clock X (row 0) and later FALLBACK clock Y (rows 0..3, blocked by
  // net 1's checked W on row 3); the real track merges X and Y into one net-0 owner span. The shadow must still
  // hold X, so signal Z (net 2, row 0) stays blocked there; dropping the merged owner would wrongly free it.
  Track_fixture f(false);
  f.options.l5_fallback_shadow = true;
  f.grid.rows                  = 4;
  f.grid.obstacle_band[1].resize(4);
  f.routes.trees.resize(3);
  f.design.nets.resize(3);
  for (auto& tree : f.routes.trees) {
    tree.nodes.resize(4);
    for (int i = 0; i < 4; ++i) {
      tree.nodes[i].tile = i;
    }
  }
  f.routes.segments.clear();
  const auto add = [&](int net, int lo, int hi, bool clock) {
    Segment s;
    s.net = net, s.layer = 0, s.band = 0, s.lo = lo, s.hi = hi, s.node_lo = lo, s.node_hi = hi, s.clock = clock;
    f.routes.segments.push_back(s);
  };
  add(0, 0, 0, true);   // X
  add(1, 3, 3, true);   // W
  add(0, 0, 3, true);   // Y
  add(2, 0, 0, false);  // Z
  assign_tracks(f.design, f.grid, f.routes, 1);
  EXPECT_EQ(f.routes.segments[0].track_state, uint8_t(Track_state::checked));
  EXPECT_EQ(f.routes.segments[1].track_state, uint8_t(Track_state::checked));
  EXPECT_EQ(f.routes.segments[2].track_state, uint8_t(Track_state::fallback));
  auto& c = f.routes.track_fallback_shadow;
  EXPECT_EQ(c["ledger:mismatched"], 0u);
  EXPECT_EQ(c["M3:fixed:failed"], 1u);
  EXPECT_EQ(c["M3:fixed:shadow_free"], 0u);
  EXPECT_EQ(c["M3:segment_first:failed"], 1u);
  EXPECT_EQ(c["M3:segment_first:shadow_free"], 0u);  // X's checked span survives Y's fallback merge
  // the sampled blocker of Z names net 0's span and both of its contributions' provenance, checked first
  ASSERT_EQ(f.routes.track_failure_samples.size(), 1u);
  const auto& why = f.routes.track_failure_samples[0];
  EXPECT_NE(why.find("M3 segment_final net 2"), std::string::npos) << why;
  EXPECT_NE(why.find("span 0 [100,200] checked"), std::string::npos) << why;
}
TEST(LegrouteRoute, RuleReachLimitsTheEndEnvelopeToOffsetsTheExactRulesCanViolate) {
  // A keepout with a long forward reach (query radius 260 DBU) but a 20-DBU side extension: the envelope scans the
  // adjacent track (90-DBU gap), yet no nominal pair there is illegal. With l5_rule_reach the measured reach is 0 and
  // two equal-span nets share the two tracks; by default the second one stays unassigned.
  for (bool reach : {false, true}) {
    Track_fixture f(false);
    f.options.l5_rule_reach                                  = reach;
    f.grid.tile_w                                            = 200;
    f.routes.segments[1].lo                                  = f.routes.segments[0].lo;
    f.routes.segments[1].hi                                  = f.routes.segments[0].hi;
    f.tech.layers["M3"].rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .020 EXTENSION .020 .020 .200 CORNERONLY ;"};
    const physical::Signal_metal_rules rules(f.tech, f.fp);
    ASSERT_EQ(rules.clearance("M3"), 260);
    assign_tracks(f.design, f.grid, f.routes, 1);
    if (reach) {
      EXPECT_EQ(f.routes.track_fallback_shadow["rule_reach:M3"], 0u);
      EXPECT_EQ(f.routes.unassigned, 0u);
      EXPECT_NE(f.routes.segments[0].track, f.routes.segments[1].track);
    } else {
      EXPECT_GT(f.routes.unassigned, 0u);
    }
  }
}
TEST(LegrouteRoute, CompactPackingAvoidsUnusableGapsWithoutRelaxingEndOfLineSpacing) {
  // Nine tracks can hold five equal-span nets with one empty track between neighbors. Preferring separation
  // three leaves only tracks 5, 2, and 8 occupied and strands the remaining holes. Six nets still cannot fit.
  for (int wires : {5, 6}) {
    SCOPED_TRACE(wires);
    Track_fixture f(false);
    f.options.l5_rule_reach = true;
    f.grid.tile_w           = 900;
    f.grid.die = f.fp.die                                    = {0, 0, 2000, 1000};
    f.tech.layers["M3"].rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .020 EXTENSION .020 .120 .200 CORNERONLY ;"};
    f.design.nets.resize(wires);
    f.routes.trees.resize(wires);
    f.routes.segments.clear();
    for (int n = 0; n < wires; ++n) {
      auto& tree = f.routes.trees[n];
      tree.nodes.resize(2);
      tree.nodes[0].tile = 0;
      tree.nodes[1].tile = 1;
      Segment s;
      s.net     = n;
      s.layer   = 0;
      s.lo      = 0;
      s.hi      = 1;
      s.node_lo = 0;
      s.node_hi = 1;
      f.routes.segments.push_back(s);
    }
    const auto [first, last] = f.grid.tracks(0, 0);
    ASSERT_EQ(first, 1);
    ASSERT_EQ(last, 9);
    const physical::Signal_metal_rules rules(f.tech, f.fp);
    const auto                         metal = [&](int64_t track) {
      const auto x     = f.grid.track_coord(0, track);
      const auto width = f.grid.layers[0].width;
      return physical::Shape{
          "M3",
          {x - width / 2., double(f.grid.bottom(0) - width), x + width / 2., double(f.grid.bottom(1) + f.grid.tile_h + width)}
      };
    };
    ASSERT_EQ(rules.lateral_reach("M3"), 120);
    ASSERT_FALSE(rules.spacing(metal(first), metal(first + 1)).legal);
    ASSERT_TRUE(rules.spacing(metal(first), metal(first + 2)).legal);
    assign_tracks(f.design, f.grid, f.routes, 1);
    EXPECT_EQ(f.routes.track_fallback_shadow["rule_reach:M3"], 1u);
    EXPECT_EQ(f.routes.unassigned, size_t(wires - 5));
    std::set<int64_t> occupied;
    for (int n = 0; n < 5; ++n) {
      const auto& segment = f.routes.segments[n];
      ASSERT_EQ(segment.track_state, uint8_t(Track_state::checked));
      ASSERT_GE(segment.track, first);
      ASSERT_LE(segment.track, last);
      EXPECT_TRUE(occupied.insert(segment.track).second);
      for (int other = 0; other < n; ++other) {
        EXPECT_TRUE(rules.spacing(metal(segment.track), metal(f.routes.segments[other].track)).legal);
      }
    }
    if (wires == 6) {
      EXPECT_EQ(f.routes.segments.back().track_state, uint8_t(Track_state::fallback));
    }
  }
}
TEST(LegrouteRoute, RuleReachKeepsALongParallelRunSpacingThatShortSamplesMiss) {
  // Advisor counterexample: a PRL row requiring 120 DBU once two wires run 2000 DBU side by side. The sampled
  // lengths never overlap that far, but the analytic lateral reach (120) keeps the adjacent track (gap 90): two
  // equal 2000+ DBU spans cannot share the two tracks.
  Track_fixture f(false);
  f.options.l5_rule_reach            = true;
  f.grid.tile_w                      = 200;
  f.grid.tile_h                      = 3000;
  f.routes.segments[1].lo            = f.routes.segments[0].lo;
  f.routes.segments[1].hi            = f.routes.segments[0].hi;
  f.tech.layers["M3"].rules.parallel = {
      {{0}, {0, 2}, {{.01, .12}}}
  };
  f.tech.layers["M3"].rules.properties["LEF58_RECTONLY"] = {"RECTONLY ;"};
  const physical::Signal_metal_rules rules(f.tech, f.fp);
  ASSERT_TRUE(rules.clearance("M3"));
  EXPECT_EQ(rules.lateral_reach("M3"), 120);
  ASSERT_FALSE(rules
                   .spacing(
                       {
                           "M3",
                           {145, 0, 155, 2100}
  },
                       {"M3", {245, 0, 255, 2100}})
                   .legal);
  assign_tracks(f.design, f.grid, f.routes, 1);
  EXPECT_EQ(f.routes.track_fallback_shadow["rule_reach:M3"], 1u);
  EXPECT_GT(f.routes.unassigned, 0u);
}
}  // namespace livehd::placement::lr
