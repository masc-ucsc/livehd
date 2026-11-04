// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <set>

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
}  // namespace livehd::placement::lr
