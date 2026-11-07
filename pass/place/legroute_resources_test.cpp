// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <set>
#include <tuple>

#include "gtest/gtest.h"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
namespace ph = livehd::physical;
struct Fixture {
  ph::Technology   tech;
  ph::Floorplan    fp;
  timing::Feedback timing;
  Legroute_options options;

  explicit Fixture(int64_t origin = 0) {
    tech.dbu = fp.dbu       = 1000;
    tech.manufacturing_grid = .001;
    tech.sites["site"]      = {.05, .25, true};
    for (int k = 1; k <= 6; ++k) {
      const auto name  = "M" + std::to_string(k);
      auto&      layer = tech.layers[name];
      layer.width      = .02;
      layer.direction  = k % 2 ? "VERTICAL" : "HORIZONTAL";
      layer.pitch_x = layer.pitch_y = .05;
      layer.offset_x = layer.offset_y = .025;
      layer.has_offset                = true;
      layer.rules.loaded              = true;
      layer.rules.spacing             = {
          {.02, {}, {}}
      };
      tech.routing_layers.push_back(name);
      fp.tracks.push_back({k % 2 ? "X" : "Y", name, 25 + (k % 2 ? 0 : origin), k % 2 ? 64 : 40, 50});
      if (k > 1) {
        const auto cut             = "V" + std::to_string(k - 1);
        tech.cut_layers[cut].width = .02;
        auto& via                  = tech.vias[cut + "_DEF"];
        via.is_default             = true;
        via.shapes                 = {
            {"M" + std::to_string(k - 1), {-.02, -.02, .02, .02}},
            {                        cut, {-.01, -.01, .01, .01}},
            {                       name, {-.02, -.02, .02, .02}}
        };
      }
    }
    auto& master       = tech.macros["dff"];
    master.width       = .4;
    master.height      = .25;
    master.klass       = "CORE";
    master.site        = "site";
    master.pins["CLK"] = {"INPUT", "CLOCK", {{"M1", {.12, .05, .14, .2}}}};
    Ann_place pose(0, double(origin), 400, 250);
    pose.status = ph::Status::PLACED;
    fp.components.push_back({"ff", "dff", pose});
    fp.die = {0, double(origin), 3200, double(origin + 2000)};
    for (int r = 0; r < 8; ++r) {
      fp.rows.push_back(
          {"r" + std::to_string(r), "site", 0, origin + r * 250, 64, 50, r % 2 ? ph::Orientation::FS : ph::Orientation::N});
    }
  }

  Grid grid(const Legroute_regions& regions = {}) {
    const auto design = build_design(tech, fp, timing, options, nullptr);
    return build_grid(design, regions);
  }
};

const Track_resource_layer& layer(const Grid& grid, std::string_view name) {
  const auto it = std::ranges::find(grid.resources.layers, name, &Track_resource_layer::name);
  if (it == grid.resources.layers.end()) {
    throw std::logic_error("fixture resource layer missing");
  }
  return *it;
}

uint8_t reasons_at(const Track_resource_layer& resource, int tile, int64_t coordinate) {
  const auto& entry  = resource.tiles.at(size_t(tile));
  const auto& tracks = resource.bands.at(entry.band);
  const auto  it     = std::lower_bound(tracks.begin(), tracks.end(), coordinate);
  if (it == tracks.end() || *it != coordinate) {
    throw std::logic_error("fixture physical track missing");
  }
  const auto index     = size_t(it - tracks.begin());
  const auto exclusion = std::ranges::find(entry.exclusions, index, &Track_resource_exclusion::track);
  return entry.whole_band_exclusions | (exclusion == entry.exclusions.end() ? 0 : exclusion->reasons);
}

void expect_capacity_parity(const Grid& grid) {
  ASSERT_EQ(grid.resources.layers.size(), grid.layers.size() + 1);
  for (const auto& resource : grid.resources.layers) {
    ASSERT_EQ(resource.tiles.size(), grid.size());
    for (size_t t = 0; t < grid.size(); ++t) {
      const int expected = resource.grid_layer < 0 ? grid.box_cap[t] : grid.cap[size_t(resource.grid_layer)][t];
      EXPECT_EQ(resource.tiles[t].coarse_capacity, expected);
      EXPECT_EQ(resource.represented_capacity(t), expected);
    }
  }
}

void paired_design(Fixture& f) {
  f.options.paired_pin_reserve = f.options.legal_boxes = true;
  f.options.final_skip_clock                           = true;
  f.options.clock_spine                                = false;
  f.options.reserved_tracks                            = 0;
  f.options.box_reserve = "M2:9,M3:9";  // The opt-in paired rule replaces, rather than adds to, uniform reservations.
  auto& master          = f.tech.macros.at("dff");
  for (const auto* name : {"D0", "D1", "Q0", "Q1"}) {
    master.pins[name] = {name[0] == 'Q' ? "OUTPUT" : "INPUT", "SIGNAL", {{"M1", {.04, .05, .06, .2}}}};
  }
  for (const auto& [name, x, y] : {
           std::tuple{"ff1", 400, 250},
           std::tuple{"ff2", 800,   0}
  }) {
    Ann_place pose(x, y, 400, 250);
    pose.status      = ph::Status::PLACED;
    pose.orientation = y ? ph::Orientation::FS : ph::Orientation::N;
    f.fp.components.push_back({name, "dff", pose});
  }
  f.fp.terminals.push_back(
      {"io", "external_io", "INPUT", 25, 525, ph::Orientation::N, ph::Status::FIXED, {{"M4", {-10, -10, 10, 10}}}});
  f.fp.nets.push_back({
      "external_many",
      {{"ff", "Q0"}, {"ff", "D0"}, {"ff1", "D0"}, {"ff2", "D0"}}
  });
  f.fp.nets.push_back({
      "external_io",
      {{"ff1", "Q0"}, {"PIN", "io"}}
  });
  f.fp.nets.push_back({
      "internal",
      {{"ff", "Q1"}, {"ff1", "D1"}}
  });
  f.fp.nets.push_back({
      "external_other",
      {{"ff", "D1"}, {"ff2", "Q0"}}
  });
  f.fp.nets.push_back({
      "clk",
      {{"ff", "CLK"}, {"ff1", "CLK"}},
      {},
      "CLOCK"
  });
}

void uniform_paired_design(Fixture& f) {
  paired_design(f);
  f.options.paired_pin_cap        = 16;
  f.fp.components[2].place.status = ph::Status::FIXED;  // External endpoint, outside the single movable cellbox.
  std::erase_if(f.fp.nets, [](const auto& net) { return net.name == "external_other"; });
}
}  // namespace

TEST(LegrouteResources, FiniteM2UnionRetainsCoordinateReasonsAndAnonymousLegacyHeadroom) {
  for (int64_t origin : {int64_t(-1000), int64_t(0), int64_t(1000)}) {
    SCOPED_TRACE(origin);
    Fixture f(origin);
    std::erase_if(f.fp.tracks, [](const auto& track) { return track.layer == "M2"; });
    f.fp.tracks.push_back({"Y", "M2", origin + 25, 40, 50});
    f.fp.tracks.push_back({"Y", "M2", origin + 50, 20, 100});
    f.fp.tracks.push_back({"Y", "M2", origin + 50, 20, 100});
    ph::Net power;
    power.name = "VDD";
    power.use  = "POWER";
    const ph::Route_path rail{
        "M2",
        20,
        {},
        {ph::Route_point{0, origin + 25, {}}, ph::Route_point{3200, origin + 25, {}}},
        {}
    };
    power.wires.push_back({
        "FIXED",
        {rail, rail}
    });
    f.fp.special_nets.push_back(power);
    Legroute_regions regions;
    regions.obstacles = {
        {"M2", {0, double(origin + 15), 3200, double(origin + 35)}}
    };
    f.options.reserved_tracks_per_um = 1;
    f.options.reserved_layers        = "M2";
    auto        grid                 = f.grid(regions);
    const int   t                    = grid.tile(1, 0);
    const auto& m2                   = layer(grid, "M2");
    ASSERT_EQ(m2.bands[0].size(), 15u);
    EXPECT_EQ(std::set<int64_t>(m2.bands[0].begin(), m2.bands[0].end()).size(), 15u);
    EXPECT_EQ(m2.grid_layer, -1);
    EXPECT_EQ(m2.tiles[t].anonymous_headroom, 0u);
    EXPECT_EQ(grid.box_cap[t], 13);
    EXPECT_EQ(reasons_at(m2, t, origin + 25),
              uint8_t(Track_exclusion::generic_reservation) | uint8_t(Track_exclusion::power_metal)
                  | uint8_t(Track_exclusion::region_obstacle));
    EXPECT_EQ(reasons_at(m2, t, origin + 50), uint8_t(Track_exclusion::power_metal) | uint8_t(Track_exclusion::region_obstacle));
    EXPECT_EQ(reasons_at(m2, t, origin + 75), 0);
    EXPECT_EQ(reasons_at(m2, grid.tile(1, 2), origin + 1025), uint8_t(Track_exclusion::generic_reservation));
    EXPECT_EQ(reasons_at(m2, grid.tile(1, 2), origin + 1050), 0);
    expect_capacity_parity(grid);

    f.options.reserved_tracks_per_um = -1;
    grid                             = f.grid(regions);
    const auto& legacy               = layer(grid, "M2");
    EXPECT_EQ(grid.box_cap[t], 12);
    EXPECT_EQ(legacy.tiles[t].anonymous_headroom, 1u);
    // Anonymous headroom must not invent a forbidden physical coordinate.
    EXPECT_EQ(legacy.tiles[t].exclusions.size(), 2u);
    EXPECT_EQ(reasons_at(legacy, t, origin + 75), 0);
    expect_capacity_parity(grid);
  }
}

TEST(LegrouteResources, UpperPGUnionPreservesOverlappingReasonsAndPerpendicularObstacleGeometry) {
  Fixture f;
  ph::Net power;
  power.name = "VDD";
  power.use  = "POWER";
  const ph::Route_path rail{
      "M4",
      20,
      {},
      {ph::Route_point{0, 1075, {}}, ph::Route_point{3200, 1075, {}}},
      {}
  };
  power.wires.push_back({
      "FIXED",
      {rail, rail}
  });
  f.fp.special_nets.push_back(power);
  Legroute_regions regions;
  regions.obstacles = {
      {"M4",  {800, 1065, 2400, 1085}},
      {"M4",    {0, 1015, 3200, 1035}},
      {"M4", {1240, 1000, 1260, 1500}}
  };
  const auto  grid = f.grid(regions);
  const int   t    = grid.tile(1, 2);
  const auto& m4   = layer(grid, "M4");
  EXPECT_EQ(m4.bands[m4.tiles[t].band].size(), 10u);
  EXPECT_EQ(m4.tiles[t].coarse_capacity, 8);
  EXPECT_EQ(m4.tiles[t].exclusions.size(), 2u);
  EXPECT_EQ(reasons_at(m4, t, 1025), uint8_t(Track_exclusion::generic_reservation) | uint8_t(Track_exclusion::region_obstacle));
  EXPECT_EQ(reasons_at(m4, t, 1075), uint8_t(Track_exclusion::power_metal) | uint8_t(Track_exclusion::region_obstacle));
  EXPECT_EQ(reasons_at(m4, t, 1125), 0);
  EXPECT_TRUE(std::ranges::any_of(grid.obstacles[size_t(m4.grid_layer)],
                                  [](const auto& r) { return r.xl == 1240 && r.xh == 1260 && r.yl == 1000 && r.yh == 1500; }));
  expect_capacity_parity(grid);
}

TEST(LegrouteResources, MacroMasksAndClockHeadroomDoNotInventTrackAssignments) {
  Fixture f;
  auto&   macro = f.tech.macros["ram"];
  macro.width   = .8;
  macro.height  = .5;
  macro.klass   = "BLOCK";
  macro.blocked_layers.insert("M4");
  Ann_place pose(800, 1000, 800, 500);
  pose.status = ph::Status::FIXED;
  f.fp.components.push_back({"mem", "ram", pose});
  auto        grid = f.grid();
  const int   t    = grid.tile(1, 2);
  const auto& m4   = layer(grid, "M4");
  EXPECT_EQ(m4.tiles[t].coarse_capacity, 0);
  EXPECT_EQ(m4.tiles[t].whole_band_exclusions, uint8_t(Track_exclusion::macro_footprint));
  EXPECT_TRUE(layer(grid, "M2").tiles[t].whole_band_exclusions & uint8_t(Track_exclusion::blocked_tile));
  for (size_t tile = 0; tile < grid.size(); ++tile) {
    EXPECT_EQ(grid.resources.anonymous_clock_v[tile], 1u);
    EXPECT_EQ(grid.resources.anonymous_clock_h[tile], unsigned(grid.row(int(tile)) % f.options.rib_tiles == 0));
  }
  expect_capacity_parity(grid);
  f.options.clock_spine    = false;
  const auto without_clock = f.grid();
  EXPECT_EQ(grid.cap, without_clock.cap);
  EXPECT_EQ(grid.box_cap, without_clock.box_cap);
  EXPECT_TRUE(std::ranges::all_of(without_clock.resources.anonymous_clock_h, [](auto n) { return n == 0; }));
  EXPECT_TRUE(std::ranges::all_of(without_clock.resources.anonymous_clock_v, [](auto n) { return n == 0; }));
}

TEST(LegrouteResources, UpperReservationPhaseMatchesPhysicalPolicyAtNegativeOrigins) {
  for (double density : {-1., 0., 1.}) {
    Fixture f(-1000);
    f.options.reserved_tracks_per_um = density;
    f.options.reserved_layers        = "M2,M4";
    const auto               grid    = f.grid();
    const Track_reservations policy(f.options, f.tech, f.fp.dbu);
    for (size_t l = 0; l < grid.layers.size(); ++l) {
      const auto& resource = grid.resources.layers[l];
      for (size_t t = 0; t < grid.size(); ++t) {
        const auto& coords = resource.bands[resource.tiles[t].band];
        for (size_t k = 0; k < coords.size(); ++k) {
          const bool selected = policy.selected(resource.name, coords[k], coords[k] - grid.layers[l].pitch, int64_t(k));
          EXPECT_EQ(bool(reasons_at(resource, int(t), coords[k]) & uint8_t(Track_exclusion::generic_reservation)), selected);
        }
      }
    }
    expect_capacity_parity(grid);
  }
}

TEST(LegrouteResources, AnonymousHeadroomSaturatesWithoutUnsignedUnderflow) {
  Fixture f;
  f.options.reserved_tracks = 100;
  f.options.reserved_layers = "M2";
  const auto  grid          = f.grid();
  const auto& m2            = layer(grid, "M2");
  for (size_t t = 0; t < grid.size(); ++t) {
    EXPECT_EQ(m2.tiles[t].anonymous_headroom, 100u);
    EXPECT_TRUE(m2.tiles[t].exclusions.empty());
    EXPECT_EQ(grid.box_cap[t], 0);
  }
  expect_capacity_parity(grid);
}

TEST(LegrouteResources, RejectsInvalidCoordinatesAndCapacityMismatch) {
  Track_resource_catalog catalog;
  catalog.anonymous_clock_h = catalog.anonymous_clock_v = {0};
  Track_resource_layer resource;
  resource.name  = "M2";
  resource.bands = {
      {-25, 25, 75}
  };
  resource.tiles.resize(1);
  resource.tiles[0].coarse_capacity = 2;
  resource.exclude(0, 0, Track_exclusion::power_metal);
  resource.exclude(0, 0, Track_exclusion::region_obstacle);
  catalog.layers.push_back(resource);
  EXPECT_NO_THROW(catalog.finalize());
  EXPECT_EQ(catalog.layers[0].tiles[0].exclusions.size(), 1u);
  auto bad                               = catalog;
  bad.layers[0].tiles[0].coarse_capacity = 3;
  EXPECT_THROW(bad.finalize(), std::logic_error);
  bad                    = catalog;
  bad.layers[0].bands[0] = {-25, 25, 25};
  EXPECT_THROW(bad.finalize(), std::logic_error);
  bad = catalog;
  bad.layers[0].exclude(0, 3, Track_exclusion::power_metal);
  EXPECT_THROW(bad.finalize(), std::logic_error);
}

TEST(LegrouteResources, UsableSupplyPrecedesBoxBudgetAndConservativelyKeepsAnonymousHeadroomPerRow) {
  Track_resource_catalog catalog;
  catalog.anonymous_clock_h = catalog.anonymous_clock_v = {0};
  Track_resource_layer resource;
  resource.name  = "M2";
  resource.bands = {
      {-75, -25, 25, 75}
  };
  resource.tiles.resize(1);
  resource.tiles[0].anonymous_headroom = 1;
  resource.tiles[0].cellbox_budget     = 1;
  resource.tiles[0].coarse_capacity    = 1;
  resource.exclude(0, 0, Track_exclusion::power_metal);
  resource.exclude(0, 0, Track_exclusion::region_obstacle);
  catalog.layers.push_back(resource);
  ASSERT_NO_THROW(catalog.finalize());
  const auto& resolved = catalog.layers[0];
  EXPECT_EQ(resolved.usable_capacity(0), 2);
  EXPECT_EQ(resolved.usable_capacity(0, -100, 0), 0);  // The remaining lower-row coordinate cannot stand in for anonymous headroom.
  EXPECT_EQ(resolved.usable_capacity(0, 0, 100), 1);
  EXPECT_EQ(resolved.represented_capacity(0), 1);
}

TEST(LegrouteResources, PairedReservationCountsExternalPinsAndReplacesUniformBudgetExactlyOnce) {
  Fixture f;
  paired_design(f);
  const auto d     = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
  auto       g     = build_grid(d, {});
  const auto boxes = build_boxes(d, g);
  ASSERT_EQ(boxes.boxes.size(), 2u);
  const auto base_cap     = g.cap;
  const auto base_box_cap = g.box_cap;
  resolve_paired_pin_reservations(d, g, boxes);
  ASSERT_EQ(g.paired_pin_reservations.size(), 2u);
  const auto& first = g.paired_pin_reservations[0];
  EXPECT_EQ(first.cells, (std::vector<int>{0, 1}));
  EXPECT_EQ(first.internal_nets, (std::vector<int>{2}));
  EXPECT_EQ(first.external_pins.size(), 5u);  // Three external nets, but five individual pin obligations.
  EXPECT_EQ(first.external_rows, (std::array<unsigned, 2>{3, 2}));
  EXPECT_EQ(first.horizontal_tracks, (std::array<unsigned, 2>{1, 1}));
  EXPECT_EQ(first.vertical_tracks, 3u);
  EXPECT_EQ(first.actual_pairs, 3u);
  EXPECT_FALSE(first.grouping_invalid);  // cap=0 retains the previous odd/imbalanced count-control behavior.
  EXPECT_EQ(first.internal_vertical_estimate, 1u);
  EXPECT_EQ(first.internal_horizontal_estimate, 1u);
  EXPECT_FALSE(first.short_supply);
  EXPECT_TRUE(
      std::ranges::any_of(first.external_pins, [](const auto& pin) { return pin.net == 1 && pin.pin == 0 && pin.row == 1; }));
  EXPECT_EQ(g.paired_pin_reservations[1].external_pins.size(), 2u);
  EXPECT_EQ(g.paired_pin_reservations[1].vertical_tracks, 1u);
  for (size_t t = 0; t < g.size(); ++t) {
    const int index = g.paired_pin_reservation_of_tile[t];
    EXPECT_EQ(g.box_cap[t], base_box_cap[t] - (index >= 0 ? 2 : 0));
    const auto budget = index >= 0 ? g.paired_pin_reservations[size_t(index)].vertical_tracks
                                         + g.paired_pin_reservations[size_t(index)].internal_vertical_estimate
                                   : 0;
    EXPECT_EQ(g.cap[size_t(first.vertical_layer)][t], base_cap[size_t(first.vertical_layer)][t] - int(budget));
    EXPECT_EQ(layer(g, "M3").tiles[t].cellbox_budget, budget);
  }
  const auto once    = g.paired_pin_reservations;
  const auto cap     = g.cap;
  const auto box_cap = g.box_cap;
  resolve_paired_pin_reservations(d, g, boxes);
  EXPECT_EQ(g.paired_pin_reservations, once);
  EXPECT_EQ(g.cap, cap);
  EXPECT_EQ(g.box_cap, box_cap);
  EXPECT_NO_THROW(validate_paired_pin_reservations(d, g, boxes));
  expect_capacity_parity(g);

  auto stale = boxes;
  stale.boxes[0].cells.pop_back();
  EXPECT_THROW(validate_paired_pin_reservations(d, g, stale), std::logic_error);
  f.fp.components[1].place.y           = 0;
  f.fp.components[1].place.orientation = ph::Orientation::N;
  EXPECT_THROW(validate_paired_pin_reservations(d, g, boxes), std::logic_error);
  resolve_paired_pin_reservations(d, g, boxes);  // Same membership, new row obligations: never widen ceil(P/2) for imbalance.
  EXPECT_EQ(g.paired_pin_reservations[0].external_rows, (std::array<unsigned, 2>{5, 0}));
  EXPECT_EQ(g.paired_pin_reservations[0].vertical_tracks, 3u);
  EXPECT_NO_THROW(validate_paired_pin_reservations(d, g, boxes));
}

TEST(LegrouteResources, PairedReservationRequiresOnePhysicalM2TrackInEachRow) {
  Fixture f;
  paired_design(f);
  std::erase_if(f.fp.tracks, [](const auto& track) { return track.layer == "M2"; });
  f.fp.tracks.push_back({"Y", "M2", 25, 2, 50});  // Two tracks total, both in the lower physical row.
  const auto d     = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
  auto       g     = build_grid(d, {});
  const auto boxes = build_boxes(d, g);
  resolve_paired_pin_reservations(d, g, boxes);
  const auto& reservation = g.paired_pin_reservations[0];
  EXPECT_EQ(reservation.horizontal_usable, (std::array<unsigned, 2>{2, 0}));
  EXPECT_EQ(reservation.horizontal_tracks, (std::array<unsigned, 2>{1, 1}));
  EXPECT_EQ(reservation.vertical_tracks, 3u);
  EXPECT_TRUE(reservation.short_supply);
  EXPECT_TRUE(g.box_short[size_t(reservation.tile)]);
  EXPECT_FALSE(box_fits(d, g, boxes.boxes[0], reservation.tile));
  const auto keys = g.keys.size();
  resolve_paired_pin_reservations(d, g, boxes);
  EXPECT_EQ(g.keys.size(), keys);
  EXPECT_NO_THROW(validate_paired_pin_reservations(d, g, boxes));
  expect_capacity_parity(g);
}

TEST(LegrouteResources, PairedReservationSubtractsOverlappingPowerExclusionsOnlyOnce) {
  Fixture f;
  paired_design(f);
  ph::Net power;
  power.name = "VDD";
  power.use  = "POWER";
  const ph::Route_path rail{
      "M3",
      20,
      {},
      {ph::Route_point{75, 0, {}}, ph::Route_point{75, 2000, {}}},
      {}
  };
  power.wires.push_back({
      "FIXED",
      {rail, rail}
  });
  f.fp.special_nets.push_back(power);
  Legroute_regions regions;
  regions.obstacles.push_back({
      "M3",
      {65, 0, 85, 2000}
  });
  const auto  d     = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
  auto        g     = build_grid(d, regions);
  const auto  boxes = build_boxes(d, g);
  const int   tile  = boxes.boxes[0].tile;
  const auto& m3    = layer(g, "M3");
  ASSERT_EQ(m3.tiles[size_t(tile)].exclusions.size(), 1u);
  const auto supply = int(m3.bands[m3.tiles[size_t(tile)].band].size()) - 1;
  resolve_paired_pin_reservations(d, g, boxes);
  const auto& reservation = g.paired_pin_reservations[0];
  EXPECT_EQ(reservation.vertical_usable, unsigned(supply));
  EXPECT_EQ(g.cap[size_t(reservation.vertical_layer)][size_t(tile)],
            supply - int(reservation.vertical_tracks + reservation.internal_vertical_estimate));
  EXPECT_EQ(reasons_at(layer(g, "M3"), tile, 75),
            uint8_t(Track_exclusion::power_metal) | uint8_t(Track_exclusion::region_obstacle));
  expect_capacity_parity(g);
}

TEST(LegrouteResources, PairedReservationDoesNotInventM2SupplyWithoutSupportedDefTracks) {
  for (const bool masked_pattern : {false, true}) {
    SCOPED_TRACE(masked_pattern);
    Fixture f;
    paired_design(f);
    std::erase_if(f.fp.tracks, [](const auto& track) { return track.layer == "M2"; });
    if (masked_pattern) {
      f.fp.tracks.push_back({"Y", "M2", 25, 40, 50, 1});
    }
    const auto d     = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
    auto       g     = build_grid(d, {});
    const auto boxes = build_boxes(d, g);
    resolve_paired_pin_reservations(d, g, boxes);
    ASSERT_FALSE(g.paired_pin_reservations.empty());
    for (const auto& reservation : g.paired_pin_reservations) {
      EXPECT_EQ(reservation.horizontal_usable, (std::array<unsigned, 2>{0, 0}));
      EXPECT_TRUE(reservation.short_supply);
      EXPECT_TRUE(g.box_short[size_t(reservation.tile)]);
    }
    EXPECT_NO_THROW(validate_paired_pin_reservations(d, g, boxes));
    expect_capacity_parity(g);

    f.options.paired_pin_reserve = false;
    const auto legacy            = build_grid(d, {});
    EXPECT_FALSE(layer(legacy, "M2").bands[0].empty());  // The uniform policy keeps its existing LEF fallback.
  }
}

TEST(LegrouteResources, PairedReservationRejectsMovableAndAmbiguousRowContracts) {
  Fixture f;
  paired_design(f);
  const auto d         = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
  auto       g         = build_grid(d, {});
  auto       boxes     = build_boxes(d, g);
  boxes.boxes[0].fixed = false;
  EXPECT_THROW(resolve_paired_pin_reservations(d, g, boxes), std::invalid_argument);
  EXPECT_TRUE(g.paired_pin_reservations.empty());
  boxes.boxes[0].fixed       = true;
  f.fp.components[0].place.y = 1;
  EXPECT_THROW(resolve_paired_pin_reservations(d, g, boxes), std::logic_error);
  EXPECT_TRUE(g.paired_pin_reservations.empty());
}

TEST(LegrouteResources, PairedReservationRejectsRoutedResetEvenWhenFinalWouldSkipIt) {
  for (const auto* skip : {"", "reset"}) {
    SCOPED_TRACE(skip);
    Fixture f;
    paired_design(f);
    f.options.final_skip_nets           = skip;
    f.tech.macros.at("dff").pins["RST"] = {"INPUT", "SIGNAL", {{"M1", {.04, .05, .06, .2}}}};
    f.fp.nets.push_back({
        "async_reset",
        {{"ff", "Q0"}, {"ff1", "RST"}}
    });
    const auto d = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
    ASSERT_TRUE(d.nets.back().routed && d.nets.back().reset);
    auto       g      = build_grid(d, {});
    const auto boxes  = build_boxes(d, g);
    const auto before = g.cap;
    try {
      resolve_paired_pin_reservations(d, g, boxes);
      FAIL() << "routed reset was silently omitted from paired obligations";
    } catch (const std::invalid_argument& error) {
      EXPECT_NE(std::string(error.what()).find("async_reset"), std::string::npos);
      EXPECT_NE(std::string(error.what()).find("routed reset"), std::string::npos);
    }
    EXPECT_TRUE(g.paired_pin_reservations.empty());
    EXPECT_EQ(g.cap, before);
  }
}

TEST(LegrouteResources, PairedReservationRejectsCaseInsensitiveSkippedRoutedSignal) {
  Fixture f;
  paired_design(f);
  f.options.final_skip_nets = "TERNAL_MA";  // Case-insensitive search matches external_many.
  const auto d              = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
  auto       g              = build_grid(d, {});
  const auto boxes          = build_boxes(d, g);
  try {
    resolve_paired_pin_reservations(d, g, boxes);
    FAIL() << "Final-only skip silently removed a routed signal's paired obligations";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find("external_many"), std::string::npos);
    EXPECT_NE(std::string(error.what()).find("final_skip_nets"), std::string::npos);
  }
  EXPECT_TRUE(g.paired_pin_reservations.empty());
  f.options.final_skip_nets = "missing_net";
  EXPECT_NO_THROW(resolve_paired_pin_reservations(d, g, boxes));
  ASSERT_EQ(g.paired_pin_reservations.size(), 2u);
  EXPECT_EQ(g.paired_pin_reservations[0].external_pins.size(), 5u);
}

TEST(LegrouteResources, PairedReservationRequiresExplicitClockExclusion) {
  Fixture f;
  paired_design(f);
  f.options.final_skip_clock = false;
  f.options.final_skip_nets  = "clk";  // A name skip does not establish the ordinary-only clock contract.
  const auto d               = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
  auto       g               = build_grid(d, {});
  const auto boxes           = build_boxes(d, g);
  try {
    resolve_paired_pin_reservations(d, g, boxes);
    FAIL() << "required clock was silently omitted without final_skip_clock";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find("clk"), std::string::npos);
    EXPECT_NE(std::string(error.what()).find("final_skip_clock"), std::string::npos);
  }
  EXPECT_TRUE(g.paired_pin_reservations.empty());
  f.options.final_skip_clock = true;
  EXPECT_NO_THROW(resolve_paired_pin_reservations(d, g, boxes));
}

TEST(LegrouteResources, PairedReservationReportsVerticalShortageWithoutChangingTheNominalRule) {
  Fixture f;
  paired_design(f);
  Legroute_regions regions;
  regions.obstacles.push_back({
      "M3",
      {0, 0, 700, 2000}
  });
  const auto d     = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
  auto       g     = build_grid(d, regions);
  const auto boxes = build_boxes(d, g);
  resolve_paired_pin_reservations(d, g, boxes);
  const auto& reservation = g.paired_pin_reservations[0];
  EXPECT_EQ(reservation.vertical_usable, 2u);
  EXPECT_EQ(reservation.vertical_tracks, 3u);
  EXPECT_TRUE(reservation.short_supply);
  EXPECT_EQ(g.cap[size_t(reservation.vertical_layer)][size_t(reservation.tile)], 0);
  EXPECT_EQ(layer(g, "M3").tiles[size_t(reservation.tile)].cellbox_budget, 4u);
  EXPECT_NO_THROW(validate_paired_pin_reservations(d, g, boxes));
  expect_capacity_parity(g);
}

TEST(LegrouteResources, PairedReservationRetainsSeparateInternalDemandWhenExternalPinCountIsZero) {
  Fixture f;
  paired_design(f);
  std::erase_if(f.fp.nets, [](const auto& net) { return net.name != "internal"; });
  const auto d     = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
  auto       g     = build_grid(d, {});
  const auto boxes = build_boxes(d, g);
  resolve_paired_pin_reservations(d, g, boxes);
  const auto& reservation = g.paired_pin_reservations[0];
  EXPECT_TRUE(reservation.external_pins.empty());
  EXPECT_EQ(reservation.vertical_tracks, 0u);
  EXPECT_EQ(reservation.horizontal_tracks, (std::array<unsigned, 2>{1, 1}));
  EXPECT_EQ(reservation.internal_nets, (std::vector<int>{0}));
  EXPECT_EQ(reservation.internal_vertical_estimate, 1u);
  EXPECT_EQ(reservation.internal_horizontal_estimate, 1u);
  EXPECT_EQ(layer(g, "M3").tiles[size_t(reservation.tile)].cellbox_budget, 1u);
  EXPECT_EQ(g.cap[size_t(reservation.vertical_layer)][size_t(reservation.tile)], int(reservation.vertical_usable) - 1);
  EXPECT_FALSE(reservation.short_supply);
  EXPECT_NO_THROW(validate_paired_pin_reservations(d, g, boxes));
  expect_capacity_parity(g);
}

TEST(LegrouteResources, PairedInternalHorizontalEstimateUsesWholeM2SupplyAndReportsExcess) {
  Fixture f;
  paired_design(f);
  std::erase_if(f.fp.nets, [](const auto& net) { return net.name != "internal"; });
  auto& master      = f.tech.macros.at("dff");
  master.pins["D2"] = master.pins.at("D1");
  master.pins["Q2"] = master.pins.at("Q1");
  f.fp.nets.push_back({
      "internal_2",
      {{"ff", "Q0"}, {"ff1", "D0"}}
  });
  f.fp.nets.push_back({
      "internal_3",
      {{"ff", "Q2"}, {"ff1", "D2"}}
  });
  std::erase_if(f.fp.tracks, [](const auto& track) { return track.layer == "M2"; });
  f.fp.tracks.push_back({"Y", "M2", 25, 2, 250});
  for (bool excess : {true, false}) {
    const auto d     = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
    auto       g     = build_grid(d, {});
    const auto boxes = build_boxes(d, g);
    resolve_paired_pin_reservations(d, g, boxes);
    const auto& reservation = g.paired_pin_reservations[0];
    EXPECT_EQ(reservation.horizontal_usable, (std::array<unsigned, 2>{1, 1}));
    EXPECT_EQ(reservation.horizontal_tracks, (std::array<unsigned, 2>{1, 1}));
    EXPECT_EQ(reservation.vertical_tracks, 0u);
    EXPECT_EQ(reservation.internal_horizontal_estimate, excess ? 3u : 2u);
    EXPECT_EQ(reservation.short_supply, excess);
    EXPECT_EQ(g.box_cap[size_t(reservation.tile)], 0);  // Internal demand may use the protected tracks too.
    EXPECT_NO_THROW(validate_paired_pin_reservations(d, g, boxes));
    f.fp.nets.pop_back();
  }
}

TEST(LegrouteResources, PairedUpperSupplyExcludesMissingFiniteDefTracksWithoutChargingPowerTwice) {
  Fixture f;
  paired_design(f);
  std::erase_if(f.fp.tracks, [](const auto& track) { return track.layer == "M3"; });
  f.fp.tracks.push_back({"X", "M3", 25, 3, 50});  // Only x=75 and125 are also on the interior Local lattice.
  f.fp.tracks.push_back({"X", "M3", 90, 2, 50});  // Additional off-lattice tracks remain conservatively unrepresented.
  ph::Net power;
  power.name = "VDD";
  power.use  = "POWER";
  const ph::Route_path rail{
      "M3",
      20,
      {},
      {ph::Route_point{175, 0, {}}, ph::Route_point{175, 2000, {}}},
      {}
  };
  power.wires.push_back({
      "FIXED",
      {rail, rail}
  });
  f.fp.special_nets.push_back(power);
  const auto  d    = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
  auto        g    = build_grid(d, {});
  const auto& m3   = layer(g, "M3");
  const int   tile = g.tile(0, 0);
  EXPECT_EQ(m3.usable_capacity(size_t(tile)), 2);
  EXPECT_EQ(g.cap[size_t(m3.grid_layer)][size_t(tile)], 2);
  EXPECT_EQ(reasons_at(m3, tile, 75), 0);
  EXPECT_EQ(reasons_at(m3, tile, 175), uint8_t(Track_exclusion::missing_def_track) | uint8_t(Track_exclusion::power_metal));
  const auto boxes = build_boxes(d, g);
  resolve_paired_pin_reservations(d, g, boxes);
  EXPECT_EQ(g.paired_pin_reservations[0].vertical_usable, 2u);
  EXPECT_TRUE(g.paired_pin_reservations[0].short_supply);
  expect_capacity_parity(g);

  f.options.paired_pin_reserve = false;
  f.options.box_reserve.clear();
  const auto  legacy = build_grid(d, {});
  const auto& old_m3 = layer(legacy, "M3");
  EXPECT_GT(old_m3.usable_capacity(size_t(tile)), 2);
  EXPECT_FALSE(reasons_at(old_m3, tile, 175) & uint8_t(Track_exclusion::missing_def_track));
}

TEST(LegrouteResources, CriticalUpperSupplyUnionsMissingTracksAndPowerBeforeReservingBudget) {
  Fixture f;
  f.options.critical_cellboxes = true;
  f.options.reserved_tracks    = 0;
  f.options.clock_spine        = false;
  std::erase_if(f.fp.tracks, [](const auto& track) { return track.layer == "M3"; });
  f.fp.tracks.push_back({"X", "M3", 25, 3, 50});
  f.fp.tracks.push_back({"X", "M3", 225, 60, 50});  // Native x=175 is absent.
  ph::Net power;
  power.name = "VDD";
  power.use  = "POWER";
  const ph::Route_path rail{
      "M3",
      20,
      {},
      {ph::Route_point{175, 0, {}}, ph::Route_point{175, 2000, {}}},
      {}
  };
  power.wires.push_back({
      "FIXED",
      {rail, rail}
  });
  f.fp.special_nets.push_back(power);
  const auto  g    = f.grid();
  const auto& m3   = layer(g, "M3");
  const auto  tile = size_t(g.tile(0, 0));
  ASSERT_EQ(g.tile_w, 1100);
  ASSERT_EQ(g.rows_per_tile, 4u);
  EXPECT_EQ(m3.usable_capacity(tile), 20);
  EXPECT_EQ(m3.tiles[tile].cellbox_budget, 20u);
  EXPECT_EQ(g.cap[size_t(m3.grid_layer)][tile], 0);
  EXPECT_FALSE(g.box_short[tile]);  // Exactly enough supply; do not charge the absent PG track twice.
  EXPECT_EQ(reasons_at(m3, int(tile), 175), uint8_t(Track_exclusion::missing_def_track) | uint8_t(Track_exclusion::power_metal));
  expect_capacity_parity(g);
}

TEST(LegrouteResources, UniformPairedCapChargesAllEightTracksEvenWithUnusedPairs) {
  for (bool no_external : {false, true}) {
    SCOPED_TRACE(no_external);
    Fixture f;
    uniform_paired_design(f);
    if (no_external) {
      std::erase_if(f.fp.nets, [](const auto& net) { return net.name != "internal"; });
    }
    const auto d     = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
    auto       g     = build_grid(d, {});
    const auto boxes = build_boxes(d, g);
    ASSERT_EQ(boxes.boxes.size(), 1u);
    const auto before = transit_capacity(g);
    resolve_paired_pin_reservations(d, g, boxes);
    const auto reservation = g.paired_pin_reservations[0];
    EXPECT_EQ(reservation.external_pins.size(), no_external ? 0u : 4u);
    EXPECT_EQ(reservation.external_rows, (no_external ? std::array<unsigned, 2>{0, 0} : std::array<unsigned, 2>{2, 2}));
    EXPECT_EQ(reservation.actual_pairs, no_external ? 0u : 2u);
    EXPECT_EQ(reservation.vertical_tracks, 8u);
    EXPECT_EQ(reservation.internal_vertical_estimate, 1u);
    EXPECT_FALSE(reservation.grouping_invalid);
    EXPECT_FALSE(reservation.short_supply);
    EXPECT_EQ(layer(g, "M3").tiles[size_t(reservation.tile)].cellbox_budget, 9u);
    const auto uniform = transit_capacity(g);
    EXPECT_EQ(uniform.second[size_t(reservation.tile)], before.second[size_t(reservation.tile)] - 9);
    EXPECT_FALSE(paired_pin_supply_gate(d, g).requires_replan());
    EXPECT_NO_THROW(validate_paired_pin_reservations(d, g, boxes));

    f.options.paired_pin_cap = 0;
    resolve_paired_pin_reservations(d, g, boxes);
    const auto count_control = transit_capacity(g);
    EXPECT_EQ(count_control.second[size_t(reservation.tile)] - uniform.second[size_t(reservation.tile)],
              8 - int(reservation.actual_pairs));
    f.options.paired_pin_cap = 16;
    resolve_paired_pin_reservations(d, g, boxes);
    EXPECT_EQ(transit_capacity(g), uniform);  // Re-resolution never releases or accumulates the unused uniform lanes.
    EXPECT_NO_THROW(validate_paired_pin_reservations(d, g, boxes));
    expect_capacity_parity(g);
  }
}

TEST(LegrouteResources, UniformPairedCapReportsGroupingDeficienciesWithoutChangingItsCharge) {
  for (const auto* deficiency : {"cap", "odd", "rows"}) {
    SCOPED_TRACE(deficiency);
    Fixture f;
    uniform_paired_design(f);
    if (std::string_view(deficiency) == "cap") {
      f.options.paired_pin_cap = 2;
    } else if (std::string_view(deficiency) == "odd") {
      std::erase_if(f.fp.nets, [](const auto& net) { return net.name == "external_io"; });
    } else {
      f.fp.components[1].place.y           = 0;
      f.fp.components[1].place.orientation = ph::Orientation::N;
    }
    const auto d     = build_design(f.tech, f.fp, f.timing, f.options, nullptr);
    auto       g     = build_grid(d, {});
    const auto boxes = build_boxes(d, g);
    ASSERT_EQ(boxes.boxes.size(), 1u);
    EXPECT_NO_THROW(resolve_paired_pin_reservations(d, g, boxes));
    const auto& reservation = g.paired_pin_reservations[0];
    EXPECT_TRUE(reservation.grouping_invalid);
    EXPECT_FALSE(reservation.short_supply);  // Group construction failed despite enough physical supply.
    EXPECT_EQ(reservation.actual_pairs, reservation.external_pins.size() / 2);
    EXPECT_EQ(reservation.vertical_tracks, f.options.paired_pin_cap / 2);
    EXPECT_EQ(layer(g, "M3").tiles[size_t(reservation.tile)].cellbox_budget,
              f.options.paired_pin_cap / 2 + reservation.internal_vertical_estimate);
    const auto gate = paired_pin_supply_gate(d, g);
    EXPECT_TRUE(gate.requires_replan());
    EXPECT_EQ(gate.unfittable_boxes, 1u);
    EXPECT_NO_THROW(validate_paired_pin_reservations(d, g, boxes));
    expect_capacity_parity(g);
  }
}
}  // namespace livehd::placement::lr
