// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <numeric>

#include "gtest/gtest.h"
#include "inou/lefdef/routing_geometry.hpp"
#include "legroute_test_frame.hpp"

namespace livehd::placement {
using Fixture = Legroute_test_frame;

TEST(LegrouteGrid, PowerOnlyProfileUsesTheLefSignalStack) {
  Fixture f;
  f.cell("dff", 0, 0);
  auto d = lr::build_design(f.tech, f.fp, f.timing, {}, nullptr);
  const auto reference = lr::build_grid(d, {});
  d.profile = &physical::tech_profile("sky130hd");
  const auto actual = lr::build_grid(d, {});
  EXPECT_EQ(actual.escape_layer, reference.escape_layer);
  ASSERT_EQ(actual.layers.size(), reference.layers.size());
  for (size_t i = 0; i < actual.layers.size(); ++i) {
    EXPECT_EQ(actual.layers[i].name, reference.layers[i].name);
  }
}

TEST(LegrouteGrid, TwoLayerStackRoutesOnBothLayers) {
  // A two-layer technology has no layer to spare for pin escape: both layers route, one per direction.
  for (size_t keep : {2u, 1u}) {
    SCOPED_TRACE(keep);
    Fixture f;
    f.cell("dff", 0, 0);
    f.tech.routing_layers.resize(keep);
    std::erase_if(f.fp.tracks, [&](const auto& t) { return t.layer != "M1" && t.layer != "M2"; });
    for (const auto* name : {"M3", "M4", "M5", "M6"}) {
      f.tech.layers.erase(name);
    }
    const auto d = lr::build_design(f.tech, f.fp, f.timing, {}, nullptr);
    if (keep == 1) {
      EXPECT_THROW((void)lr::build_grid(d, {}), std::invalid_argument);
      continue;
    }
    const auto g = lr::build_grid(d, {});
    EXPECT_TRUE(g.escape_layer.empty());
    ASSERT_EQ(g.layers.size(), 2u);
    EXPECT_EQ(g.layers[0].name, "M1");
    EXPECT_FALSE(g.layers[0].horizontal);
    EXPECT_EQ(g.layers[1].name, "M2");
    EXPECT_TRUE(g.layers[1].horizontal);
    const auto [h, v] = lr::transit_capacity(g);
    EXPECT_GT(h[size_t(g.tile(2, 3))], 0);
    EXPECT_GT(v[size_t(g.tile(2, 3))], 0);
  }
}

TEST(LegrouteGrid, RouteEscapeCountsTheEscapeLayerAndEveryInterleavedTrackPattern) {
  // route_escape adds the escape layer (M2 here) to the routed stack, as FastRoute routes from MIN_ROUTING_LAYER
  Fixture f;
  f.cell("dff", 0, 0);
  Legroute_options opt;
  opt.route_escape = true;
  const auto d     = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  const auto g     = lr::build_grid(d, {});
  ASSERT_EQ(g.layers.size(), 5u);  // M2..M6
  EXPECT_EQ(g.layers[0].name, "M2");
  EXPECT_TRUE(g.layers[0].horizontal);
  const auto single = g.cap[0];
  // The same M2 tracks written as five patterns of one row's step (ORFS asap7 writes M2 so): every track counts,
  // not only the first pattern's one per row
  Fixture split;
  split.cell("dff", 0, 0);
  std::erase_if(split.fp.tracks, [](const auto& t) { return t.layer == "M2"; });
  for (int64_t start : {25, 75, 125, 175, 225}) {
    split.fp.tracks.push_back({"Y", "M2", start, 16, 250});
  }
  const auto ds = lr::build_design(split.tech, split.fp, split.timing, opt, nullptr);
  const auto gs = lr::build_grid(ds, {});
  ASSERT_EQ(gs.layers[0].name, "M2");
  EXPECT_EQ(gs.layers[0].pitch, 50);
  EXPECT_EQ(gs.cap[0], single);
  // without route_escape the escape layer stays out of the stack
  const auto dn = lr::build_design(f.tech, f.fp, f.timing, {}, nullptr);
  EXPECT_EQ(lr::build_grid(dn, {}).layers.front().name, "M3");
}

TEST(LegrouteGrid, TileGridCapacityStripesAndMacros) {
  Fixture f;
  f.cell("dff", 0, 0);
  // a fixed obstacle macro over the right part of rows 4..7, and an M4 power stripe at y = 2075
  auto& block  = f.tech.macros["block"];
  block.width  = 1.6;
  block.height = 1;
  block.klass  = "BLOCK";
  block.obstructions.push_back({
      "M3",
      {0, 0, 1.6, 1}
  });
  Ann_place p(8000, 1000, 1600, 1000);
  p.status = ph::Status::FIXED;
  f.fp.components.push_back({"blk", "block", p});
  ph::Net vdd{"VDD", {}};
  vdd.use = "POWER";
  ph::Route_path stripe{
      "M4",
      20,
      {},
      {ph::Route_point{0, 2075, {}}, ph::Route_point{10000, 2075, {}}},
      {}
  };
  vdd.wires.push_back({"FIXED", {stripe}});
  f.fp.special_nets.push_back(vdd);
  for (unsigned reserved : {0u, 1u}) {
    SCOPED_TRACE(reserved);
    Legroute_options opt;
    opt.reserved_tracks = reserved;
    const auto d        = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
    const auto g        = lr::build_grid(d, {});
    EXPECT_EQ(g.tile_w, 800);  // two flops wide
    EXPECT_EQ(g.tile_h, 500);  // two rows
    EXPECT_EQ(g.cols, 13);
    EXPECT_EQ(g.rows, 8);
    EXPECT_EQ(g.escape_layer, "M2");
    ASSERT_EQ(g.layers.size(), 4u);  // M3..M6: the stack above the escape layer
    int m3 = -1, m4 = -1;
    for (int l = 0; l < int(g.layers.size()); ++l) {
      m3 = g.layers[l].name == "M3" ? l : m3;
      m4 = g.layers[l].name == "M4" ? l : m4;
    }
    ASSERT_GE(m3, 0);
    ASSERT_GE(m4, 0);
    EXPECT_FALSE(g.layers[size_t(m3)].horizontal);
    EXPECT_TRUE(g.layers[size_t(m4)].horizontal);
    // a macro tile loses the macro's layers and its row sites
    EXPECT_EQ(g.cap[m3][g.tile(10, 2)], 0);
    EXPECT_EQ(g.placeable_sites[size_t(g.tile(10, 2))], 0);
    EXPECT_EQ(g.placeable_sites[size_t(g.tile(2, 2))], 32);  // 16 sites in each of the two rows
    EXPECT_EQ(g.cap[m3][g.tile(2, 2)], 16 - int(reserved));  // 800 / 50 tracks
    // The stripe takes one of the ten M4 tracks of its tile row (the track after the reserved one).
    EXPECT_EQ(g.cap[m4][g.tile(2, 4)], 10 - int(reserved) - 1);
    EXPECT_EQ(g.cap[m4][g.tile(2, 3)], 10 - int(reserved));
    const auto [h, v] = lr::transit_capacity(g);
    EXPECT_EQ(h[size_t(g.tile(2, 3))], (10 - int(reserved)) + (5 - int(reserved)));  // M4 + M6
    EXPECT_EQ(v[size_t(g.tile(2, 3))], (16 - int(reserved)) + (8 - int(reserved)));  // M3 + M5
  }
}

TEST(LegrouteGrid, PhysicalPowerTracksAreDeduplicatedAgainstEachOtherAndTheReserve) {
  Fixture f(64, 8);
  f.cell("dff", 0, 0);
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
  Legroute_power resources;
  resources.obstacles = {
      {"M4",  {800, 1065, 2400, 1085}}, // same physical track, partial overlapping span
      {"M4",    {0, 1015, 3200, 1035}}, // the first track of the band: the reserved one when a reserve is set
      {"M4", {1240, 1000, 1260, 1500}}  // perpendicular metal remains an obstacle, takes no track
  };
  for (unsigned reserved : {0u, 1u}) {
    SCOPED_TRACE(reserved);
    Legroute_options options;
    options.reserved_tracks = reserved;
    const auto design       = lr::build_design(f.tech, f.fp, f.timing, options, nullptr);
    auto       grid         = lr::build_grid(design, resources);
    const auto layer        = size_t(std::ranges::find(grid.layers, "M4", &lr::Layer::name) - grid.layers.begin());
    ASSERT_LT(layer, grid.layers.size());
    const int tile           = grid.tile(1, 2);
    const auto [first, last] = grid.tracks(int(layer), 2);
    EXPECT_EQ(last - first + 1, 10);
    EXPECT_EQ(grid.cap[layer][tile], 8);          // the first track once (reserve or metal) and one different PG track
    ASSERT_EQ(grid.obstacles[layer].size(), 5u);  // no obstacle was discarded
    EXPECT_TRUE(std::ranges::any_of(grid.obstacles[layer], [](const ph::Rect& r) {
      return r.xl == 1240 && r.xh == 1260 && r.yl == 1000 && r.yh == 1500;
    }));
    auto more = resources;
    more.obstacles.push_back({
        "M4",
        {0, 1115, 3200, 1135}
    });
    grid = lr::build_grid(design, more);
    EXPECT_EQ(grid.cap[layer][tile], 7);  // a genuinely different physical track is still deducted
  }
}

TEST(LegrouteGrid, PlaceableSitesSubtractFixedObjectsButNotMovableCells) {
  Fixture f(40, 2);
  auto&   macro                             = f.tech.macros["block"];
  macro.klass                               = "BLOCK";
  macro.width                               = .5;
  macro.height                              = .5;
  const auto fixed_macro                    = f.cell("block", 0, 0);
  f.fp.components[fixed_macro].place.status = ph::Status::FIXED;
  const auto fixed_core                     = f.cell("inv", 500, 0);
  f.fp.components[fixed_core].place.status  = ph::Status::FIXED;
  f.tech.macros["tap"]                      = f.tech.macros.at("inv");
  f.tech.macros["tap"].klass                = "CORE WELLTAP";
  f.tech.macros["tap"].pins.clear();
  const auto tap                    = f.cell("tap", 1000, 250);
  f.fp.components[tap].place.status = ph::Status::FIXED;
  f.cell("inv", 750, 0);
  f.cell("dff", 1200, 250);
  Legroute_options options;
  const auto       d = lr::build_design(f.tech, f.fp, f.timing, options, nullptr);
  const auto       g = lr::build_grid(d, {});
  ASSERT_EQ(g.placeable_sites.size(), g.size());
  // 2 rows x 40 sites, less the macro (10 sites in both rows), the fixed inverter and the tap (2 sites each)
  EXPECT_EQ(std::accumulate(g.placeable_sites.begin(), g.placeable_sites.end(), int64_t{}), 56);
  EXPECT_EQ(d.movable.size(), 2u);
}

TEST(LegrouteGrid, MacroTileSnap) {
  Fixture f;
  f.cell("dff", 0, 0);
  const auto t = legroute_tile(f.tech, f.fp);
  EXPECT_EQ(t.w, 800);
  EXPECT_EQ(t.h, 500);
  EXPECT_EQ(snap_to_tiles(t, 1234, 777, 0, 0), (std::pair<int64_t, int64_t>{1600, 1000}));
  EXPECT_EQ(snap_to_tiles(t, 1234, 777, 100, 100), (std::pair<int64_t, int64_t>{900, 600}));
}

TEST(LegrouteGrid, FixedMacroPinsUseTheirTransformedAccessAndRotatedFootprint) {
  for (const auto orientation : {ph::Orientation::N,
                                 ph::Orientation::S,
                                 ph::Orientation::FN,
                                 ph::Orientation::FS,
                                 ph::Orientation::E,
                                 ph::Orientation::W,
                                 ph::Orientation::FE,
                                 ph::Orientation::FW}) {
    SCOPED_TRACE(int(orientation));
    Fixture f;
    f.cell("dff", 0, 0);
    auto& macro          = f.tech.macros["ram"];
    macro.width          = 2;
    macro.height         = 1;
    macro.origin_x       = .2;
    macro.origin_y       = .1;
    macro.klass          = "BLOCK";
    macro.blocked_layers = {"M1", "M2", "M3", "M4", "M5", "M6"};
    macro.pins["L"]      = {"INPUT", "SIGNAL", {{"M4", {-.2, 0, -.1, .05}}}};
    macro.pins["R"]      = {"INPUT", "SIGNAL", {{"M4", {1.7, .5, 1.8, .55}}}};
    macro.pins["I"]      = {"INPUT", "SIGNAL", {{"M4", {.6, .3, .7, .35}}}};
    const auto hard = f.cell("ram", 4000, 1500), driver = f.cell("inv", 500, 0);
    f.fp.components[hard].place.status      = ph::Status::FIXED;
    f.fp.components[hard].place.orientation = orientation;
    if (ph::swaps_axes(orientation)) {
      std::swap(f.fp.components[hard].place.width, f.fp.components[hard].place.height);
    }
    f.net({
        {driver, "Y"},
        {  hard, "L"}
    });
    f.net({
        {driver, "Y"},
        {  hard, "R"}
    });
    Legroute_options options;
    options.clock_charge = false;
    const auto d         = lr::build_design(f.tech, f.fp, f.timing, options, nullptr);
    const auto g         = lr::build_grid(d, {});
    const auto left      = lr::physical_pin_access(d, g, d.nets[0].pins[1]);
    const auto right     = lr::physical_pin_access(d, g, d.nets[1].pins[1]);
    EXPECT_NE(left.tile, right.tile);  // distinct macro pins must not collapse to its center
    const double width  = ph::swaps_axes(orientation) ? 1000 : 2000;
    const double height = ph::swaps_axes(orientation) ? 2000 : 1000;
    const int    center = g.tile_of(4000 + width / 2, 1500 + height / 2);
    for (size_t layer = 0; layer < g.layers.size(); ++layer) {
      EXPECT_EQ(g.cap[layer][size_t(center)], 0) << "rotated macro interior acquired routing capacity";
    }
    if (!ph::swaps_axes(orientation)) {
      EXPECT_TRUE(left.x < 4000 || left.x >= 4000 + width);
      EXPECT_TRUE(right.x < 4000 || right.x >= 4000 + width);
      EXPECT_NE(left.tile, center);
      EXPECT_NE(right.tile, center);
      if (orientation == ph::Orientation::N) {
        EXPECT_DOUBLE_EQ(left.y, 1625);
        EXPECT_DOUBLE_EQ(right.y, 2125);
        const auto plan = legroute_plan(f.tech, f.fp, f.timing, options);
        EXPECT_EQ(plan.unrouted, 0u);
        EXPECT_EQ(plan.overflow_tracks, 0u);
      }
    } else {
      // Rotating an M4 pin onto a horizontal side does not create a vertical
      // escape on that horizontal routing layer. Keep its actual pin tile.
      const auto rect = ph::placed_rect(macro, f.fp.components[hard].place, f.fp.dbu, macro.pins.at("L").shapes.front().rect);
      EXPECT_DOUBLE_EQ(left.x, (rect.xl + rect.xh) / 2);
      EXPECT_DOUBLE_EQ(left.y, (rect.yl + rect.yh) / 2);
    }
    const lr::Pin interior{int(hard), -1, &macro.pins.at("I"), "I"};
    const auto    inside = lr::physical_pin_access(d, g, interior);
    const auto    rect   = ph::placed_rect(macro, f.fp.components[hard].place, f.fp.dbu, macro.pins.at("I").shapes.front().rect);
    EXPECT_DOUBLE_EQ(inside.x, (rect.xl + rect.xh) / 2);
    EXPECT_DOUBLE_EQ(inside.y, (rect.yl + rect.yh) / 2);
  }
}

TEST(LegrouteGrid, MacroBoundaryAccessCannotCrossAnExternalObstruction) {
  Fixture f;
  f.cell("dff", 0, 0);
  auto& macro                        = f.tech.macros["ram"];
  macro.width                        = 2;
  macro.height                       = 1;
  macro.klass                        = "BLOCK";
  macro.blocked_layers               = {"M3", "M4", "M5", "M6"};
  macro.pins["L"]                    = {"INPUT", "SIGNAL", {{"M4", {0, .1, .1, .15}}}};
  const auto hard                    = f.cell("ram", 4000, 1500);
  f.fp.components[hard].place.status = ph::Status::FIXED;
  Legroute_options options;
  const auto       d = lr::build_design(f.tech, f.fp, f.timing, options, nullptr);
  Legroute_power   power;
  power.obstacles.push_back({
      "M4",
      {3800, 1600, 3900, 1650}
  });
  const auto    g = lr::build_grid(d, power);
  const lr::Pin pin{int(hard), -1, &macro.pins.at("L"), "L"};
  const auto    access = lr::physical_pin_access(d, g, pin);
  EXPECT_DOUBLE_EQ(access.x, 4050);
  EXPECT_DOUBLE_EQ(access.y, 1625);  // retain the blocked pin, do not jump the obstruction
  EXPECT_EQ(access.tile, g.tile_of(4050, 1625));
}

TEST(LegrouteGrid, CriticalityFollowsTheSlackWindowAndSkipsClockPowerAndResetNets) {
  Fixture    f(64, 4);
  const auto ff = f.cell("dff", 0, 0);
  const auto a = f.cell("inv", 800, 0), b = f.cell("inv", 1600, 0);
  f.net(
      {
          {ff, "Q"},
          { a, "A"}
  },
      -.1);  // the worst slack (ns)
  f.net(
      {
          {a, "Y"},
          {b, "A"}
  },
      0);  // 100 ps above it: half of the default 200 ps window
  f.net(
      {
          { b,   "Y"},
          {ff, "CLK"}
  },
      -.5,
      "CLOCK");
  Legroute_options options;
  const auto       d = lr::build_design(f.tech, f.fp, f.timing, options, nullptr);
  ASSERT_EQ(d.nets.size(), 3u);
  EXPECT_TRUE(d.nets[0].routed);
  EXPECT_DOUBLE_EQ(d.nets[0].crit, 1);
  EXPECT_DOUBLE_EQ(d.nets[1].crit, .5);
  EXPECT_FALSE(d.nets[2].routed);  // a clock net is charged, not routed
  EXPECT_TRUE(d.nets[2].clock);
  EXPECT_DOUBLE_EQ(d.nets[2].crit, 0);
  EXPECT_EQ(d.flop_w, 400);
}
}  // namespace livehd::placement
