// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "legroute.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <set>

#include "gtest/gtest.h"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/routing_occupancy.hpp"
#include "legroute_internal.hpp"

namespace livehd::placement {
namespace {
namespace ph = livehd::physical;
// Fake technology: 50 x 250 DBU sites (dbu 1000), M1 pins, M2 box layer, M3/M5 vertical and M4/M6 horizontal routing.
struct Fixture {
  ph::Technology   tech;
  ph::Floorplan    fp;
  timing::Feedback timing;
  Fixture(int sites = 200, int rows = 16) {
    tech.dbu = fp.dbu       = 1000;
    tech.manufacturing_grid = .001;
    tech.sites["site"]      = {.05, .25, true};
    const auto layer        = [&](const std::string& name, bool h, double pitch) {
      auto& l     = tech.layers[name];
      l.width     = .02;
      l.direction = h ? "HORIZONTAL" : "VERTICAL";
      l.pitch_x = l.pitch_y = pitch;
      l.offset_x = l.offset_y = pitch / 2;
      l.has_offset            = true;
      l.rules.loaded          = true;
      l.rules.spacing         = {
          {.02, {}, {}}
      };
      tech.routing_layers.push_back(name);
      const auto step = int64_t(pitch * 1000), across = h ? int64_t(rows) * 250 : int64_t(sites) * 50;
      fp.tracks.push_back({h ? "Y" : "X", name, step / 2, across / step, step});
    };
    layer("M1", false, .05);
    layer("M2", true, .05);
    layer("M3", false, .05);
    layer("M4", true, .05);
    layer("M5", false, .1);
    layer("M6", true, .1);
    for (int k = 1; k < 6; ++k) {
      const auto lo = "M" + std::to_string(k), hi = "M" + std::to_string(k + 1), cut = "V" + std::to_string(k);
      tech.cut_layers[cut].width   = .02;
      tech.cut_layers[cut].spacing = {.02};
      auto& v                      = tech.vias[cut + "_DEF"];
      v.is_default                 = true;
      v.shapes                     = {
          { lo, {-.01, -.02, .01, .02}},
          {cut, {-.01, -.01, .01, .01}},
          { hi, {-.02, -.01, .02, .01}}
      };
      if (k % 2 == 0) {  // a horizontal lower metal: its landing runs along x
        v.shapes[0].rect = {-.02, -.01, .02, .01};
        v.shapes[2].rect = {-.01, -.02, .01, .02};
      }
    }
    const auto master = [&](const std::string& name, double w, std::vector<std::pair<std::string, double>> pins, bool flop) {
      auto& m  = tech.macros[name];
      m.width  = w;
      m.height = .25;
      m.klass  = "CORE";
      m.site   = "site";
      for (const auto& [pin, x] : pins) {
        const bool out = pin == "Y" || pin == "Q";
        m.pins[pin]    = {out ? "OUTPUT" : "INPUT", flop && pin == "CLK" ? "CLOCK" : "SIGNAL", {{"M1", {x, .05, x + .02, .2}}}};
      }
    };
    master("inv",
           .1,
           {
               {"A", .02},
               {"Y", .06}
    },
           false);
    master("nand",
           .15,
           {
               {"A", .02},
               {"B", .06},
               {"Y", .11}
    },
           false);
    master("dff",
           .4,
           {
               {  "D", .02},
               {"CLK", .14},
               {  "Q", .34}
    },
           true);
    fp.die = {0, 0, double(sites * 50), double(rows * 250)};
    for (int y = 0; y < rows; ++y) {
      fp.rows.push_back({"r" + std::to_string(y), "site", 0, y * 250, sites, 50, y % 2 ? ph::Orientation::FS : ph::Orientation::N});
    }
  }
  // These SAT/cut/spill fixtures need the horizontal supply historically attributed to M2 transit. Provide it
  // on a real upper layer instead: M2 remains exclusively available for in-box wiring. The synthetic stack may
  // have adjacent horizontal layers; their default via and both physical track lattices are explicit.
  void add_horizontal_transit_layer(int64_t pitch_dbu = 50) {
    auto& layer = tech.layers["M7"] = tech.layers.at("M2");
    layer.pitch_x = layer.pitch_y = double(pitch_dbu) / tech.dbu;
    layer.offset_x = layer.offset_y = double(pitch_dbu / 2) / tech.dbu;
    tech.routing_layers.push_back("M7");
    auto tracks  = *std::ranges::find(fp.tracks, std::string("M2"), &ph::Track::layer);
    tracks.layer = "M7";
    tracks.start = pitch_dbu / 2;
    tracks.step  = pitch_dbu;
    tracks.count = (int64_t(fp.die.yh) - 1 - tracks.start) / tracks.step + 1;
    fp.tracks.push_back(std::move(tracks));
    tech.cut_layers["V6"].width   = .02;
    tech.cut_layers["V6"].spacing = {.02};
    auto& via                     = tech.vias["V6_DEF"];
    via.is_default                = true;
    via.shapes                    = {
        {"M6", {-.02, -.01, .02, .01}},
        {"V6", {-.01, -.01, .01, .01}},
        {"M7", {-.02, -.01, .02, .01}}
    };
  }
  size_t cell(const std::string& master, int64_t x, int64_t y) {
    const auto& m = tech.macros.at(master);
    Ann_place   p(double(x), double(y), m.width * 1000, m.height * 1000);
    p.status     = ph::Status::PLACED;
    const auto i = fp.components.size();
    fp.components.push_back({"c" + std::to_string(i), master, p});
    return i;
  }
  void net(std::vector<std::pair<size_t, std::string>> pins, double slack = 1, std::string use = "") {
    ph::Net n{"n" + std::to_string(fp.nets.size()), {}};
    for (const auto& [c, p] : pins) {
      n.connections.push_back({fp.components[c].name, p});
      timing.pins[fp.components[c].name + ":" + p].slack = slack;
    }
    n.use = std::move(use);
    fp.nets.push_back(std::move(n));
  }
  // A clocked design: chains of inverters and nands between flops, scattered by a fixed seed.
  void random_design(int flops, int chain, unsigned seed = 1) {
    std::mt19937                       rng(seed);
    std::uniform_int_distribution<int> xs(0, int(fp.die.xh) - 500), ys(0, int(fp.rows.size()) - 1);
    const auto                         at = [&](const std::string& m) { return cell(m, xs(rng), ys(rng) * 250); };
    std::vector<size_t>                ff;
    for (int k = 0; k < flops; ++k) {
      ff.push_back(at("dff"));
    }
    std::vector<std::pair<size_t, std::string>> clk;
    for (auto f : ff) {
      clk.emplace_back(f, "CLK");
    }
    std::vector<size_t>                                      q_net(flops);    // each flop's Q net
    std::vector<std::vector<std::pair<size_t, std::string>>> q_sinks(flops);  // nand B inputs on another flop's Q
    for (int k = 0; k < flops; ++k) {
      size_t      prev = ff[k];
      std::string out  = "Q";
      for (int j = 0; j < chain; ++j) {
        const bool   two = j % 3 == 1;
        const size_t c   = at(two ? "nand" : "inv");
        if (j == 0) {
          q_net[k] = fp.nets.size();
        }
        net(
            {
                {prev, out},
                {   c, "A"}
        },
            -10.0 * (k % 4) - j);
        if (two) {
          q_sinks[(k + 1) % flops].emplace_back(c, "B");
        }
        prev = c;
        out  = "Y";
      }
      net(
          {
              {               prev, out},
              {ff[(k + 1) % flops], "D"}
      },
          -30.0 * (k % 4));
    }
    for (int k = 0; k < flops; ++k) {
      for (const auto& [c, p] : q_sinks[k]) {
        fp.nets[q_net[k]].connections.push_back({fp.components[c].name, p});
        timing.pins[fp.components[c].name + ":" + p].slack = 5;
      }
    }
    fp.terminals.push_back({"clk", "clk", "INPUT", 0, 2000, ph::Orientation::N, ph::Status::FIXED, {{"M4", {-10, -10, 10, 10}}}});
    ph::Net n{"clk", {{"PIN", "clk"}}};
    for (const auto& [c, p] : clk) {
      n.connections.push_back({fp.components[c].name, p});
    }
    n.use = "CLOCK";
    fp.nets.push_back(std::move(n));
  }
  // the router's policy: M1 pins escape on M2 through V1, M3 branches, M2..M5 signal metal
  static ph::Tech_profile profile() {
    ph::Tech_profile p;
    p.route.access        = {"M1", "M2", "V1"};
    p.route.branch_layer  = "M3";
    p.route.branch_cut    = "V2";
    p.route.signal_layers = {"M2", "M3", "M4", "M5", "M6"};
    return p;
  }
  void expect_legal() const {
    std::map<int64_t, std::vector<std::pair<int64_t, int64_t>>> rows;
    for (const auto& c : fp.components) {
      const auto& p = c.place;
      EXPECT_EQ(p.x % 50, 0) << c.name;
      EXPECT_EQ(p.y % 250, 0) << c.name;
      EXPECT_GE(p.x, 0);
      EXPECT_LE(p.x + std::llround(tech.macros.at(c.macro).width * 1000), int64_t(fp.die.xh)) << c.name;
      EXPECT_EQ(p.orientation, (p.y / 250) % 2 ? ph::Orientation::FS : ph::Orientation::N) << c.name;
      rows[p.y].emplace_back(p.x, p.x + std::llround(tech.macros.at(c.macro).width * 1000));
    }
    for (auto& [y, v] : rows) {
      std::ranges::sort(v);
      for (size_t k = 1; k < v.size(); ++k) {
        EXPECT_LE(v[k - 1].second, v[k].first) << "overlap in row " << y;
      }
    }
  }
};
}  // namespace

TEST(Legroute, LocalJoinProbeCommitsOnlyCompleteTrialNetsWithoutChangingInput) {
  for (int mode = 0; mode < 3; ++mode) {
    SCOPED_TRACE(mode);
    Fixture f;
    f.fp.design  = "local_join_probe";
    const auto a = f.cell("inv", 0, 0), b = f.cell("inv", 500, 0);
    f.net({
        {a, "Y"},
        {b, "A"}
    });
    Legroute_options options;
    options.final                      = false;
    const auto                 profile = Fixture::profile();
    auto                       design  = lr::build_design(f.tech, f.fp, f.timing, options, &profile);
    const auto                 grid    = lr::build_grid(design, {});
    lr::Final_planned_geometry planned;
    planned.paths.resize(1);
    planned.path_state.resize(1);
    planned.joins.resize(1);
    planned.io_stub_failure.resize(1);
    planned.paths[0] = {
        ph::flush_wire_path({"M3", {215, 125, 235, 325}},
        false),
        ph::flush_wire_path({"M4", {125, 215, 325, 235}},
        true)
    };
    planned.joins[0] = {
        {"M3", "M4", 225, 225}
    };
    if (mode == 1) {
      planned.joins[0].emplace_back("M3", "M4", 725, 225);  // Missing retained endpoint after one feasible join.
    }
    auto work = f.fp;
    work.nets[0].wires.push_back({"ROUTED", planned.paths[0]});
    const auto saved_wires = work.nets[0].wires;
    const auto occupancy   = ph::routing_occupancy(f.tech, work, work.dbu);
    const auto occupied    = occupancy.size();
    const auto directory   = "local_join_probe_" + std::to_string(mode);
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    if (mode != 0) {
      std::ofstream(directory + "/local_backbone_join_probe.def") << "stale successful witness\n";
    }
    const auto deadline = lr::Clock::now() + std::chrono::milliseconds(mode == 2 ? 0 : 1000);
    lr::write_local_join_probe(design, grid, planned, work, occupancy, directory, deadline);
    EXPECT_EQ(work.nets[0].wires, saved_wires);
    EXPECT_EQ(occupancy.size(), occupied);
    EXPECT_TRUE(f.fp.nets[0].wires.empty());
    std::ifstream     input(directory + "/local_backbone_join_probe.json");
    const std::string report{std::istreambuf_iterator<char>(input), {}};
    EXPECT_NE(report.find("\"physical_certificate\":false"), std::string::npos);
    EXPECT_NE(report.find(mode == 0 ? "\"all_join_requests_witnessed\":true" : "\"all_join_requests_witnessed\":false"),
              std::string::npos)
        << report;
    EXPECT_EQ(std::filesystem::exists(directory + "/local_backbone_join_probe.def"), mode == 0);
    if (mode == 1) {
      EXPECT_NE(report.find("\"witnessed_before_rollback\":1"), std::string::npos) << report;
      EXPECT_NE(report.find("\"accepted_joins\":0"), std::string::npos) << report;
    }
    if (mode == 2) {
      EXPECT_NE(report.find("\"timed_out\":true"), std::string::npos) << report;
    }
    std::filesystem::remove_all(directory);
  }
}

TEST(Legroute, FinalNetScopeCountsOnlyRequiredExcludedNetsWithoutChangingWires) {
  Fixture          f;
  Legroute_options opt;
  lr::Design       d{f.tech, f.fp, opt};
  const auto       add = [&](const std::string& name, bool routed, bool clock, bool power, size_t pins) {
    const auto frame = f.fp.nets.size();
    f.fp.nets.push_back({name, {}});
    lr::Net net;
    net.frame  = frame;
    net.routed = routed;
    net.clock  = clock;
    net.power  = power;
    net.pins.resize(pins);
    d.nets.push_back(std::move(net));
  };
  add("top/data", true, false, false, 2);
  add("CLK_MAIN", false, true, false, 2);
  add("top/SoFt_ReSeT_N", true, false, false, 2);
  add("top/reset_clk", false, true, false, 2);
  add("reset_vdd", false, false, true, 2);
  add("reset_unused", false, false, false, 1);
  add("reset_unused_clk", false, true, false, 1);
  f.fp.nets[0].wires.push_back({"ROUTED", {}});
  const auto original = f.fp.nets;
  const auto all      = lr::final_net_scope(d);
  EXPECT_EQ(all.required, (std::vector<char>{1, 1, 1, 1, 0, 0, 0}));
  EXPECT_EQ(all.skipped, 0u);

  opt.final_skip_clock = true;
  opt.final_skip_nets  = "reset";  // Case-insensitive substring search, including one clock already excluded above.
  const auto ordinary  = lr::final_net_scope(d);
  EXPECT_EQ(ordinary.required, (std::vector<char>{1, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(ordinary.skipped, 3u);  // Count the overlap once; power and single-pin nets were never required.
  opt.final_skip_clock = false;
  const auto named     = lr::final_net_scope(d);
  EXPECT_EQ(named.required, (std::vector<char>{1, 1, 0, 0, 0, 0, 0}));
  EXPECT_EQ(named.skipped, 2u);
  EXPECT_EQ(f.fp.nets, original);
}

TEST(Legroute, PairedPinPlanAndLocalResolveIdenticalFixedBoxBudgets) {
  Fixture    f(32, 2);
  const auto a = f.cell("dff", 0, 0), b = f.cell("dff", 800, 250);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  Legroute_options opt;
  opt.threads     = 1;
  opt.clock_spine = opt.region_pins = opt.final = false;
  opt.legal_boxes = opt.global_route = opt.global_all = opt.global_keep = true;
  opt.reserved_tracks                                                   = 0;
  opt.spill_rounds                                                      = 0;
  opt.box_reserve        = "M2:100,M3:100";  // The experimental policy replaces, rather than adds, the uniform control.
  opt.paired_pin_reserve = true;
  opt.output_dir         = (std::filesystem::path(testing::TempDir()) / "paired_fixed_budget").string();
  const auto original    = f.fp;
  const auto plan        = legroute_plan(f.tech, f.fp, f.timing, opt);
  ASSERT_FALSE(plan.gates.requires_replan());
  EXPECT_EQ(plan.endpoint_basis, "box_centroids");
  const auto report = legroute(f.tech, f.fp, f.timing, opt);
  EXPECT_EQ(report.sat_unsat_regions + report.sat_unknown_regions, 0u);
  EXPECT_EQ(report.unassigned_segments, 0u);
  EXPECT_EQ(report.cellbox_allowance_short_tiles, 0u);
  EXPECT_EQ(report.route_passes, 0u);
  EXPECT_EQ(f.fp.components, original.components);
  EXPECT_EQ(f.fp.nets, original.nets);
  const auto read = [](const std::string& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << path;
    return std::string(std::istreambuf_iterator<char>(input), {});
  };
  EXPECT_EQ(read(opt.output_dir + "/plan_paired_pin_reservations.json"),
            read(opt.output_dir + "/local_paired_pin_reservations.json"));
  std::filesystem::remove_all(opt.output_dir);
}

TEST(Legroute, PairedPinMissingSecondRowSupplyStopsBeforeGlobalAndSat) {
  Fixture    f(32, 2);
  const auto a = f.cell("dff", 0, 0), b = f.cell("dff", 800, 0);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  for (auto& track : f.fp.tracks) {
    if (track.layer == "M2") {
      track.count = 5;  // Five real tracks, all in the bottom row: total supply alone is insufficient.
    }
  }
  Legroute_options opt;
  opt.clock_spine = opt.region_pins = opt.final = false;
  opt.legal_boxes = opt.global_route = opt.global_all = opt.global_keep = true;
  opt.reserved_tracks                                                   = 0;
  opt.paired_pin_reserve                                                = true;
  const auto original                                                   = f.fp;
  const auto plan                                                       = legroute_plan(f.tech, f.fp, f.timing, opt);
  EXPECT_FALSE(plan.route_attempted);
  EXPECT_EQ(plan.route_skip_reason, Legroute_plan_report::Route_skip_reason::paired_pin_supply);
  EXPECT_TRUE(plan.gates.requires_replan());
  EXPECT_GT(plan.gates.unfittable_boxes, 0u);
  EXPECT_THROW(legroute(f.tech, f.fp, f.timing, opt), Legroute_replan);
  EXPECT_EQ(f.fp.components, original.components);
  EXPECT_EQ(f.fp.nets, original.nets);
  opt.legal_window = 1;
  EXPECT_THROW(validate_legroute_options(opt), std::invalid_argument);
  opt.legal_window = 0;
  opt.cap_scale    = .1;
  EXPECT_THROW(validate_legroute_options(opt), std::invalid_argument);
  opt.global_share = .1;
  EXPECT_NO_THROW(validate_legroute_options(opt));
}

TEST(Legroute, PackSegmentsWidestFirstIntoRoomiestSegment) {
  EXPECT_EQ(lr::pack_segments({3, 2, 1}, {4, 3}), (std::vector<int>{0, 1, 0}));
  EXPECT_EQ(lr::pack_segments({3, 2, 2}, {4, 3}), (std::vector<int>{1, 0, 0}));  // exact fallback: {3} + {2, 2}
  EXPECT_EQ(lr::pack_segments({3, 3, 2, 2, 2}, {6, 6}).size(), 5u);              // 3+3 / 2+2+2
  EXPECT_TRUE(lr::pack_segments({4, 4, 1}, {4, 4}).empty());                     // genuinely impossible
  EXPECT_TRUE(lr::pack_segments({3, 3}, {4, 2}).empty());
  const std::vector<int> prefer{1, -1};
  EXPECT_EQ(lr::pack_segments({2, 1}, {4, 3}, &prefer), (std::vector<int>{1, 0}));
}

TEST(Legroute, TileGridCapacityStripesMacrosAndRegions) {
  Fixture f;
  f.cell("dff", 0, 0);
  // a fixed obstacle macro over the right part of rows 4..7, and an M4 power stripe at y = 2025
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
  Legroute_options opt;
  const auto       d = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  Legroute_regions regions;
  regions.rects = {
      {   0,    0,  4000, 4000},
      {4000,    0, 10000, 2000},
      {4000, 2000, 10000, 4000}
  };
  regions.rect_region = {0, 1, 0};  // region 0 is L-shaped
  auto g              = lr::build_grid(d, regions);
  EXPECT_EQ(g.tile_w, 800);  // two flops wide
  EXPECT_EQ(g.tile_h, 500);  // two rows
  EXPECT_EQ(g.cols, 13);
  EXPECT_EQ(g.rows, 8);
  EXPECT_EQ(g.regions, 2);
  EXPECT_EQ(g.region[g.tile(0, 0)], 0);
  EXPECT_EQ(g.region[g.tile(7, 1)], 1);
  EXPECT_EQ(g.region[g.tile(7, 6)], 0);  // the L's arm
  // macro tiles are blocked for boxes and lose the macro's layers
  EXPECT_TRUE(g.blocked[g.tile(10, 2)]);
  EXPECT_FALSE(g.blocked[g.tile(2, 2)]);
  int m3 = -1, m4 = -1;
  for (int l = 0; l < int(g.layers.size()); ++l) {
    m3 = g.layers[l].name == "M3" ? l : m3;
    m4 = g.layers[l].name == "M4" ? l : m4;
  }
  ASSERT_GE(m3, 0);
  ASSERT_GE(m4, 0);
  EXPECT_EQ(g.cap[m3][g.tile(10, 2)], 0);
  EXPECT_EQ(g.cap[m3][g.tile(2, 2)], 16 - 1);  // 800 / 50 tracks, one reserved
  // The stripe takes the next M4 track after the reserved one (10 tracks, one reserved, one distinct stripe).
  EXPECT_EQ(g.cap[m4][g.tile(2, 4)], 10 - 1 - 1);
  EXPECT_EQ(g.cap[m4][g.tile(2, 3)], 10 - 1);
}

TEST(Legroute, CellboxBudgetComesOffUsableTracksPerLayerAndShortTilesHostNoBox) {
  Fixture f;
  f.cell("dff", 0, 0);
  Legroute_options opt;
  Legroute_regions regions;
  regions.rects = {
      {0, 0, 10000, 4000}
  };
  regions.rect_region = {0};
  const auto base_d   = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  const auto base     = lr::build_grid(base_d, regions);
  opt.box_reserve     = "M2:1,M3:2";
  const auto d        = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  const auto g        = lr::build_grid(d, regions);  // finalize() checks catalog parity with the reduced counts
  int        m3       = -1;
  for (int l = 0; l < int(g.layers.size()); ++l) {
    m3 = g.layers[l].name == "M3" ? l : m3;
  }
  ASSERT_GE(m3, 0);
  const int t = g.tile(2, 2);
  EXPECT_EQ(g.cap[m3][t], base.cap[m3][t] - 2);  // layer identity: only M3 loses M3's share
  EXPECT_EQ(g.resources.layers[size_t(m3)].tiles[size_t(t)].cellbox_budget, 2u);
  if (g.box_layer == "M2") {
    EXPECT_EQ(g.box_cap[t], base.box_cap[t] - 1);
  }
  EXPECT_FALSE(g.box_short[t]);
  // a budget no tile can keep: every free tile is short and hosts no box
  opt.box_reserve    = "M3:1000";
  const auto d_short = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  const auto g_short = lr::build_grid(d_short, regions);
  EXPECT_TRUE(g_short.box_short[t]);
  EXPECT_EQ(g_short.cap[m3][t], 0);
  lr::Box box;
  box.cells = {0};
  box.sites = 1;
  EXPECT_FALSE(lr::box_fits(d_short, g_short, box, t));
  EXPECT_TRUE(lr::box_fits(d, g, box, t));
  box.placed = true;  // even a box already placed at that home cannot keep a promise the tile cannot hold
  box.cx     = double(g_short.cx(g_short.col(t)));
  box.cy     = double(g_short.cy(g_short.row(t)));
  EXPECT_FALSE(lr::box_fits(d_short, g_short, box, t));
  opt.box_reserve  = "M3";
  const auto d_bad = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  EXPECT_THROW(lr::build_grid(d_bad, regions), std::invalid_argument);
}

TEST(Legroute, PhysicalPowerTracksAreDeduplicatedAgainstEachOtherAndReservations) {
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
  Legroute_regions resources;
  resources.obstacles = {
      {"M4",  {800, 1065, 2400, 1085}}, // same physical track, partial overlapping span
      {"M4",    {0, 1015, 3200, 1035}}, // already excluded first reserved track in this band
      {"M4", {1240, 1000, 1260, 1500}}  // perpendicular metal remains a detailed obstacle
  };
  Legroute_options options;
  const auto       design = lr::build_design(f.tech, f.fp, f.timing, options, nullptr);
  auto             grid   = lr::build_grid(design, resources);
  const auto       layer  = size_t(std::ranges::find(grid.layers, "M4", &lr::Layer::name) - grid.layers.begin());
  ASSERT_LT(layer, grid.layers.size());
  const int tile           = grid.tile(1, 2);
  const auto [first, last] = grid.tracks(int(layer), 2);
  EXPECT_EQ(last - first + 1, 10);
  EXPECT_EQ(grid.cap[layer][tile], 8);          // one generic physical track and one different PG track
  ASSERT_EQ(grid.obstacles[layer].size(), 5u);  // no obstacle was discarded from detailed routing
  EXPECT_TRUE(std::ranges::any_of(grid.obstacles[layer],
                                  [](const ph::Rect& r) { return r.xl == 1240 && r.xh == 1260 && r.yl == 1000 && r.yh == 1500; }));
  options.reserved_layers = "M3";  // disabling the M4 heuristic must retain both physical PG tracks
  grid                    = lr::build_grid(design, resources);
  EXPECT_EQ(grid.cap[layer][tile], 8);
  EXPECT_EQ(grid.obstacles[layer].size(), 5u);
  options.reserved_layers.clear();
  resources.obstacles.push_back({
      "M4",
      {0, 1115, 3200, 1135}
  });
  grid = lr::build_grid(design, resources);
  EXPECT_EQ(grid.cap[layer][tile], 7);  // a genuinely different physical track is still deducted
}

TEST(Legroute, DensityReservationsUsePhysicalLengthAcrossUnequalBandsAndOrigins) {
  Fixture          f;
  Legroute_options options;
  options.reserved_tracks        = 7;  // overridden by density mode
  options.reserved_tracks_per_um = 1;
  const lr::Track_reservations policy(options, f.tech, 1000);
  for (int64_t offset : {int64_t(0), int64_t(25)}) {
    int h = 0, v = 0;
    for (int64_t k = -40; k < 40; ++k) {
      const auto coordinate = offset + 50 * k;
      const auto h_ordinal  = (k + 40) % 10;  // 0.5um H bands
      const auto v_ordinal  = (k + 40) % 40;  // 2um V bands
      const bool rh         = policy.selected("M4", coordinate, coordinate - 50, h_ordinal);
      const bool rv         = policy.selected("M3", coordinate, coordinate - 50, v_ordinal);
      EXPECT_EQ(rh, rv);
      h += rh;
      v += rv;
    }
    EXPECT_EQ(h, 4);
    EXPECT_EQ(v, 4);  // equal 4um physical spans, including negative coordinates
  }
  EXPECT_TRUE(policy.selected("M4", 1000, 950, 9));
  EXPECT_TRUE(policy.selected("M4", -1000, -1050, 9));
  EXPECT_FALSE(policy.selected("M4", 1050, 1000, 0));  // no reset at a tile edge
  options.reserved_tracks_per_um = 0;
  EXPECT_FALSE(lr::Track_reservations(options, f.tech, 1000).selected("M4", 0, -50, 0));
  options.reserved_tracks_per_um = 40;
  EXPECT_TRUE(lr::Track_reservations(options, f.tech, 1000).selected("M3", 75, 25, 5));
  options.reserved_tracks_per_um = -1;
  options.reserved_tracks        = 1;
  options.reserved_layers        = " M2, M4 ";
  const lr::Track_reservations legacy(options, f.tech, 1000);
  EXPECT_TRUE(legacy.selected("M4", 1050, 1000, 0));
  EXPECT_FALSE(legacy.selected("M4", 1100, 1050, 1));
  EXPECT_FALSE(legacy.selected("M3", 1050, 1000, 0));
  f.tech.layers["V1"] = {};  // a known non-routing layer must not silently disable the reservation
  for (const auto* bad : {"M9", "V1", "M2,", ",M2", "M2,,M4", " "}) {
    options.reserved_layers = bad;
    EXPECT_THROW((lr::Track_reservations(options, f.tech, 1000)), std::invalid_argument) << bad;
  }
}

TEST(Legroute, DensityM2ReservationsUseUniquePatternPredecessorsAndRailUnion) {
  for (int64_t origin : {int64_t(-1000), int64_t(0), int64_t(1000)}) {
    SCOPED_TRACE(origin);
    Fixture f(64, 8);
    f.cell("dff", 0, origin);
    for (auto& row : f.fp.rows) {
      row.y += origin;
    }
    f.fp.die.yl += origin;
    f.fp.die.yh += origin;
    std::erase_if(f.fp.tracks, [](const auto& track) { return track.layer == "M2"; });
    // The first finite coordinate25 has the virtual predecessor-25 of its
    // originating50-DBU lattice. Other predecessors come from the unique union.
    f.fp.tracks.push_back({"Y", "M2", origin + 25, 40, 50});
    f.fp.tracks.push_back({"Y", "M2", origin + 50, 20, 100});
    f.fp.tracks.push_back({"Y", "M2", origin + 50, 20, 100});
    Legroute_options options;
    options.reserved_tracks_per_um = 1;
    options.reserved_layers        = "M2";
    const auto design              = lr::build_design(f.tech, f.fp, f.timing, options, nullptr);
    auto       grid                = lr::build_grid(design, {});
    EXPECT_EQ(grid.box_cap[grid.tile(1, 0)], 14);  // 15 unique tracks, reserve25
    EXPECT_EQ(grid.box_cap[grid.tile(1, 1)], 15);
    EXPECT_EQ(grid.box_cap[grid.tile(1, 2)], 14);  // reserve1025, never1050 too
    EXPECT_EQ(grid.box_cap[grid.tile(1, 3)], 15);
    Legroute_regions resources;
    const ph::Shape  rail{
        "M2",
        {0, double(origin + 15), 3200, double(origin + 35)}
    };
    resources.obstacles = {rail, rail};
    grid                = lr::build_grid(design, resources);
    EXPECT_EQ(grid.box_cap[grid.tile(1, 0)], 13);  // rail excludes25+50; reserved25 is not charged twice
    const auto m4     = size_t(std::ranges::find(grid.layers, "M4", &lr::Layer::name) - grid.layers.begin());
    const auto [a, b] = grid.tracks(int(m4), 1);
    EXPECT_EQ(grid.cap[m4][grid.tile(1, 1)], b - a + 1);  // whitelist applies to high layers too
  }
}

TEST(Legroute, BoxesMergeConnectedNeighborsAndStopAtTheCostCap) {
  Fixture    f;
  const auto a = f.cell("inv", 1000, 0), b = f.cell("inv", 1100, 0), far = f.cell("inv", 9000, 3750);
  f.net({
      {a, "Y"},
      {b, "A"}
  });
  f.net({
      {  b, "Y"},
      {far, "A"}
  });
  Legroute_options opt;
  const auto       d     = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto             g     = lr::build_grid(d, {});
  const auto       boxes = lr::build_boxes(d, g);
  EXPECT_EQ(boxes.box_of[d.cell[a]], boxes.box_of[d.cell[b]]);
  EXPECT_NE(boxes.box_of[d.cell[a]], boxes.box_of[d.cell[far]]);  // 100+ tile widths away: above the cost cap
}

TEST(Legroute, MacroTileSnap) {
  Fixture f;
  f.cell("dff", 0, 0);
  const auto t = legroute_tile(f.tech, f.fp);
  EXPECT_EQ(t.w, 800);
  EXPECT_EQ(t.h, 500);
  EXPECT_EQ(snap_to_tiles(t, 1234, 777, 0, 0), (std::pair<int64_t, int64_t>{1600, 1000}));
  EXPECT_EQ(snap_to_tiles(t, 1234, 777, 100, 100), (std::pair<int64_t, int64_t>{900, 600}));
}

TEST(Legroute, EndToEndLegalRoutedAndConnected) {
  for (bool two_regions : {false, true}) {
    Fixture f;
    f.random_design(12, 4);
    Legroute_options opt;
    opt.threads = 2;
    Legroute_regions regions;
    if (two_regions) {
      regions.rects = {
          {   0, 0,  5000, 4000},
          {5000, 0, 10000, 4000}
      };
    }
    Legroute_report r;
    const auto      profile = Fixture::profile();
    try {
      r = legroute(f.tech, f.fp, f.timing, opt, regions, &profile);
    } catch (const std::exception& e) {
      FAIL() << e.what() << " two regions " << two_regions;
    }
    f.expect_legal();
    EXPECT_EQ(r.cells, f.fp.components.size());
    EXPECT_GT(r.boxes, 0u);
    EXPECT_LT(r.boxes, r.cells);
    EXPECT_EQ(r.sat_regions, two_regions ? 2u : 1u);
    EXPECT_EQ(r.sat_unsat_regions + r.sat_unknown_regions, 0u);
    EXPECT_GT(r.connections, 0u);
    EXPECT_EQ(r.clock_domains, two_regions ? 2u : 1u);
    EXPECT_GT(r.routed_nets, 0u);
    // all or nothing: written wires are every routed net, connected (closure itself returns with the per-box
    // legroute.final of the SAT redesign)
    size_t wired = 0;
    for (const auto& n : f.fp.nets) {
      wired += !n.wires.empty();
      if (!n.wires.empty()) {
        const auto c = ph::check_route_connectivity(f.tech, f.fp, n);
        EXPECT_TRUE(c.connected) << n.name << ": " << c.reason << " (" << c.components << " components)";
      }
    }
    EXPECT_EQ(wired, r.wires_emitted ? r.routed_nets : 0u);
  }
}

TEST(Legroute, FinalClosureEmitsAndConnectsExistingClockTopology) {
  for (bool spines : {false, true}) {
    Fixture    f;
    const auto sink0 = f.cell("dff", 1000, 0);
    const auto sink1 = f.cell("dff", 2500, 500);
    f.fp.terminals.push_back(
        {"clk", "clk", "INPUT", 125, 1025, ph::Orientation::N, ph::Status::FIXED, {{"M4", {-10, -10, 10, 10}}}});
    // Classification must also work when LEF CLOCK pins identify the net without an explicit DEF USE CLOCK.
    f.fp.nets.push_back({
        "clk",
        {{"PIN", "clk"}, {f.fp.components[sink0].name, "CLK"}, {f.fp.components[sink1].name, "CLK"}}
    });
    Legroute_options opt;
    opt.threads        = 1;
    opt.clock_spine    = spines;
    const auto profile = Fixture::profile();
    const auto r       = legroute(f.tech, f.fp, f.timing, opt, {}, &profile);
    EXPECT_EQ(r.routed_nets, 1u) << "spines " << spines;
    EXPECT_EQ(r.clock_required_nets, 1u);
    EXPECT_EQ(r.connected_nets, 1u);
    EXPECT_EQ(r.clock_connected_nets, 1u);
    EXPECT_EQ(r.clock_failed_nets, 0u);
    EXPECT_EQ(r.clock_unchecked_nets, 0u);
    ASSERT_TRUE(r.wires_emitted) << "spines " << spines << " failures "
                                 << (r.failure_sample.empty() ? "" : r.failure_sample.back());
    ASSERT_FALSE(f.fp.nets[0].wires.empty());
    const auto connected = ph::check_route_connectivity(f.tech, f.fp, f.fp.nets[0]);
    EXPECT_TRUE(connected.connected) << connected.reason;
    EXPECT_EQ(r.clock_domains > 0, spines);
  }
}

TEST(Legroute, LegalBoxesRouteTheGivenPlacementUnmoved) {
  Fixture f;
  f.random_design(12, 4);
  // a legal placement by hand: each row's cells packed left to right in their x order, on sites
  std::map<int64_t, std::vector<size_t>> rows;
  for (size_t i = 0; i < f.fp.components.size(); ++i) {
    rows[f.fp.components[i].place.y].push_back(i);
  }
  for (auto& [y, cells] : rows) {
    std::ranges::sort(cells, [&](size_t a, size_t b) { return f.fp.components[a].place.x < f.fp.components[b].place.x; });
    int64_t x = 0;
    for (size_t i : cells) {
      auto& p       = f.fp.components[i].place;
      p.x           = std::max(x, p.x / 50 * 50);
      p.orientation = (y / 250) % 2 ? ph::Orientation::FS : ph::Orientation::N;
      x             = p.x + std::llround(f.tech.macros.at(f.fp.components[i].macro).width * 1000);
    }
  }
  f.expect_legal();
  const auto before = f.fp.components;
  for (unsigned window : {0u, 1u}) {  // fixed on their tiles; free to move a tile (the cells repacked where they land)
    f.fp.components = before;
    for (auto& n : f.fp.nets) {
      n.wires.clear();
    }
    Legroute_options opt;
    opt.threads      = 2;
    opt.legal_boxes  = true;
    opt.legal_window = window;
    opt.final        = false;  // legroute.final would move cells inside their boxes
    Legroute_regions regions;
    regions.rects = {
        {   0, 0,  5000, 4000},
        {5000, 0, 10000, 4000}
    };
    const auto      profile = Fixture::profile();
    Legroute_report r;
    try {
      r = legroute(f.tech, f.fp, f.timing, opt, regions, &profile);
    } catch (const std::exception& e) {
      FAIL() << e.what() << " window " << window;
    }
    EXPECT_GT(r.boxes, 0u);
    EXPECT_EQ(r.legal_boxes, r.boxes) << window;  // every box is a tile of the legal placement
    EXPECT_EQ(r.sat_regions, 2u);
    EXPECT_EQ(r.sat_unsat_regions + r.sat_unknown_regions, 0u) << window;
    EXPECT_GT(r.connections, 0u);
    ASSERT_EQ(f.fp.components.size(), before.size());
    f.expect_legal();
    if (window > 0) {
      continue;
    }
    EXPECT_EQ(r.displacement_um, 0) << "a fixed legal box is on its own tile";
    for (size_t i = 0; i < before.size(); ++i) {  // the cells stay where the legalization left them
      EXPECT_EQ(f.fp.components[i].place.x, before[i].place.x) << before[i].name;
      EXPECT_EQ(f.fp.components[i].place.y, before[i].place.y) << before[i].name;
    }
  }
}

TEST(Legroute, RouteDrivenPartitionKeepsPlacementAndIgnoresSeedMesh) {
  Fixture f;
  for (int row = 0; row < 4; ++row) {
    size_t prev = f.cell("inv", 200, row * 500);
    for (int col = 1; col < 8; ++col) {
      const auto next = f.cell("inv", col * 1000 + 200, row * 500);
      f.net({
          {prev, "Y"},
          {next, "A"}
      });
      prev = next;
    }
  }
  const auto       before = f.fp.components;
  Legroute_options opt;
  opt.threads            = 1;
  opt.clock_spine        = false;
  opt.legal_boxes        = true;
  opt.global_route       = true;
  opt.region_pins        = false;
  opt.global_share       = 1;
  opt.partition_cells    = 4;
  opt.partition_critical = 0;
  opt.final              = false;
  for (unsigned target : {0u, 4u}) {
    opt.partition_cells = target;
    opt.global_all      = target == 0;
    std::vector<int>             first_mesh;
    std::vector<lr::Global_tree> first_trees;
    for (int seeds : {1, 2}) {
      opt.partition_timeout_rounds = target && seeds == 2 ? 2 : 0;
      Legroute_regions regions;
      if (seeds == 2) {
        regions.rects = {
            {   0, 0,  5000, 4000},
            {5000, 0, 10000, 4000}
        };
      }
      auto                   d         = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
      auto                   g         = lr::build_grid(d, regions);
      auto                   boxes     = lr::build_boxes(d, g);
      const auto             seed_mesh = g.region;
      const std::vector<int> cap(g.size(), 10);
      const auto             all = lr::route_all_signals(d, g, boxes, cap, cap);
      EXPECT_EQ(all.unrouted, 0u);
      ASSERT_EQ(all.trees.size(), d.nets.size());
      for (const auto& t : all.trees) {
        EXPECT_TRUE(t.routed);
        EXPECT_FALSE(t.edges.empty()) << "even nets inside one seed region are routed";
      }
      if (seeds == 1) {
        first_trees = all.trees;
      } else {
        for (size_t n = 0; n < all.trees.size(); ++n) {
          EXPECT_EQ(all.trees[n].edges, first_trees[n].edges);
        }
      }
      lr::Routing     routes;
      Legroute_report rep;
      lr::sat_place(d, g, boxes, regions, routes, rep);
      if (target) {
        EXPECT_GT(rep.regions, 1u);
      } else {
        EXPECT_EQ(rep.regions, size_t(seeds));
        EXPECT_EQ(g.region, seed_mesh);
      }
      EXPECT_EQ(rep.sat_regions, rep.regions);
      EXPECT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
      EXPECT_EQ(rep.partition_route_calls, 1u);
      EXPECT_EQ(rep.partition_oversized, 0u);
      EXPECT_LE(rep.partition_max_cells, opt.partition_cells);
      EXPECT_EQ(rep.crossings_built, 1u);
      EXPECT_EQ(rep.partition_timeout_rounds, 0u);
      EXPECT_EQ(rep.partition_timeout_regions, 0u);
      if (seeds == 1) {
        first_mesh = g.region;
      } else {
        if (target) {
          EXPECT_EQ(first_mesh, g.region);
        }
      }
      for (const auto& b : boxes.boxes) {
        EXPECT_EQ(b.tile, g.tile_of(b.cx, b.cy));
        EXPECT_EQ(b.region, g.region[size_t(b.tile)]);
      }
      lr::pack_cells(d, g, boxes);
      for (size_t c = 0; c < before.size(); ++c) {
        EXPECT_EQ(before[c].place.x, f.fp.components[c].place.x);
        EXPECT_EQ(before[c].place.y, f.fp.components[c].place.y);
      }
    }
  }
}

TEST(Legroute, NonrectangularRegionsRouteWithoutMovingTheirCells) {
  Fixture             f(48, 4);
  std::vector<size_t> cells;
  for (int r = 0; r < 2; ++r) {
    for (int c = 0; c < 3; ++c) {
      cells.push_back(f.cell("dff", c * 800 + 100, r * 500));
    }
  }
  f.net({
      {cells[0], "Q"},
      {cells[1], "D"},
      {cells[3], "D"}
  });
  f.net({
      {cells[2], "Q"},
      {cells[4], "D"},
      {cells[5], "D"}
  });
  const auto       before = f.fp.components;
  Legroute_options opt;
  opt.threads            = 1;
  opt.clock_spine        = false;
  opt.legal_boxes        = true;
  opt.global_route       = true;
  opt.region_pins        = false;
  opt.global_share       = 1;
  opt.partition_cells    = 3;
  opt.partition_critical = 1;
  opt.partition_mesh     = true;
  opt.final              = false;
  Legroute_regions seed;
  auto             d = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto             g = lr::build_grid(d, seed);
  ASSERT_EQ(g.cols, 3);
  ASSERT_EQ(g.rows, 2);
  auto            boxes = lr::build_boxes(d, g);
  lr::Routing     routes;
  Legroute_report rep;
  lr::sat_place(d, g, boxes, seed, routes, rep);
  EXPECT_EQ(rep.regions, 2u);
  EXPECT_EQ(rep.sat_regions, 2u);
  EXPECT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
  EXPECT_EQ(rep.partition_max_cells, 3u);
  EXPECT_EQ(rep.partition_critical_crossings, 0u);
  EXPECT_EQ(rep.partition_route_calls, 1u);
  EXPECT_TRUE(lr::connected_regions(g, g.region));
  EXPECT_EQ(g.region[0], g.region[1]);
  EXPECT_EQ(g.region[0], g.region[3]);
  EXPECT_EQ(g.region[2], g.region[4]);
  EXPECT_EQ(g.region[2], g.region[5]);
  EXPECT_NE(g.region[0], g.region[2]);
  lr::pack_cells(d, g, boxes);
  EXPECT_EQ(f.fp.components, before);
}

TEST(Legroute, MovableRouteCutsSharePlanningBoxesAndConnectMovedEndpoints) {
  Fixture    f(48, 4);
  const auto a = f.cell("dff", 100, 0), b = f.cell("dff", 100, 0);  // deliberately overlapping
  const auto c = f.cell("dff", 100, 500), dff = f.cell("dff", 1700, 0);
  const auto e = f.cell("dff", 900, 500), ff = f.cell("dff", 1700, 500);
  const auto fixed                    = f.cell("inv", 950, 0);
  f.fp.components[fixed].place.status = ph::Status::FIXED;
  f.net(
      {
          {    a, "Q"},
          {    b, "D"},
          {    c, "D"},
          {fixed, "A"}
  },
      -10);
  f.net(
      {
          {dff, "Q"},
          {  e, "D"},
          { ff, "D"}
  },
      -9);
  f.net(
      {
          {  c, "Q"},
          {dff, "D"}
  },
      5);
  const auto       before = f.fp.components;
  Legroute_options opt;
  opt.threads            = 1;
  opt.clock_spine        = false;
  opt.legal_boxes        = false;
  opt.global_route       = true;
  opt.region_pins        = false;
  opt.box_fill           = .49;  // each flop remains a movable box
  opt.partition_cells    = 3;
  opt.partition_critical = .6;
  opt.partition_mesh     = true;
  opt.final              = false;
  Legroute_regions seed;
  seed.rects = {
      {0,   0, 2400,  500},
      {0, 500, 2400, 1000}
  };
  const auto plan = legroute_plan(f.tech, f.fp, f.timing, opt, seed);
  ASSERT_FALSE(plan.gates.requires_replan()) << legroute_gate_json(plan.gates);
  EXPECT_EQ(plan.endpoint_basis, "box_centroids");
  auto d = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto g = lr::build_grid(d, seed);
  ASSERT_EQ(g.regions, 1);  // seed boundaries do not change connectivity-box grouping
  auto boxes = lr::build_boxes(d, g);
  ASSERT_EQ(boxes.boxes.size(), 6u);
  const auto       initial_boxes = boxes;
  std::vector<int> cap_h(g.size()), cap_v(g.size());
  for (size_t l = 0; l < g.layers.size(); ++l) {
    for (size_t t = 0; t < g.size(); ++t) {
      (g.layers[l].horizontal ? cap_h : cap_v)[t] += g.cap[l][t];
    }
  }
  const auto      coarse = lr::route_all_signals(d, g, boxes, cap_h, cap_v);
  lr::Routing     routing;
  Legroute_report report;
  lr::sat_place(d, g, boxes, seed, routing, report);
  ASSERT_EQ(report.sat_unsat_regions + report.sat_unknown_regions, 0u);
  EXPECT_EQ(report.spilled_regions, 0u);
  EXPECT_EQ(report.partition_max_cells, 3u);
  EXPECT_EQ(report.global_search_pops, plan.search_pops);
  ASSERT_EQ(g.regions, 2);
  EXPECT_EQ(g.region[0], g.region[1]);
  EXPECT_EQ(g.region[0], g.region[3]);
  EXPECT_EQ(g.region[2], g.region[4]);
  EXPECT_EQ(g.region[2], g.region[5]);
  EXPECT_NE(g.region[0], g.region[2]);
  EXPECT_TRUE(lr::connected_regions(g, g.region));
  const auto local_screen = lr::assess_legroute_gates(d, g, &initial_boxes, coarse, {}, cap_h, cap_v, true);
  EXPECT_EQ(legroute_gate_json(local_screen), legroute_gate_json(plan.gates));
  size_t moved = 0;
  for (const auto& box : boxes.boxes) {
    moved += box.tile != g.tile_of(box.cx, box.cy);
    EXPECT_FALSE(box.fixed);
    EXPECT_EQ(g.region[size_t(box.tile)], box.region);
  }
  EXPECT_GT(moved, 0u);
  // The dynamic local tree must reach each moved box and the fixed pin. A
  // preserved coarse tree at the old overlapping homes would fail this check.
  const auto& tree = routing.trees[0];
  ASSERT_FALSE(tree.nodes.empty());
  std::vector<char> seen(tree.nodes.size());
  std::vector<int>  queue{0};
  seen[0] = 1;
  for (size_t at = 0; at < queue.size(); ++at) {
    for (int id : tree.conns) {
      const auto& edge = routing.conns[size_t(id)];
      const int   next = edge.a == queue[at] ? edge.b : edge.b == queue[at] ? edge.a : -1;
      if (next >= 0 && !seen[size_t(next)]) {
        seen[size_t(next)] = 1;
        queue.push_back(next);
      }
    }
  }
  for (const auto& pin : d.nets[0].pins) {
    const int cell    = pin.comp >= 0 ? d.cell[size_t(pin.comp)] : -1;
    const int tile    = cell >= 0 ? boxes.boxes[size_t(boxes.box_of[size_t(cell)])].tile : lr::physical_pin_access(d, g, pin).tile;
    bool      reached = false;
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
      reached |= seen[i] && tree.nodes[i].tile == tile;
    }
    EXPECT_TRUE(reached) << "pin tile " << tile;
  }
  lr::pack_cells(d, g, boxes);
  f.expect_legal();
  EXPECT_EQ(f.fp.components[fixed], before[fixed]);
  EXPECT_NE(std::pair(f.fp.components[a].place.x, f.fp.components[a].place.y),
            std::pair(f.fp.components[b].place.x, f.fp.components[b].place.y));
  opt.global_keep = true;
  EXPECT_THROW(validate_legroute_options(opt), std::invalid_argument);
  opt.global_keep     = false;
  opt.partition_cells = 0;
  opt.global_all      = true;
  EXPECT_THROW(validate_legroute_options(opt), std::invalid_argument);
}

TEST(Legroute, MovableRegionWithoutDistinctBoxRoomReturnsBeforeSat) {
  Fixture f(16, 2);  // one 800x500 tile, enough physical area for both flops
  f.cell("dff", 100, 0);
  f.cell("dff", 100, 0);
  Legroute_options opt;
  opt.clock_spine     = false;
  opt.global_route    = true;
  opt.region_pins     = false;
  opt.partition_cells = 2;
  opt.box_fill        = .49;
  // The fit precondition sees it before any route: the two boxes cannot take distinct tiles.
  const auto plan     = legroute_plan(f.tech, f.fp, f.timing, opt);
  EXPECT_FALSE(plan.route_attempted);
  EXPECT_EQ(plan.route_skip_reason, Legroute_plan_report::Route_skip_reason::window_density);
  EXPECT_FALSE(plan.fit_matched);
  EXPECT_EQ(plan.fit_reason, "count");  // two movable boxes, one free tile: rejected without any search
  EXPECT_TRUE(plan.gates.requires_replan());
  // Without the precondition the route-side gate reports the same deficit.
  opt.fit_density       = 0;
  const auto unscreened = legroute_plan(f.tech, f.fp, f.timing, opt);
  ASSERT_EQ(unscreened.gates.unfittable_boxes, 1u);
  EXPECT_DOUBLE_EQ(unscreened.gates.max_cell_density, .5);
  auto d     = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto g     = lr::build_grid(d, {});
  auto boxes = lr::build_boxes(d, g);
  ASSERT_EQ(boxes.boxes.size(), 2u);
  lr::Routing     routing;
  Legroute_report report;
  try {
    lr::sat_place(d, g, boxes, {}, routing, report);
    FAIL() << "unfittable movable region entered SAT";
  } catch (const Legroute_replan& replan) {
    EXPECT_EQ(legroute_gate_json(replan.gates), legroute_gate_json(unscreened.gates));
  }
  EXPECT_EQ(report.sat_regions, 0u);
  EXPECT_EQ(report.sat_variables, 0u);
  EXPECT_EQ(report.sat_solves, 0u);
}

TEST(Legroute, PatternPlanningAndAllSignalHandoffReportTheSameWork) {
  Fixture    f(64, 4);
  const auto a = f.cell("dff", 100, 0), b = f.cell("dff", 2500, 500);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  Legroute_options opt;
  opt.legal_boxes = opt.global_route = opt.global_pattern = true;
  opt.region_pins = opt.clock_spine = false;
  opt.partition_cells               = 2;
  opt.threads                       = 1;
  const auto plan                   = legroute_plan(f.tech, f.fp, f.timing, opt, {}, nullptr, 100000, true);
  ASSERT_FALSE(plan.gates.requires_replan());
  EXPECT_EQ(plan.pattern_attempts, 1u);
  EXPECT_EQ(plan.pattern_completed, 1u);
  EXPECT_EQ(plan.search_pops, 0u);
  auto            d     = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto            g     = lr::build_grid(d, {});
  auto            boxes = lr::build_boxes(d, g);
  lr::Routing     routing;
  Legroute_report report;
  lr::sat_place(d, g, boxes, {}, routing, report);
  EXPECT_EQ(report.global_pattern_attempts, plan.pattern_attempts);
  EXPECT_EQ(report.global_pattern_completed, plan.pattern_completed);
  EXPECT_EQ(report.global_pattern_fallbacks, plan.pattern_fallbacks);
  EXPECT_EQ(report.global_pattern_work, plan.pattern_work);
  EXPECT_EQ(report.global_pattern_work_limited, plan.pattern_work_limited);
  EXPECT_EQ(report.global_search_pops, plan.search_pops);
  EXPECT_EQ(report.sat_unsat_regions + report.sat_unknown_regions, 0u);
}

TEST(Legroute, RetainedTreeCanLeaveAndReenterARegion) {
  Fixture    f(48, 6);
  const auto a = f.cell("dff", 100, 0), b = f.cell("dff", 1700, 0);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  Legroute_options opt;
  opt.threads      = 1;
  opt.clock_spine  = false;
  opt.legal_boxes  = true;
  opt.global_route = opt.global_all = true;
  opt.region_pins                   = false;
  opt.global_share                  = 1;
  opt.spill_rounds                  = 0;
  opt.final                         = false;
  // Region 0 is a U; the shortest tree runs between its two ends through region 1.
  Legroute_regions regions;
  regions.rects = {
      {   0,    0,  800, 1500},
      {1600,    0, 2400, 1500},
      { 800, 1000, 1600, 1500},
      { 800,    0, 1600, 1000}
  };
  regions.rect_region = {0, 0, 0, 1};
  for (bool keep : {false, true}) {
    opt.global_keep = keep;
    auto d          = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
    auto g          = lr::build_grid(d, regions);
    auto boxes      = lr::build_boxes(d, g);
    ASSERT_TRUE(lr::connected_regions(g, g.region));
    lr::Routing     routes;
    Legroute_report rep;
    lr::sat_place(d, g, boxes, regions, routes, rep);
    if (keep) {
      EXPECT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
      EXPECT_EQ(rep.global_retained_parts, 2u);
      EXPECT_EQ(rep.global_pieces, 0u) << "each retained component has no interior edge";
      EXPECT_EQ(routes.conns.size(), 2u) << "only the two boundary hops, without an extra in-region connection";
    } else {
      EXPECT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
      EXPECT_EQ(rep.global_retained_parts, 0u);
      EXPECT_EQ(routes.conns.size(), 2u) << "separate local pieces are already joined outside the region";
    }
  }
}

TEST(Legroute, LocalTreePartsRouteIndependentlyInsideAReenteredRegion) {
  Fixture    f(80, 6);
  const auto a = f.cell("dff", 100, 0), b = f.cell("dff", 3300, 0);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  Legroute_options opt;
  opt.threads      = 1;
  opt.clock_spine  = false;
  opt.legal_boxes  = true;
  opt.global_route = opt.global_all = true;
  opt.region_pins                   = false;
  opt.global_share                  = 1;
  opt.spill_rounds                  = 0;
  opt.final                         = false;
  Legroute_regions regions;
  // The two arms each need an interior SAT connection. Their global connection
  // crosses the bottom of region 1; no extra path through the U's top is required.
  regions.rects = {
      {   0,    0, 1600, 1500},
      {2400,    0, 4000, 1500},
      {1600, 1000, 2400, 1500},
      {1600,    0, 2400, 1000}
  };
  regions.rect_region = {0, 0, 0, 1};
  auto d              = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto g              = lr::build_grid(d, regions);
  auto boxes          = lr::build_boxes(d, g);
  ASSERT_EQ(g.cols, 5);
  ASSERT_TRUE(lr::connected_regions(g, g.region));
  // The U's top has no horizontal capacity. No component needs to cross it;
  // a cut bound spanning all endpoints together would reject this legal route.
  for (int c = 0; c < g.cols; ++c) {
    const int t  = g.tile(c, g.rows - 1);
    g.box_cap[t] = 0;
    for (size_t l = 0; l < g.layers.size(); ++l) {
      if (g.layers[l].horizontal) {
        g.cap[l][t] = 0;
      }
    }
  }
  lr::Routing     routes;
  Legroute_report rep;
  lr::sat_place(d, g, boxes, regions, routes, rep);
  ASSERT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
  EXPECT_EQ(rep.global_retained_parts, 0u);
  EXPECT_EQ(rep.global_pieces, 0u);
  std::set<std::pair<int, int>> edges;
  size_t                        local = 0;
  for (const auto& conn : routes.conns) {
    local += std::ranges::all_of(conn.poly, [&](const auto& p) { return g.region[g.tile(p.first, p.second)] == 0; });
    for (size_t k = 1; k < conn.poly.size(); ++k) {
      const auto [x0, y0] = conn.poly[k - 1];
      const auto [x1, y1] = conn.poly[k];
      EXPECT_EQ(y0, 0);
      EXPECT_EQ(y1, 0);
      for (int c = std::min(x0, x1); c < std::max(x0, x1); ++c) {
        edges.emplace(g.tile(c, 0), g.tile(c + 1, 0));
      }
    }
  }
  EXPECT_EQ(local, 2u) << "both interior connections must be chosen by regional SAT";
  EXPECT_EQ(edges,
            (std::set<std::pair<int, int>>{
                {0, 1},
                {1, 2},
                {2, 3},
                {3, 4}
  }));
  size_t pins = 0;
  for (const auto& node : routes.trees[0].nodes) {
    pins += node.pins.size();
    if (!node.pins.empty()) {
      EXPECT_TRUE(node.tile == 0 || node.tile == 4);
    }
  }
  EXPECT_EQ(pins, 2u);
}

TEST(Legroute, FixedRoutingOverflowCannotBeClampedToZero) {
  Fixture    f(48, 2);
  const auto a = f.cell("dff", 100, 0), b = f.cell("dff", 1700, 0);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  Legroute_options opt;
  opt.threads      = 1;
  opt.clock_spine  = false;
  opt.legal_boxes  = true;
  opt.global_route = opt.global_all = opt.global_keep = true;
  opt.region_pins                                     = false;
  opt.global_share                                    = 1;
  opt.cap_scale                                       = .01;  // scaled regional capacity is zero, yet a fixed tree uses its edges
  opt.spill_rounds                                    = 0;
  opt.final                                           = false;
  Legroute_regions regions;
  auto             d     = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto             g     = lr::build_grid(d, regions);
  auto             boxes = lr::build_boxes(d, g);
  lr::Routing      routes;
  Legroute_report  rep;
  lr::sat_place(d, g, boxes, regions, routes, rep);
  EXPECT_EQ(rep.sat_unsat_regions, 1u);
  EXPECT_GT(rep.sat_fixed_overflow, 0u);
  EXPECT_GT(rep.global_retained_parts, 0u);
}

TEST(Legroute, MovableCoarseCorridorJointlyLegalizesCoincidentBoxesAndRoutesTheirSelectedTiles) {
  for (unsigned lazy : {0u, 2u}) {
    SCOPED_TRACE(lazy);
    Fixture    f(48, 6);
    const auto a = f.cell("inv", 100, 0), b = f.cell("inv", 100, 0);
    f.net({
        {a, "Y"},
        {b, "A"}
    });
    Legroute_options opt;
    opt.threads     = 1;
    opt.legal_boxes = opt.clock_spine = opt.region_pins = opt.final = opt.sat_seed = false;
    opt.partition_cells                                                            = 100;
    opt.window_tiles                                                               = 1;
    opt.spill_rounds                                                               = 0;
    opt.sat_ms                                                                     = 1000;
    opt.capacity_lazy                                                              = lazy;
    opt.tiers.clear();
    auto      d = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
    auto      g = lr::build_grid(d, {});
    // Keep the two cells in distinct boxes to isolate local legalization from
    // connectivity grouping. Both start on the same admitted coarse tile.
    lr::Boxes boxes;
    for (int i = 0; i < int(d.movable.size()); ++i) {
      lr::Box box;
      box.cells = {i};
      box.cx    = 150;
      box.cy    = 125;
      box.tile  = g.tile_of(box.cx, box.cy);
      box.sites = lr::cell_sites(d, g, i);
      boxes.box_of.push_back(int(boxes.boxes.size()));
      boxes.boxes.push_back(box);
    }
    ASSERT_EQ(boxes.boxes.size(), 2u);
    const int home = boxes.boxes[0].tile;
    ASSERT_EQ(boxes.boxes[1].tile, home);
    lr::Routing     routes;
    Legroute_report report;
    lr::sat_place(d, g, boxes, {}, routes, report);
    EXPECT_EQ(report.sat_unsat_regions + report.sat_unknown_regions, 0u);
    EXPECT_FALSE(report.coarse_backbone_fixed);
    EXPECT_TRUE(report.coarse_corridor_free);
    EXPECT_EQ(report.coarse_backbone_parts, 1u);
    EXPECT_EQ(report.coarse_backbone_edges, 0u);  // singleton witness, not an invented star
    // This easy joint placement/routing witness is found by the bounded
    // proposal and independently validated, without building the free graph.
    EXPECT_EQ(report.corridor_seed_attempts, 1u);
    EXPECT_EQ(report.corridor_seed_successes, 1u);
    EXPECT_EQ(report.coarse_attachment_stubs, 2u);
    EXPECT_EQ(report.corridor_components, 1u);
    EXPECT_EQ(report.corridor_checked_components, 1u);
    EXPECT_EQ(report.corridor_candidate_edges, 0u);
    EXPECT_GT(report.corridor_selected_edges, 0u);
    ASSERT_NE(boxes.boxes[0].tile, boxes.boxes[1].tile);
    EXPECT_TRUE(boxes.boxes[0].tile != home || boxes.boxes[1].tile != home);
    std::map<std::pair<int, int>, std::set<int>> edge_nets;
    std::map<int, std::set<int>>                 graph;
    for (const auto& conn : routes.conns) {
      for (size_t k = 1; k < conn.poly.size(); ++k) {
        const auto [x0, y0] = conn.poly[k - 1];
        const auto [x1, y1] = conn.poly[k];
        EXPECT_TRUE(x0 == x1 || y0 == y1);
        const int dx = (x1 > x0) - (x1 < x0), dy = (y1 > y0) - (y1 < y0);
        for (int x = x0, y = y0; x != x1 || y != y1; x += dx, y += dy) {
          const int t = g.tile(x, y), q = g.tile(x + dx, y + dy);
          edge_nets[{std::min(t, q), int(y0 == y1)}].insert(conn.net);
          graph[t].insert(q);
          graph[q].insert(t);
          EXPECT_EQ(g.region[t], g.region[q]);
          EXPECT_LE(std::abs(g.col(t) - g.col(home)) + std::abs(g.row(t) - g.row(home)), 1);
          EXPECT_LE(std::abs(g.col(q) - g.col(home)) + std::abs(g.row(q) - g.row(home)), 1);
        }
      }
    }
    ASSERT_FALSE(edge_nets.empty());
    std::set<int>    reached{boxes.boxes[0].tile};
    std::vector<int> queue{boxes.boxes[0].tile};
    for (size_t i = 0; i < queue.size(); ++i) {
      for (int q : graph[queue[i]]) {
        if (reached.insert(q).second) {
          queue.push_back(q);
        }
      }
    }
    EXPECT_TRUE(reached.contains(boxes.boxes[1].tile));
    for (const auto& [edge, nets] : edge_nets) {
      const auto [t, h] = edge;
      const int q       = t + (h ? 1 : g.cols);
      int       ct = 0, cq = 0;
      for (size_t layer = 0; layer < g.layers.size(); ++layer) {
        if (g.layers[layer].horizontal == bool(h)) {
          ct += g.cap[layer][t];
          cq += g.cap[layer][q];
        }
      }
      EXPECT_LE(int(nets.size()), int(std::floor(std::min(ct, cq) * opt.cap_scale)));
    }
    lr::pack_cells(d, g, boxes);
    lr::final_place(d, g, boxes, routes, 1);
    const auto& pa = f.fp.components[a].place;
    const auto& pb = f.fp.components[b].place;
    EXPECT_TRUE(pa.x + 100 <= pb.x || pb.x + 100 <= pa.x || pa.y + 250 <= pb.y || pb.y + 250 <= pa.y)
        << "distinct legal tiles must separate the original coincident cell placements";
  }
}

TEST(Legroute, OptionalBoxRouteSeedKeepsOriginalDomainsAndCommitsOnlyValidatedMovablePlacement) {
  EXPECT_FALSE(Legroute_options{}.box_route_seed);
  for (unsigned lazy : {0u, 2u}) {
    SCOPED_TRACE(lazy);
    Fixture    f(48, 6);
    const auto a = f.cell("inv", 100, 0), b = f.cell("inv", 100, 0);
    f.net({
        {a, "Y"},
        {b, "A"}
    });
    Legroute_options opt;
    opt.threads     = 1;
    opt.legal_boxes = opt.clock_spine = opt.region_pins = opt.final = opt.sat_seed = false;
    opt.box_route_seed                                                             = true;
    opt.global_route                                                               = true;
    opt.partition_cells                                                            = 100;
    opt.window_tiles                                                               = 1;
    opt.spill_rounds                                                               = 0;
    opt.sat_ms                                                                     = 1000;
    opt.capacity_lazy                                                              = lazy;
    opt.tiers.clear();
    EXPECT_NO_THROW(validate_legroute_options(opt));
    auto invalid        = opt;
    invalid.legal_boxes = true;
    EXPECT_THROW(validate_legroute_options(invalid), std::invalid_argument);
    invalid        = opt;
    invalid.freeze = true;
    EXPECT_THROW(validate_legroute_options(invalid), std::invalid_argument);

    const auto physical_pose = f.fp.components;
    const auto plan          = legroute_plan(f.tech, f.fp, f.timing, opt);
    EXPECT_TRUE(plan.box_route_seed_attempted);
    EXPECT_TRUE(plan.box_route_seed_accepted);
    EXPECT_EQ(plan.endpoint_basis, "provisional_box_tiles");
    EXPECT_EQ(f.fp.components, physical_pose);
    auto      d = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
    auto      g = lr::build_grid(d, {});
    lr::Boxes boxes;
    for (int i = 0; i < int(d.movable.size()); ++i) {
      lr::Box box;
      box.cells = {i};
      box.cx    = 150;
      box.cy    = 125;
      box.tile  = g.tile_of(box.cx, box.cy);
      box.sites = lr::cell_sites(d, g, i);
      boxes.box_of.push_back(int(boxes.boxes.size()));
      boxes.boxes.push_back(box);
    }
    const auto original = boxes;
    ASSERT_EQ(original.boxes[0].tile, original.boxes[1].tile);
    lr::Routing     routes;
    Legroute_report report;
    lr::sat_place(d, g, boxes, {}, routes, report);
    EXPECT_EQ(report.box_route_seed_attempts, 1u);
    EXPECT_EQ(report.box_route_seed_successes, 1u);
    EXPECT_EQ(report.corridor_seed_successes, 1u);
    EXPECT_EQ(report.sat_unsat_regions + report.sat_unknown_regions, 0u);
    EXPECT_TRUE(report.coarse_corridor_free);
    EXPECT_GT(report.coarse_backbone_edges, 0u);
    EXPECT_GT(report.corridor_selected_edges, 0u);
    EXPECT_EQ(f.fp.components, physical_pose);
    EXPECT_EQ(boxes.box_of, original.box_of);
    std::set<int> occupied;
    for (size_t i = 0; i < boxes.boxes.size(); ++i) {
      EXPECT_EQ(boxes.boxes[i].cx, original.boxes[i].cx);
      EXPECT_EQ(boxes.boxes[i].cy, original.boxes[i].cy);
      EXPECT_EQ(boxes.boxes[i].cells, original.boxes[i].cells);
      EXPECT_FALSE(boxes.boxes[i].fixed) << "private point assumptions must not freeze the actual movable boxes";
      EXPECT_TRUE(occupied.insert(boxes.boxes[i].tile).second);
      EXPECT_TRUE(lr::box_fits(d, g, boxes.boxes[i], boxes.boxes[i].tile));
    }
    std::map<std::pair<int, int>, std::set<int>> edge_nets;
    std::map<int, std::set<int>>                 graph;
    for (const auto& conn : routes.conns) {
      for (size_t k = 1; k < conn.poly.size(); ++k) {
        const auto [x0, y0] = conn.poly[k - 1];
        const auto [x1, y1] = conn.poly[k];
        ASSERT_TRUE(x0 == x1 || y0 == y1);
        const int dx = (x1 > x0) - (x1 < x0), dy = (y1 > y0) - (y1 < y0);
        for (int x = x0, y = y0; x != x1 || y != y1; x += dx, y += dy) {
          const int t = g.tile(x, y), q = g.tile(x + dx, y + dy);
          EXPECT_EQ(g.region[t], g.region[q]);
          edge_nets[{std::min(t, q), int(y0 == y1)}].insert(conn.net);
          graph[t].insert(q);
          graph[q].insert(t);
        }
      }
    }
    ASSERT_FALSE(edge_nets.empty());
    std::set<int>    reached{boxes.boxes[0].tile};
    std::vector<int> queue{boxes.boxes[0].tile};
    for (size_t i = 0; i < queue.size(); ++i) {
      for (int q : graph[queue[i]]) {
        if (reached.insert(q).second) {
          queue.push_back(q);
        }
      }
    }
    EXPECT_TRUE(reached.contains(boxes.boxes[1].tile));
    for (const auto& [edge, nets] : edge_nets) {
      const auto [t, horizontal] = edge;
      const int q                = t + (horizontal ? 1 : g.cols);
      int       ct = 0, cq = 0;
      for (size_t layer = 0; layer < g.layers.size(); ++layer) {
        if (g.layers[layer].horizontal == bool(horizontal)) {
          ct += g.cap[layer][t];
          cq += g.cap[layer][q];
        }
      }
      EXPECT_LE(int(nets.size()), int(std::floor(std::min(ct, cq) * opt.cap_scale)));
    }
  }
}

TEST(Legroute, PassThroughPartKeepsItsZWhileSiblingMovesAndSharesCapacity) {
  for (unsigned lazy : {0u, 2u}) {
    SCOPED_TRACE(lazy);
    Fixture    f(112, 10);
    const auto a = f.cell("dff", 100, 2000), b = f.cell("dff", 4900, 0), sibling = f.cell("dff", 4900, 2000);
    f.net({
        {      a, "Q"},
        {      b, "D"},
        {sibling, "D"}
    });
    Legroute_options opt;
    opt.threads     = 1;
    opt.clock_spine = opt.region_pins = opt.global_keep = opt.final = false;
    opt.legal_boxes = opt.global_route = opt.global_all = true;
    opt.global_share                                    = .95;
    opt.cap_scale                                       = .5;  // raw capacity 2 or 3 gives exactly one regional track
    opt.capacity_lazy                                   = lazy;
    opt.spill_rounds                                    = 0;
    auto d                                              = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
    auto g                                              = lr::build_grid(d, {});
    ASSERT_EQ(g.cols, 7);
    ASSERT_EQ(g.rows, 5);
    g.regions = 2;
    for (int t = 0; t < int(g.size()); ++t) {
      const int x = g.col(t), y = g.row(t);
      g.region[t] = ((y >= 3 && x >= 1) || (y == 2 && x >= 1 && x <= 5) || (x == 5 && y == 1)) ? 0 : 1;
    }
    ASSERT_TRUE(lr::connected_regions(g, g.region));
    auto      boxes       = lr::build_boxes(d, g);
    const int sb          = boxes.box_of[size_t(d.cell[sibling])];
    const int destination = g.tile(5, 1);
    boxes.boxes[sb].fixed = boxes.boxes[sb].placed = false;
    // Only this tile has placement room for the sibling. Its original global
    // endpoint remains (6,4); the regional solver must actually move it.
    const auto empty_room                          = uint32_t(g.keys.size());
    g.keys.emplace_back();
    for (int t = 0; t < int(g.size()); ++t) {
      if (g.region[t] == 0 && t != destination) {
        g.key[size_t(t)] = empty_room;
      }
    }
    std::vector<int> h(g.size()), v(g.size());
    const auto       horizontal = [&](int y, int lo, int hi, int cap) {
      for (int x = lo; x <= hi; ++x) {
        h[g.tile(x, y)] = cap;
      }
    };
    const auto vertical = [&](int x, int lo, int hi, int cap) {
      for (int y = lo; y <= hi; ++y) {
        v[g.tile(x, y)] = cap;
      }
    };
    // The preferred coarse tree enters region 0 at (1,4), takes a Z through
    // (1,2),(5,2),(5,1), leaves, then reenters at (6,3) to reach the sibling.
    horizontal(4, 0, 1, 3);
    horizontal(2, 1, 6, 3);
    horizontal(0, 5, 6, 3);
    horizontal(3, 5, 6, 2);
    vertical(1, 2, 4, 3);
    vertical(5, 0, 2, 3);
    vertical(5, 3, 3, 2);
    vertical(6, 0, 4, 3);
    bool horizontal_layer = false, vertical_layer = false;
    for (size_t l = 0; l < g.layers.size(); ++l) {
      g.cap[l].assign(g.size(), 0);
      if (g.layers[l].horizontal && !horizontal_layer) {
        g.cap[l].assign(h.begin(), h.end());
        horizontal_layer = true;
      } else if (!g.layers[l].horizontal && !vertical_layer) {
        g.cap[l].assign(v.begin(), v.end());
        vertical_layer = true;
      }
    }
    const auto coarse = lr::route_all_signals(d, g, boxes, h, v);
    ASSERT_EQ(coarse.unrouted, 0u);
    const std::set<std::pair<int, int>> coarse_edges(coarse.trees[0].edges.begin(), coarse.trees[0].edges.end());
    const auto                          edge = [&](int x, int y, int xx, int yy) {
      const int ta = g.tile(x, y), tb = g.tile(xx, yy);
      return std::pair{std::min(ta, tb), std::max(ta, tb)};
    };
    ASSERT_TRUE(coarse_edges.contains(edge(6, 2, 6, 3))) << "sibling component must enter from outside the region";
    ASSERT_FALSE(coarse_edges.contains(edge(5, 2, 5, 3))) << "sibling must be a separate component of the same net/region";
    lr::Routing     routes;
    Legroute_report rep;
    lr::sat_place(d, g, boxes, {}, routes, rep);
    ASSERT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
    EXPECT_EQ(rep.sat_fixed_overflow, 0u);
    EXPECT_GT(rep.global_retained_parts, 0u);
    EXPECT_EQ(boxes.boxes[sb].tile, destination);
    EXPECT_FALSE(boxes.boxes[sb].fixed);
    std::map<std::pair<int, int>, size_t> emitted;
    for (const auto& conn : routes.conns) {
      for (size_t k = 1; k < conn.poly.size(); ++k) {
        const auto [x0, y0] = conn.poly[k - 1];
        const auto [x1, y1] = conn.poly[k];
        ASSERT_TRUE(x0 == x1 || y0 == y1);
        const int step = x0 == x1 ? g.cols : 1;
        const int ta = g.tile(x0, y0), tb = g.tile(x1, y1);
        for (int t = std::min(ta, tb); t < std::max(ta, tb); t += step) {
          ++emitted[{t, t + step}];
          if (conn.region == 0
              && g.region[g.tile(conn.poly.front().first, conn.poly.front().second)]
                     == g.region[g.tile(conn.poly.back().first, conn.poly.back().second)]) {
            EXPECT_EQ(g.region[t], 0);
            EXPECT_EQ(g.region[t + step], 0);
          }
        }
      }
    }
    // Every original edge of the fixed Z survives extraction. Both single-L
    // shortcuts leave the mesh, so retaining only endpoint connectivity fails.
    for (int y = 2; y < 4; ++y) {
      EXPECT_TRUE(emitted.contains(edge(1, y, 1, y + 1)));
    }
    for (int x = 1; x < 5; ++x) {
      EXPECT_TRUE(emitted.contains(edge(x, 2, x + 1, 2)));
    }
    EXPECT_TRUE(emitted.contains(edge(5, 1, 5, 2)));
    EXPECT_GT(emitted.at(edge(5, 1, 5, 2)), 1u) << "fixed and local pieces share this one-track edge as one net";
    bool attached = false;
    for (const auto& node : routes.trees[0].nodes) {
      for (int p : node.pins) {
        if (d.nets[0].pins[size_t(p)].comp == int(sibling)) {
          attached = true;
          EXPECT_EQ(node.tile, destination);
        }
      }
    }
    EXPECT_TRUE(attached);
    const auto polylines = [&] {
      std::vector<std::vector<std::pair<int, int>>> out;
      for (const auto& conn : routes.conns) {
        out.push_back(conn.poly);
      }
      return out;
    }();
    lr::box_demand(d, g, boxes, routes);
    lr::assign_layers(d, g, routes);
    ASSERT_FALSE(routes.segments.empty());
    lr::assign_tracks(d, g, routes, 1);
    EXPECT_EQ(routes.unassigned, 0u);
    ASSERT_EQ(routes.conns.size(), polylines.size());
    for (size_t k = 0; k < routes.conns.size(); ++k) {
      EXPECT_EQ(routes.conns[k].poly, polylines[k]);
    }
  }
}

TEST(Legroute, MixedMeshComponentUsesAJointlySupportedMovableStarRoot) {
  // Exact SHA3 regional mesh, translated down by 47 tile rows. A real IO
  // and two fixed global hops share this component with one movable box.
  Fixture    f(512, 30);
  const auto driver = f.cell("dff", 30 * 800 + 100, 0);
  const auto moving = f.cell("dff", 16 * 800 + 100, 8 * 500);
  const auto sink   = f.cell("dff", 30 * 800 + 100, 10 * 500);
  f.fp.terminals.push_back(
      {"out", "n0", "OUTPUT", 25, 13 * 500 + 125, ph::Orientation::N, ph::Status::FIXED, {{"M4", {-10, -10, 10, 10}}}});
  f.fp.nets.push_back({
      "n0",
      {{f.fp.components[driver].name, "Q"},
        {"PIN", "out"},
        {f.fp.components[moving].name, "D"},
        {f.fp.components[sink].name, "D"}}
  });
  Legroute_options opt;
  opt.threads     = 1;
  opt.legal_boxes = opt.global_route = opt.global_all = true;
  opt.global_keep = opt.clock_spine = opt.region_pins = opt.final = false;
  opt.spill_rounds                                                = 0;
  opt.window_tiles                                                = 4;
  auto d                                                          = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto g                                                          = lr::build_grid(d, {});
  ASSERT_EQ(g.cols, 32);
  ASSERT_EQ(g.rows, 15);
  g.regions = 3;
  const std::array<int, 13> lo{7, 6, 5, 4, 3, 2, 0, 0, 0, 0, 0, 0, 0};
  const std::array<int, 13> hi{31, 31, 31, 31, 31, 31, 31, 31, 31, 28, 23, 23, 8};
  for (int t = 0; t < int(g.size()); ++t) {
    const int  x = g.col(t), y = g.row(t);
    const bool mesh = y >= 1 && y <= 13 && x >= lo[size_t(y - 1)] && x <= hi[size_t(y - 1)];
    g.region[t]     = mesh ? 0 : y < 7 ? 1 : 2;
    // The only routes outside the mesh are the two leaf entries. Therefore
    // the IO, moving box and both hop anchors belong to one coarse part.
    if (!mesh && t != g.tile(30, 0) && t != g.tile(30, 10)) {
      g.box_cap[size_t(t)] = 0;
      for (auto& layer : g.cap) {
        layer[size_t(t)] = 0;
      }
    }
  }
  ASSERT_TRUE(lr::connected_regions(g, g.region));
  auto      boxes       = lr::build_boxes(d, g);
  const int mb          = boxes.box_of[size_t(d.cell[moving])];
  boxes.boxes[mb].fixed = boxes.boxes[mb].placed = false;
  const auto terminals                           = f.fp.terminals;
  const int  io = g.tile(0, 13), hop0 = g.tile(30, 1), hop1 = g.tile(30, 9);
  const auto inside_l = [&](int a, int b, bool horizontal) {
    const int row = horizontal ? g.row(a) : g.row(b), col = horizontal ? g.col(b) : g.col(a);
    for (int x = std::min(g.col(a), g.col(b)); x <= std::max(g.col(a), g.col(b)); ++x) {
      if (g.region[g.tile(x, row)] != 0) {
        return false;
      }
    }
    for (int y = std::min(g.row(a), g.row(b)); y <= std::max(g.row(a), g.row(b)); ++y) {
      if (g.region[g.tile(col, y)] != 0) {
        return false;
      }
    }
    return true;
  };
  ASSERT_FALSE(inside_l(io, hop0, true));
  ASSERT_FALSE(inside_l(io, hop0, false));
  std::set<int> domain, supported;
  for (int y = 4; y <= 12; ++y) {
    for (int x = 12; x <= 20; ++x) {
      const int t = g.tile(x, y);
      ASSERT_TRUE(lr::box_fits(d, g, boxes.boxes[mb], t));
      domain.insert(t);
      if (std::ranges::all_of(std::array{io, hop0, hop1},
                              [&](int end) { return inside_l(t, end, true) || inside_l(t, end, false); })) {
        supported.insert(t);
      }
    }
  }
  ASSERT_EQ(domain.size(), 81u);
  ASSERT_EQ(supported.size(), 54u);
  ASSERT_TRUE(supported.contains(g.tile(16, 8)));
  lr::Routing     routes;
  Legroute_report rep;
  ASSERT_NO_THROW(lr::sat_place(d, g, boxes, {}, routes, rep));
  ASSERT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
  EXPECT_EQ(rep.sat_root_fallbacks, 1u);
  EXPECT_GT(rep.sat_solves, 0u);
  EXPECT_FALSE(boxes.boxes[mb].fixed);
  const int chosen = boxes.boxes[mb].tile;
  EXPECT_TRUE(supported.contains(chosen));
  EXPECT_EQ(f.fp.terminals, terminals);
  std::map<int, std::set<int>> graph;
  size_t                       regional_connections = 0, crossings = 0;
  for (const auto& c : routes.conns) {
    const auto& nodes = routes.trees[size_t(c.net)].nodes;
    const int   a = nodes[size_t(c.a)].tile, b = nodes[size_t(c.b)].tile;
    if (g.region[a] != g.region[b]) {
      ++crossings;
      EXPECT_TRUE(a == hop0 || b == hop0 || a == hop1 || b == hop1);
    } else if (c.region == 0) {
      ++regional_connections;
      EXPECT_TRUE(a == chosen || b == chosen);
    }
    for (size_t k = 1; k < c.poly.size(); ++k) {
      const auto [x0, y0] = c.poly[k - 1];
      const auto [x1, y1] = c.poly[k];
      ASSERT_TRUE(x0 == x1 || y0 == y1);
      const int step = x0 == x1 ? g.cols : 1;
      const int ta = g.tile(x0, y0), tb = g.tile(x1, y1);
      for (int t = std::min(ta, tb); t < std::max(ta, tb); t += step) {
        graph[t].insert(t + step);
        graph[t + step].insert(t);
        if (c.region == 0 && g.region[a] == g.region[b]) {
          EXPECT_EQ(g.region[t], 0);
          EXPECT_EQ(g.region[t + step], 0);
        }
      }
    }
  }
  EXPECT_EQ(crossings, 2u);
  EXPECT_EQ(regional_connections, 3u);
  std::set<int>    reached{io};
  std::vector<int> pending{io};
  while (!pending.empty()) {
    const int t = pending.back();
    pending.pop_back();
    for (int next : graph[t]) {
      if (reached.insert(next).second) {
        pending.push_back(next);
      }
    }
  }
  for (int t : {chosen, hop0, hop1, g.tile(30, 0), g.tile(30, 10)}) {
    EXPECT_TRUE(reached.contains(t));
  }
  lr::box_demand(d, g, boxes, routes);
  lr::assign_layers(d, g, routes);
  ASSERT_FALSE(routes.segments.empty());
  lr::assign_tracks(d, g, routes, 1);
  EXPECT_EQ(routes.unassigned, 0u);
  const auto report_path = std::filesystem::path(testing::TempDir()) / "supported_star_root.json";
  write_legroute_report(rep, report_path.string());
  std::ifstream report_file(report_path);
  std::string   report_text;
  ASSERT_TRUE(bool(std::getline(report_file, report_text)));
  EXPECT_NE(report_text.find("\"sat_root_fallbacks\":1"), std::string::npos);
  std::filesystem::remove(report_path);
}

TEST(Legroute, StarFallbackRequiresOneRootPositionForEveryNeighbor) {
  Fixture    f(96, 10);
  const auto a = f.cell("dff", 100, 500), moving = f.cell("dff", 900, 1000);
  const auto b = f.cell("dff", 2500, 1500), c = f.cell("dff", 1700, 1500);
  f.net({
      {     a, "Q"},
      {moving, "D"},
      {     b, "D"},
      {     c, "D"}
  });
  Legroute_options opt;
  opt.threads      = 1;
  opt.legal_boxes  = true;
  opt.global_route = opt.global_all = opt.clock_spine = opt.region_pins = opt.final = false;
  opt.spill_rounds                                                                  = 0;
  auto d = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto g = lr::build_grid(d, {});
  ASSERT_EQ(g.cols, 6);
  ASSERT_EQ(g.rows, 5);
  const std::set<std::pair<int, int>> mesh{
      {0, 1},
      {0, 2},
      {1, 1},
      {1, 2},
      {2, 1},
      {2, 2},
      {2, 3},
      {3, 3},
      {3, 4}
  };
  g.regions = 1;
  for (int t = 0; t < int(g.size()); ++t) {
    g.region[t] = mesh.contains({g.col(t), g.row(t)}) ? 0 : -1;
  }
  for (int start = 0; start < int(g.size()); ++start) {
    if (g.region[start] >= 0) {
      continue;
    }
    g.region[start] = g.regions;
    std::vector<int> pending{start};
    while (!pending.empty()) {
      const int t = pending.back();
      pending.pop_back();
      for (int next : {g.col(t) ? t - 1 : -1, g.col(t) + 1 < g.cols ? t + 1 : -1, t - g.cols, t + g.cols}) {
        if (next >= 0 && next < int(g.size()) && g.region[next] < 0) {
          g.region[next] = g.regions;
          pending.push_back(next);
        }
      }
    }
    ++g.regions;
  }
  ASSERT_TRUE(lr::connected_regions(g, g.region));
  auto      boxes       = lr::build_boxes(d, g);
  const int mb          = boxes.box_of[size_t(d.cell[moving])];
  boxes.boxes[mb].fixed = boxes.boxes[mb].placed = false;
  const int  p = g.tile(1, 2), q = g.tile(3, 4), ta = g.tile(0, 1), tb = g.tile(3, 3), tc = g.tile(2, 3);
  const auto empty_room = uint32_t(g.keys.size());
  g.keys.emplace_back();
  for (int t = 0; t < int(g.size()); ++t) {
    if (g.region[t] == 0 && t != p && t != q && t != ta && t != tb && t != tc) {
      g.key[size_t(t)] = empty_room;
    }
  }
  const auto visible = [&](int from, int to) {
    const auto h = [&](int y) {
      for (int x = std::min(g.col(from), g.col(to)); x <= std::max(g.col(from), g.col(to)); ++x) {
        if (!mesh.contains({x, y})) {
          return false;
        }
      }
      return true;
    };
    const auto v = [&](int x) {
      for (int y = std::min(g.row(from), g.row(to)); y <= std::max(g.row(from), g.row(to)); ++y) {
        if (!mesh.contains({x, y})) {
          return false;
        }
      }
      return true;
    };
    return (h(g.row(from)) && v(g.col(to))) || (v(g.col(from)) && h(g.row(to)));
  };
  ASSERT_FALSE(visible(ta, tb));
  // Both candidate positions support C, but A requires P and B requires Q.
  // Taking a union of independent pair supports would choose this root and
  // create an impossible model. The later fixed C root supports all legs.
  ASSERT_TRUE(visible(p, ta));
  ASSERT_FALSE(visible(p, tb));
  ASSERT_FALSE(visible(q, ta));
  ASSERT_TRUE(visible(q, tb));
  ASSERT_TRUE(visible(p, tc));
  ASSERT_TRUE(visible(q, tc));
  ASSERT_TRUE(visible(tc, ta));
  ASSERT_TRUE(visible(tc, tb));
  lr::Routing     routes;
  Legroute_report rep;
  ASSERT_NO_THROW(lr::sat_place(d, g, boxes, {}, routes, rep));
  ASSERT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
  EXPECT_EQ(rep.sat_root_fallbacks, 1u);
  EXPECT_FALSE(boxes.boxes[mb].fixed);
  EXPECT_TRUE(boxes.boxes[mb].tile == p || boxes.boxes[mb].tile == q);
  ASSERT_EQ(routes.conns.size(), 3u);
  for (const auto& conn : routes.conns) {
    const auto& nodes = routes.trees[size_t(conn.net)].nodes;
    EXPECT_TRUE(nodes[size_t(conn.a)].tile == tc || nodes[size_t(conn.b)].tile == tc);
  }
}

TEST(Legroute, ExpiredRegionSetupDoesNotRestartBudgetsOrPublishRouting) {
  Fixture    f(80, 6);
  const auto a = f.cell("dff", 100, 0), b = f.cell("dff", 2500, 500);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  f.fp.terminals.push_back({"clk", "clk", "INPUT", 125, 1025, ph::Orientation::N, ph::Status::FIXED, {{"M4", {-10, -10, 10, 10}}}});
  f.fp.nets.push_back({
      "clk",
      {{"PIN", "clk"}, {f.fp.components[a].name, "CLK"}, {f.fp.components[b].name, "CLK"}}
  });
  const auto       components  = f.fp.components;
  const auto       terminals   = f.fp.terminals;
  const auto       nets        = f.fp.nets;
  const auto       width_rules = f.fp.nondefault_rules;
  Legroute_options opt;
  opt.threads          = 2;
  opt.sat_ms           = 0;  // Deterministic setup expiry: not a machine-speed assertion.
  opt.fail_fast        = false;
  opt.alternatives     = "noise,checker";
  opt.alt_ms           = 10000;
  opt.sat_relax_rounds = 3;
  opt.spill_rounds     = 2;
  opt.spill_fraction   = 0;
  opt.window_grow      = 1;
  const auto expired   = legroute(f.tech, f.fp, f.timing, opt);
  EXPECT_GT(expired.sat_unknown_regions, 0u);
  EXPECT_EQ(expired.sat_unsat_regions, 0u);
  EXPECT_EQ(expired.sat_variables, 0u);
  EXPECT_EQ(expired.sat_clauses, 0u);
  EXPECT_EQ(expired.sat_solves, 0u);
  EXPECT_EQ(expired.alt_tried, 0u) << "an expired region must not gain a fresh alt_ms allowance";
  EXPECT_FALSE(expired.wires_emitted);
  EXPECT_EQ(expired.unchecked_nets, 2u);
  EXPECT_EQ(expired.clock_required_nets, 1u);
  EXPECT_EQ(expired.clock_unchecked_nets, 1u);
  EXPECT_EQ(expired.clock_domains, 0u) << "clock construction and final routing were skipped";
  EXPECT_EQ(f.fp.components, components);
  EXPECT_EQ(f.fp.terminals, terminals);
  EXPECT_EQ(f.fp.nets, nets);
  EXPECT_EQ(f.fp.nondefault_rules, width_rules);
  // The same placement and policy, with a real budget, reaches the solver
  // and later routing stages. The test does not assume detailed closure.
  opt.sat_ms           = 1000;
  const auto completed = legroute(f.tech, f.fp, f.timing, opt);
  EXPECT_EQ(completed.sat_unsat_regions + completed.sat_unknown_regions, 0u);
  EXPECT_GT(completed.sat_solves, 0u);
  EXPECT_GT(completed.clock_domains, 0u);
}

TEST(Legroute, FixedConnectionOutsideBothLShapesIsAModelErrorNotAreaFailure) {
  Fixture    f(80, 6);
  const auto a = f.cell("dff", 100, 0), b = f.cell("dff", 3300, 1000);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  Legroute_options opt;
  opt.threads      = 1;
  opt.legal_boxes  = true;
  opt.global_route = opt.global_all = opt.clock_spine = opt.region_pins = opt.final = false;
  opt.spill_rounds                                                                  = 0;
  auto d = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto g = lr::build_grid(d, {});
  ASSERT_EQ(g.cols, 5);
  ASSERT_EQ(g.rows, 3);
  g.regions = 3;
  for (int t = 0; t < int(g.size()); ++t) {
    const int x = g.col(t), y = g.row(t);
    g.region[t] = y == 1 || (x == 0 && y == 0) || (x == 4 && y == 2) ? 0 : y == 0 ? 1 : 2;
  }
  ASSERT_TRUE(lr::connected_regions(g, g.region));  // a connected Z corridor, physically not disproved
  auto            boxes = lr::build_boxes(d, g);
  lr::Routing     routes;
  Legroute_report rep;
  try {
    lr::sat_place(d, g, boxes, {}, routes, rep);
    FAIL() << "neither single-L shape can represent the fixed connection";
  } catch (const Legroute_unfit&) {
    FAIL() << "a routing model limitation must not request area growth";
  } catch (const std::runtime_error& e) {
    const std::string message = e.what();
    EXPECT_NE(message.find("model cannot represent fixed connection"), std::string::npos);
    EXPECT_NE(message.find("net 0 (n0), region 0, part -1"), std::string::npos);
    EXPECT_NE(message.find("tiles 0 (0,0) -> 14 (4,2)"), std::string::npos);
    EXPECT_NE(message.find("not area grown"), std::string::npos);
  }
  EXPECT_EQ(rep.sat_solves, 0u);
  EXPECT_TRUE(routes.conns.empty());
  // The same initial pair is not a proof when one endpoint can move. Its
  // complete domain contains legal L placements and must reach the solver.
  auto      movable_boxes           = lr::build_boxes(d, g);
  const int moving                  = movable_boxes.box_of[size_t(d.cell[b])];
  movable_boxes.boxes[moving].fixed = movable_boxes.boxes[moving].placed = false;
  lr::Routing     movable_routes;
  Legroute_report movable_report;
  ASSERT_NO_THROW(lr::sat_place(d, g, movable_boxes, {}, movable_routes, movable_report));
  EXPECT_EQ(movable_report.sat_unsat_regions + movable_report.sat_unknown_regions, 0u);
  EXPECT_GT(movable_report.sat_solves, 0u);
  EXPECT_NE(movable_boxes.boxes[moving].tile, g.tile(4, 2));
}

TEST(Legroute, AlternativeWorkerPropagatesFixedConnectionModelError) {
  Fixture    f(80, 6);
  const auto a = f.cell("dff", 100, 0), b = f.cell("dff", 3300, 1000);
  const auto movable = f.cell("inv", 2000, 500);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  Legroute_options opt;
  opt.threads      = 1;
  opt.legal_boxes  = true;
  opt.global_route = opt.global_all = opt.clock_spine = opt.region_pins = opt.final = false;
  opt.cap_scale                                                                     = .01;
  opt.spill_rounds                                                                  = 1;
  opt.spill_fraction                                                                = 0;
  opt.alternatives                                                                  = "noise,checker";
  auto d = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto g = lr::build_grid(d, {});
  ASSERT_EQ(g.cols, 5);
  ASSERT_EQ(g.rows, 3);
  g.regions = 3;
  for (int t = 0; t < int(g.size()); ++t) {
    const int x = g.col(t), y = g.row(t);
    g.region[t] = y == 1 || (x == 0 && y == 0) || (x == 4 && y == 2) ? 0 : y == 0 ? 1 : 2;
  }
  ASSERT_TRUE(lr::connected_regions(g, g.region));
  auto      boxes           = lr::build_boxes(d, g);
  const int moving          = boxes.box_of[size_t(d.cell[movable])];
  boxes.boxes[moving].fixed = boxes.boxes[moving].placed = false;
  const auto      components                             = f.fp.components;
  lr::Routing     routes;
  Legroute_report rep;
  // The main candidate stops at the conservative zero-track cut bound.
  // Checker skips that bound, reaches the fixed non-L connection, and raises
  // a model error inside its actual alternative worker. It must join/rethrow,
  // rather than terminate the process or turn the error into area pressure.
  try {
    lr::sat_place(d, g, boxes, {}, routes, rep);
    FAIL() << "checker worker must propagate its fixed-connection model error";
  } catch (const Legroute_unfit&) {
    FAIL() << "model construction failure is not physical capacity evidence";
  } catch (const std::runtime_error& e) {
    const std::string message = e.what();
    EXPECT_NE(message.find("model cannot represent fixed connection"), std::string::npos);
    EXPECT_NE(message.find("net 0 (n0), region 0, part -1"), std::string::npos);
    EXPECT_NE(message.find("alternative checker (relaxed)"), std::string::npos);
  }
  EXPECT_EQ(f.fp.components, components);
  EXPECT_TRUE(routes.conns.empty());
  for (const auto& net : f.fp.nets) {
    EXPECT_TRUE(net.wires.empty());
  }
}

TEST(Legroute, RetainedBranchesPreserveEveryEdgeAndPinAttachment) {
  Fixture    f(80, 6);
  const auto a = f.cell("dff", 700, 0), b = f.cell("dff", 3300, 1000), c = f.cell("dff", 100, 1000);
  f.fp.components[a].place.status = ph::Status::FIXED;  // origin and center are on different tiles
  f.net({
      {a, "Q"},
      {b, "D"},
      {c, "D"}
  });
  Legroute_options opt;
  opt.threads      = 1;
  opt.clock_spine  = false;
  opt.legal_boxes  = true;
  opt.global_route = opt.global_all = opt.global_keep = true;
  opt.global_share                                    = 1;
  opt.region_pins                                     = false;
  opt.spill_rounds                                    = 0;
  opt.final                                           = false;
  Legroute_regions regions;
  regions.rects = {
      {   0, 0, 1600, 1500},
      {1600, 0, 4000, 1500}
  };
  auto             d     = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto             g     = lr::build_grid(d, regions);
  auto             boxes = lr::build_boxes(d, g);
  std::vector<int> cap_h(g.size()), cap_v(g.size());
  for (size_t t = 0; t < g.size(); ++t) {
    for (size_t l = 0; l < g.layers.size(); ++l) {
      (g.layers[l].horizontal ? cap_h : cap_v)[t] += g.cap[l][t];
    }
  }
  const auto expected = lr::route_all_signals(d, g, boxes, cap_h, cap_v);
  ASSERT_EQ(expected.unrouted, 0u);
  lr::Routing     routes;
  Legroute_report rep;
  lr::sat_place(d, g, boxes, regions, routes, rep);
  ASSERT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
  EXPECT_GT(rep.global_pieces, 0u);
  std::vector<std::set<std::pair<int, int>>> edges(d.nets.size());
  for (const auto& conn : routes.conns) {
    for (size_t i = 1; i < conn.poly.size(); ++i) {
      const auto [x0, y0] = conn.poly[i - 1];
      const auto [x1, y1] = conn.poly[i];
      ASSERT_TRUE(x0 == x1 || y0 == y1);
      const int step = x0 == x1 ? g.cols : 1;
      const int ta = g.tile(x0, y0), tb = g.tile(x1, y1);
      for (int t = std::min(ta, tb); t < std::max(ta, tb); t += step) {
        EXPECT_TRUE(edges[size_t(conn.net)].emplace(t, t + step).second) << "duplicate retained edge";
      }
    }
  }
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const std::set<std::pair<int, int>> wanted(expected.trees[n].edges.begin(), expected.trees[n].edges.end());
    EXPECT_EQ(edges[n], wanted);
    size_t pins = 0;
    for (const auto& node : routes.trees[n].nodes) {
      pins += node.pins.size();
      if (!node.pins.empty()) {
        EXPECT_TRUE(std::ranges::any_of(edges[n], [&](const auto& e) { return e.first == node.tile || e.second == node.tile; }));
      }
    }
    EXPECT_EQ(pins, d.nets[n].pins.size());
  }
}

TEST(Legroute, BoxLayerCapacityCannotAdmitTransitWithoutAnAssignableHorizontalLayer) {
  for (bool m4_available : {false, true}) {
    SCOPED_TRACE(m4_available);
    Fixture    f(32, 2);
    const auto a = f.cell("dff", 0, 0), b = f.cell("dff", 800, 0);
    f.net({
        {a, "Q"},
        {b, "D"}
    });
    Legroute_options opt;
    opt.threads     = 1;
    opt.clock_spine = opt.region_pins = opt.final = false;
    opt.legal_boxes = opt.global_route = opt.global_all = opt.global_keep = true;
    opt.reserved_tracks                                                   = 0;
    opt.spill_rounds                                                      = 0;
    Legroute_regions regions;
    regions.obstacles.push_back({"M6", f.fp.die});
    if (!m4_available) {
      regions.obstacles.push_back({"M4", f.fp.die});
    }
    const auto plan = legroute_plan(f.tech, f.fp, f.timing, opt, regions, nullptr, 100000, true);
    auto       d    = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
    auto       g    = lr::build_grid(d, regions);
    ASSERT_EQ(g.cols, 2);
    ASSERT_EQ(g.rows, 1);
    const auto [h, v] = lr::transit_capacity(g);
    for (size_t t = 0; t < g.size(); ++t) {
      EXPECT_GT(g.box_cap[t], 0);  // M2 remains available for Final in both cases.
      EXPECT_GT(v[t], 0);
      if (m4_available) {
        EXPECT_GT(h[t], 0);
      } else {
        EXPECT_EQ(h[t], 0);
      }
    }
    auto            boxes = lr::build_boxes(d, g);
    lr::Routing     routes;
    Legroute_report report;
    if (!m4_available) {
      EXPECT_TRUE(plan.gates.requires_replan());
      EXPECT_EQ(plan.unrouted, 1u);
      EXPECT_EQ(plan.topology_disconnected, 1u);
      EXPECT_THROW(lr::sat_place(d, g, boxes, regions, routes, report), Legroute_replan);
      EXPECT_EQ(report.sat_regions, 0u);
      EXPECT_EQ(report.sat_solves, 0u);
      continue;
    }
    ASSERT_FALSE(plan.gates.requires_replan()) << legroute_gate_json(plan.gates);
    EXPECT_EQ(plan.unrouted, 0u);
    lr::sat_place(d, g, boxes, regions, routes, report);
    ASSERT_EQ(report.sat_unsat_regions + report.sat_unknown_regions, 0u);
    lr::pack_cells(d, g, boxes);
    lr::box_demand(d, g, boxes, routes);
    EXPECT_EQ(routes.cap_h, h);  // The box layer absorbs the pin stubs, not the interbox route.
    lr::assign_layers(d, g, routes);
    lr::assign_tracks(d, g, routes, 1);
    EXPECT_EQ(routes.unassigned, 0u);
    size_t horizontal_segments = 0;
    for (const auto& segment : routes.segments) {
      if (g.layers[size_t(segment.layer)].horizontal) {
        ++horizontal_segments;
        EXPECT_EQ(g.layers[size_t(segment.layer)].name, "M4");
        EXPECT_EQ(segment.track_state, uint8_t(lr::Track_state::checked));
      }
    }
    EXPECT_GT(horizontal_segments, 0u);
    std::fill(g.box_cap.begin(), g.box_cap.end(), 0);
    EXPECT_EQ(lr::transit_capacity(g).first, h);
    lr::Routing without_box_room;
    lr::box_demand(d, g, boxes, without_box_room);
    for (const auto& box : boxes.boxes) {
      EXPECT_EQ(without_box_room.cap_h[size_t(box.tile)], h[size_t(box.tile)] - 1);
    }
  }
}

TEST(Legroute, FailedPhysicalLocalMetalAuditSkipsFinalEvenWithAllTracksAssigned) {
  Fixture f(64, 4);
  f.fp.design  = "local_unknown_rules";
  const auto a = f.cell("dff", 0, 0), b = f.cell("dff", 1600, 0);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  for (const auto* layer : {"M4", "M6"}) {
    f.tech.layers[layer].rules.unsupported.insert("UNSUPPORTED_TEST_RULE");
  }
  Legroute_options opt;
  opt.threads     = 1;
  opt.clock_spine = opt.region_pins = false;
  opt.legal_boxes = opt.global_route = opt.global_all = opt.global_keep = true;
  opt.reserved_tracks                                                   = 0;
  opt.spill_rounds                                                      = 0;
  opt.final              = true;  // The known-unknown metal audit must prevent entry into Final.
  opt.l5_fallback_shadow = true;
  opt.output_dir         = "local_unknown_rules";
  std::filesystem::remove_all(opt.output_dir);
  std::filesystem::create_directories(opt.output_dir);
  const auto original = f.fp;
  const auto plan     = legroute_plan(f.tech, f.fp, f.timing, opt, {}, nullptr, 100000, true);
  ASSERT_FALSE(plan.gates.requires_replan());
  const auto report = legroute(f.tech, f.fp, f.timing, opt, {});
  EXPECT_EQ(report.sat_unsat_regions + report.sat_unknown_regions, 0u);
  EXPECT_EQ(report.unassigned_segments, 0u);
  EXPECT_TRUE(report.local_metal_audit_attempted);
  EXPECT_GT(report.local_metal_unknown_shapes, 0u);
  EXPECT_TRUE(report.local_metal_rejected());
  EXPECT_EQ(report.route_passes, 0u);
  EXPECT_FALSE(report.wires_emitted);
  EXPECT_EQ(report.unchecked_nets, 1u);
  ASSERT_FALSE(report.steps.empty());
  EXPECT_EQ(report.steps.back().name, "L5 layers, tracks");
  EXPECT_EQ(f.fp.nets, original.nets);
  std::ifstream     input(opt.output_dir + "/legroute.json");
  const std::string json{std::istreambuf_iterator<char>(input), {}};
  EXPECT_NE(json.find("\"local_metal_audit_attempted\":true"), std::string::npos);
  std::filesystem::remove_all(opt.output_dir);
}

TEST(Legroute, FailedPhysicalLocalHandoffSkipsFinalAndKeepsLegalPlacement) {
  Fixture    f(32, 2);
  const auto a = f.cell("dff", 0, 0), b = f.cell("dff", 800, 0);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  f.tech.layers["M4"].rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .020 EXTENSION .020 .500 .020 CORNERONLY ;"};
  Legroute_options opt;
  opt.threads     = 1;
  opt.clock_spine = opt.region_pins = false;
  opt.legal_boxes = opt.global_route = opt.global_all = opt.global_keep = true;
  opt.reserved_tracks                                                   = 0;
  opt.spill_rounds                                                      = 0;
  opt.final                                                             = true;
  Legroute_regions regions;
  regions.obstacles.push_back({"M6", f.fp.die});
  regions.obstacles.push_back({
      "M4",
      {0, 125, f.fp.die.xh, 145}
  });
  const auto original = f.fp;
  const auto plan     = legroute_plan(f.tech, f.fp, f.timing, opt, regions, nullptr, 100000, true);
  ASSERT_FALSE(plan.gates.requires_replan());
  const auto report = legroute(f.tech, f.fp, f.timing, opt, regions);
  EXPECT_EQ(report.sat_unsat_regions + report.sat_unknown_regions, 0u);
  EXPECT_GT(report.unassigned_segments, 0u);
  EXPECT_EQ(report.route_passes, 0u);
  EXPECT_FALSE(report.wires_emitted);
  EXPECT_EQ(report.routed_nets, 1u);
  EXPECT_EQ(report.unchecked_nets, report.routed_nets);
  ASSERT_FALSE(report.steps.empty());
  EXPECT_EQ(report.steps.back().name, "L5 layers, tracks");
  f.expect_legal();
  EXPECT_EQ(f.fp.nets, original.nets);
}

TEST(Legroute, RequiredStageFailureReportsTheSameClockAndNameExclusionsAsFinal) {
  for (bool expire_sat : {false, true}) {
    SCOPED_TRACE(expire_sat);
    Fixture    f(32, 2);
    const auto a = f.cell("dff", 0, 0), b = f.cell("dff", 800, 0);
    f.net({
        {a, "Q"},
        {b, "D"}
    });
    f.net({
        {b, "Q"},
        {a, "D"}
    });
    f.fp.nets.back().name = "top/SoFt_ReSeT_N";
    f.net(
        {
            {a, "CLK"},
            {b, "CLK"}
    },
        0,
        "CLOCK");
    f.tech.layers["M4"].rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT .020 EXTENSION .020 .500 .020 CORNERONLY ;"};
    Legroute_options opt;
    opt.threads     = 1;
    opt.clock_spine = opt.region_pins = false;
    opt.legal_boxes = opt.global_route = opt.global_all = opt.global_keep = true;
    opt.reserved_tracks                                                   = 0;
    opt.spill_rounds                                                      = 0;
    opt.final                                                             = true;
    opt.final_skip_clock                                                  = true;
    opt.final_skip_nets                                                   = "reset";
    if (expire_sat) {
      opt.sat_ms = 0;
    }
    Legroute_regions regions;
    regions.obstacles.push_back({"M6", f.fp.die});
    regions.obstacles.push_back({
        "M4",
        {0, 125, f.fp.die.xh, 145}
    });
    const auto original = f.fp;
    const auto report   = legroute(f.tech, f.fp, f.timing, opt, regions);
    if (expire_sat) {
      EXPECT_GT(report.sat_unknown_regions, 0u);
      EXPECT_EQ(report.sat_unsat_regions, 0u);
    } else {
      EXPECT_EQ(report.sat_unsat_regions + report.sat_unknown_regions, 0u);
      EXPECT_GT(report.unassigned_segments, 0u);
    }
    EXPECT_EQ(report.route_passes, 0u);
    EXPECT_FALSE(report.wires_emitted);
    EXPECT_EQ(report.routed_nets, 1u);
    EXPECT_EQ(report.unchecked_nets, 1u);
    EXPECT_EQ(report.final_skipped_nets, 2u);
    EXPECT_EQ(report.clock_required_nets, 0u);
    EXPECT_EQ(report.clock_unchecked_nets, 0u);
    ASSERT_FALSE(report.steps.empty());
    EXPECT_EQ(report.steps.back().name, expire_sat ? "L3 SAT placement" : "L5 layers, tracks");
    EXPECT_EQ(f.fp.nets, original.nets);
    if (expire_sat) {
      opt.final             = false;
      const auto local_only = legroute(f.tech, f.fp, f.timing, opt, regions);
      EXPECT_GT(local_only.sat_unknown_regions, 0u);
      EXPECT_EQ(local_only.routed_nets, 3u);
      EXPECT_EQ(local_only.unchecked_nets, 3u);
      EXPECT_EQ(local_only.final_skipped_nets, 0u);
      EXPECT_EQ(local_only.clock_required_nets, 1u);
      EXPECT_EQ(local_only.clock_unchecked_nets, 1u);
      EXPECT_EQ(local_only.route_passes, 0u);
      EXPECT_EQ(f.fp.nets, original.nets);
    }
  }
}

TEST(Legroute, GlobalCapacityAndDisconnectionReturnToPlanningBeforeSat) {
  Fixture    f(32, 2);
  const auto a = f.cell("dff", 0, 0), b = f.cell("dff", 400, 0);
  const auto c = f.cell("dff", 800, 0), e = f.cell("dff", 1200, 0);
  f.net({
      {a, "Q"},
      {c, "D"}
  });
  f.net({
      {b, "Q"},
      {e, "D"}
  });
  Legroute_regions regions;
  regions.rects = {
      {  0, 0,  800, 500},
      {800, 0, 1600, 500}
  };
  for (int tracks : {0, 1, 2, 3}) {
    for (bool strict : {false, true}) {
      Legroute_options opt;
      opt.threads      = 1;
      opt.clock_spine  = false;
      opt.legal_boxes  = true;
      opt.global_route = opt.global_all = opt.global_keep = true;
      opt.global_share                                    = 1;
      opt.region_pins                                     = false;
      opt.fail_fast                                       = strict;
      opt.spill_rounds                                    = 0;
      opt.final                                           = false;
      auto d                                              = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
      auto g                                              = lr::build_grid(d, regions);
      ASSERT_EQ(g.cols, 2);
      ASSERT_EQ(g.rows, 1);
      for (auto& cap : g.cap) {
        std::fill(cap.begin(), cap.end(), 0);
      }
      const auto horizontal = std::ranges::find(g.layers, true, &lr::Layer::horizontal);
      ASSERT_NE(horizontal, g.layers.end());
      std::fill(g.cap[size_t(horizontal - g.layers.begin())].begin(), g.cap[size_t(horizontal - g.layers.begin())].end(), tracks);
      auto            boxes = lr::build_boxes(d, g);
      lr::Routing     routes;
      Legroute_report rep;
      if (tracks < 3) {
        try {
          lr::sat_place(d, g, boxes, regions, routes, rep);
          FAIL() << "a failed 95% coarse screen entered SAT";
        } catch (const Legroute_replan& error) {
          EXPECT_TRUE(error.gates.requires_replan());
          EXPECT_EQ(error.gates.search_limited, 0u);
          EXPECT_EQ(error.gates.topology_disconnected, tracks < 2 ? 2u : 0u);
          EXPECT_EQ(error.gates.routing_dense_edges, tracks == 2 ? 1u : 0u);
          EXPECT_FALSE(error.gates.hotspots.empty());
        }
        EXPECT_EQ(rep.sat_regions, 0u);
        EXPECT_EQ(rep.sat_solves, 0u);
      } else {
        lr::sat_place(d, g, boxes, regions, routes, rep);
        EXPECT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
        EXPECT_EQ(rep.global_overflow_edges, 0u);
      }
    }
  }
}

TEST(Legroute, CongestionAndEveryOversizedPartitionReturnToPlanningBeforeSat) {
  struct Case {
    int      tracks;
    unsigned target;
    bool     protect_critical, recover;
    unsigned cells = 4;
  };
  for (bool mesh : {false, true}) {
    for (auto test : {
             Case{2, 2, false, true},
             Case{2, 4, false, true},
             Case{3, 2, false, false},
             Case{3, 2, true, true},
             Case{3, 4, true, true, 6},
             Case{3, 1, false, true}
    }) {
      SCOPED_TRACE(std::format("mesh={} tracks={} target={} protect={}", mesh, test.tracks, test.target, test.protect_critical));
      Fixture    f(int(test.cells * 8), 2);
      const auto a = f.cell("dff", 0, 0), b = f.cell("dff", 400, 0);
      const auto c = f.cell("dff", 800, 0), e = f.cell("dff", 1200, 0);
      f.net({
          {a, "Q"},
          {c, "D"}
      });
      f.net({
          {b, "Q"},
          {e, "D"}
      });
      if (test.cells == 6) {
        const auto last = f.cell("dff", 1600, 0);
        f.cell("dff", 2000, 0);
        f.net({
            {   c, "Q"},
            {last, "D"}
        });
        // Critical protection may join cells across within-capacity edges, but
        // the resulting region must still honor the configured model target.
      }
      const auto       before = f.fp.components;
      Legroute_options opt;
      opt.threads     = 1;
      opt.legal_boxes = opt.global_route = true;
      opt.global_share                   = 1;
      opt.partition_cells                = test.target;
      opt.partition_mesh                 = mesh;
      opt.partition_critical             = test.protect_critical ? 1 : 0;
      opt.global_keep = opt.clock_spine = opt.region_pins = opt.final = opt.fail_fast = false;
      opt.spill_rounds                                                                = 0;
      Legroute_regions regions;
      auto             d = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
      auto             g = lr::build_grid(d, regions);
      ASSERT_EQ(g.cols, int(test.cells / 2));
      ASSERT_EQ(g.rows, 1);
      for (auto& cap : g.cap) {
        std::fill(cap.begin(), cap.end(), 0);
      }
      const auto horizontal = std::ranges::find(g.layers, true, &lr::Layer::horizontal);
      ASSERT_NE(horizontal, g.layers.end());
      std::fill(g.cap[size_t(horizontal - g.layers.begin())].begin(),
                g.cap[size_t(horizontal - g.layers.begin())].end(),
                test.tracks);
      auto                   boxes = lr::build_boxes(d, g);
      const std::vector<int> cap_h(g.size(), test.tracks), cap_v(g.size(), 0);
      const auto             coarse = lr::route_all_signals(d, g, boxes, cap_h, cap_v);
      ASSERT_EQ(coarse.unrouted, 0u);
      ASSERT_EQ(coarse.search_limited, 0u);
      EXPECT_EQ(coarse.overflow_edges, test.tracks == 2 ? 1u : 0u);
      lr::Routing     routes;
      Legroute_report rep;
      if (test.recover) {
        try {
          lr::sat_place(d, g, boxes, regions, routes, rep);
          FAIL() << "congestion or any oversized region must return to planning";
        } catch (const Legroute_replan& error) {
          if (test.tracks == 2) {
            EXPECT_GT(error.gates.routing_dense_edges, 0u);
          } else {
            EXPECT_GT(error.gates.oversized_regions, 0u);
          }
          EXPECT_FALSE(error.gates.affected_components.empty());
        }
        EXPECT_EQ(rep.sat_regions, 0u) << "recovery must precede regional SAT construction";
      } else {
        EXPECT_NO_THROW(lr::sat_place(d, g, boxes, regions, routes, rep));
        EXPECT_GT(rep.sat_regions, 0u);
        EXPECT_LE(rep.partition_max_cells, test.target);
      }
      EXPECT_EQ(f.fp.components, before);
    }
  }
}

TEST(Legroute, WorkerExceptionsReachTheCaller) {
  EXPECT_THROW(lr::parallel(1, 2, [](size_t) { throw std::runtime_error("worker failure"); }), std::runtime_error);
  std::atomic<size_t> completed = 0;
  lr::parallel(8, 2, [&](size_t) { ++completed; });
  EXPECT_EQ(completed.load(), 8u);
}

TEST(Legroute, RejectsInvalidPolicyNumbers) {
  Legroute_options opt;
  for (const auto* value : {"-1", "1.5", "nan", "inf", "4294967296", "2junk"}) {
    for (const auto* key : {"tree_moves", "window_tiles"}) {
      opt.sat_alt = std::string(key) + "=" + value;
      EXPECT_THROW(validate_legroute_options(opt), std::invalid_argument) << opt.sat_alt;
    }
  }
  opt.sat_alt = "tree_moves=0,window_tiles=4";
  EXPECT_NO_THROW(validate_legroute_options(opt));
  opt.sat_alt.clear();
  for (double value : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    opt.cap_scale = value;
    EXPECT_THROW(validate_legroute_options(opt), std::invalid_argument);
  }
  opt.cap_scale = 1;
  for (double value : {-2.0, -.5, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    opt.reserved_tracks_per_um = value;
    EXPECT_THROW(validate_legroute_options(opt), std::invalid_argument);
  }
  for (double value : {-1.0, 0.0, .5}) {
    opt.reserved_tracks_per_um = value;
    EXPECT_NO_THROW(validate_legroute_options(opt));
  }
  opt.reserved_tracks_per_um   = -1;
  opt.partition_timeout_rounds = 1;
  EXPECT_THROW(validate_legroute_options(opt), std::invalid_argument);
  opt.partition_cells = 10;
  opt.legal_boxes = opt.global_route = true;
  opt.region_pins                    = false;
  EXPECT_NO_THROW(validate_legroute_options(opt));
  opt.hop_choice = 1;
  EXPECT_THROW(validate_legroute_options(opt), std::invalid_argument);
}

TEST(Legroute, ReportFailureRollsBackPlacementAndRouting) {
  Fixture f;
  f.random_design(12, 4, 3);
  const auto before  = f.fp;
  const auto blocker = std::filesystem::path(testing::TempDir()) / "legroute_report_blocker";
  {
    std::ofstream file(blocker);
    ASSERT_TRUE(file.good());
  }
  Legroute_options opt;
  opt.threads    = 1;
  opt.output_dir = blocker.string();  // A regular file cannot contain legroute.json.
  EXPECT_THROW(legroute(f.tech, f.fp, f.timing, opt), std::runtime_error);
  EXPECT_EQ(f.fp.components, before.components);
  EXPECT_EQ(f.fp.nondefault_rules, before.nondefault_rules);
  ASSERT_EQ(f.fp.nets.size(), before.nets.size());
  for (size_t n = 0; n < f.fp.nets.size(); ++n) {
    EXPECT_EQ(f.fp.nets[n].wires, before.nets[n].wires);
  }
  std::filesystem::remove(blocker);
}

TEST(Legroute, FailureLeavesTheFloorplanUnchanged) {
  Fixture f(20, 2);  // one tile row of two tiles cannot hold six flops
  for (int k = 0; k < 6; ++k) {
    f.cell("dff", 0, 0);
  }
  const auto       before = f.fp;
  Legroute_options opt;
  EXPECT_THROW(legroute(f.tech, f.fp, f.timing, opt), std::runtime_error);
  EXPECT_EQ(f.fp.components, before.components);
}

// The SAT placement's guarantee: per tile edge and direction, the nets whose chosen legs cross the edge fit the tracks
// of the narrower tile beside it.
TEST(Legroute, SatPlacementFitsTheTracks) {
  for (unsigned reserved : {1u, 7u}) {  // 7 of the fixture's 8-10 tracks per layer reserved: the capacity binds
    Fixture f;
    f.add_horizontal_transit_layer();
    f.random_design(12, 4, 3);
    Legroute_options opt;
    opt.threads           = 1;
    opt.reserved_tracks   = reserved;
    opt.clock_spine       = false;
    opt.global_share      = .95;
    const auto      d     = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
    auto            g     = lr::build_grid(d, {});
    auto            boxes = lr::build_boxes(d, g);
    lr::Routing     R;
    Legroute_report rep;
    lr::sat_place(d, g, boxes, {}, R, rep);
    ASSERT_EQ(rep.sat_regions, 1u);
    if (rep.sat_unsat_regions) {
      continue;  // no routable placement exists under this reservation: reported, never violated
    }
    std::set<int> used;
    for (const auto& b : boxes.boxes) {
      EXPECT_TRUE(used.insert(b.tile).second) << "two boxes on tile " << b.tile;
    }
    std::map<std::pair<int, bool>, std::set<int>> cross;  // (edge: its left / lower tile, horizontal) -> nets
    for (const auto& c : R.conns) {
      for (size_t k = 0; k + 1 < c.poly.size(); ++k) {
        const auto [c0, r0] = c.poly[k];
        const auto [c1, r1] = c.poly[k + 1];
        if (r0 == r1) {
          for (int cc = std::min(c0, c1); cc < std::max(c0, c1); ++cc) {
            cross[{g.tile(cc, r0), true}].insert(c.net);
          }
        } else {
          for (int rr = std::min(r0, r1); rr < std::max(r0, r1); ++rr) {
            cross[{g.tile(c0, rr), false}].insert(c.net);
          }
        }
      }
    }
    const auto tracks = [&](int t, bool h) {
      int cap = 0;  // only layers that L5 can assign carry transit
      for (size_t l = 0; l < g.layers.size(); ++l) {
        cap += g.layers[l].horizontal == h ? g.cap[l][t] : 0;
      }
      return cap;
    };
    for (const auto& [key, nets] : cross) {
      const int t = key.first, q = key.second ? t + 1 : t + g.cols;
      EXPECT_LE(int(nets.size()), std::min(tracks(t, key.second), tracks(q, key.second)))
          << "edge of tile " << t << (key.second ? " H" : " V");
    }
  }
}

// The cut bound: nets whose endpoints are held on both sides of a tile boundary beyond the tracks of its edges make the
// region unsat before any solving. Raw physical capacity passes the coarse screen;
// a deliberately conservative SAT calibration creates the regional cut deficit.
TEST(Legroute, CutBoundIsUnsatBeforeSolving) {
  Fixture f(240, 4);  // a 12 um strip: 15 tile columns of 0.8 um, 2 tile rows
  f.add_horizontal_transit_layer();
  std::vector<size_t> left, right;
  for (int row = 0; row < 4; ++row) {
    for (int k = 0; k < 4; ++k) {
      left.push_back(f.cell("dff", 400 * k, 250 * row));
      right.push_back(f.cell("dff", 10400 + 400 * k, 250 * row));
    }
  }
  for (size_t k = 0; k < left.size(); ++k) {
    f.net({
        { left[k], "Q"},
        {right[k], "D"}
    });
  }
  Legroute_options opt;
  opt.threads           = 1;
  opt.reserved_tracks   = 1;
  opt.cap_scale         = .1;
  opt.clock_spine       = false;
  opt.merge_cost        = 2;  // a flop never joins its partner's box across the strip (cost 8); neighbors pack as usual
  const auto      d     = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  auto            g     = lr::build_grid(d, {});
  auto            boxes = lr::build_boxes(d, g);
  lr::Routing     R;
  Legroute_report rep;
  lr::sat_place(d, g, boxes, {}, R, rep);
  EXPECT_EQ(rep.sat_regions, 1u);
  EXPECT_EQ(rep.sat_unsat_regions, 1u);
  EXPECT_EQ(rep.sat_cut_regions, 1u) << "the bound, not a solver run, must reject it";
  EXPECT_EQ(rep.sat_solves, 0u);
  // The public strict flow must report this required-stage failure without
  // spending time on clocks, track assignment or final physical routing.
  opt.fail_fast = true;
  f.net(
      {
          { left.front(), "CLK"},
          {right.front(), "CLK"}
  },
      0,
      "CLOCK");
  const auto original = f.fp.components;
  const auto stopped  = legroute(f.tech, f.fp, f.timing, opt);
  EXPECT_EQ(stopped.sat_unsat_regions, 1u);
  EXPECT_EQ(stopped.route_passes, 0u);
  EXPECT_EQ(stopped.routed_nets, left.size() + 1);
  EXPECT_EQ(stopped.unchecked_nets, stopped.routed_nets);
  EXPECT_EQ(stopped.clock_required_nets, 1u);
  EXPECT_EQ(stopped.clock_unchecked_nets, 1u);
  EXPECT_EQ(stopped.clock_connected_nets, 0u);
  EXPECT_FALSE(stopped.wires_emitted);
  ASSERT_FALSE(stopped.steps.empty());
  EXPECT_EQ(stopped.steps.back().name, "L3 SAT placement");
  EXPECT_EQ(f.fp.components, original);
  EXPECT_TRUE(std::all_of(f.fp.nets.begin(), f.fp.nets.end(), [](const auto& net) { return net.wires.empty(); }));
  std::set<int> used;  // the boxes still have distinct tiles (the fallback placement)
  for (const auto& b : boxes.boxes) {
    EXPECT_TRUE(used.insert(b.tile).second) << "two boxes on tile " << b.tile;
  }
}

// A two-row cell (a domino master, a tile tall) moves as a box like any cell: it lands on a tile's bottom row with
// that row's orientation, takes the same sites in both rows, and the single-row cells of its box pack beside it.
TEST(Legroute, TwoRowCellsPlaceAsTileTallBoxes) {
  Fixture f(200, 8);
  f.tech.macros["dom"]        = f.tech.macros.at("dff");  // 0.8 um wide (a tile), two rows tall
  f.tech.macros["dom"].width  = .8;
  f.tech.macros["dom"].height = .5;
  std::vector<size_t> doms, invs;
  for (int k = 0; k < 6; ++k) {
    doms.push_back(f.cell("dom", 1000 + 1200 * k, 500 * (k % 3)));
    invs.push_back(f.cell("inv", 1100 + 1200 * k, 250 + 500 * (k % 3)));
    f.net({
        {doms[k], "Q"},
        {invs[k], "A"}
    });
  }
  Legroute_options opt;
  opt.threads     = 1;
  opt.clock_spine = false;
  const auto d    = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  ASSERT_EQ(d.movable.size(), 12u);
  EXPECT_EQ(std::count(d.tall.begin(), d.tall.end(), 1), 6);
  auto g = lr::build_grid(d, {});
  EXPECT_EQ(g.tile_w, 800) << "two single-row flops wide, not the two-row cell";
  auto            boxes = lr::build_boxes(d, g);
  lr::Routing     R;
  Legroute_report rep;
  lr::sat_place(d, g, boxes, {}, R, rep);
  ASSERT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
  lr::pack_cells(d, g, boxes);
  std::vector<std::tuple<int64_t, int64_t, int64_t, int64_t>> rects;  // every movable cell's placed rectangle
  for (int m : d.movable) {
    const auto& c = f.fp.components[size_t(m)];
    const auto  w = std::llround(f.tech.macros.at(c.macro).width * 1000), h = std::llround(f.tech.macros.at(c.macro).height * 1000);
    if (h > 250) {
      EXPECT_EQ((c.place.y - g.y0) % g.tile_h, 0) << c.name << " starts at a tile's bottom row";
      EXPECT_EQ(c.place.orientation, g.row_orient[size_t(2 * g.row(g.tile_of(double(c.place.x + 1), double(c.place.y + 1))))])
          << c.name;
    }
    for (const auto& [x0, y0, x1, y1] : rects) {
      EXPECT_FALSE(c.place.x < x1 && x0 < c.place.x + w && c.place.y < y1 && y0 < c.place.y + h) << c.name << " overlaps a cell";
    }
    rects.emplace_back(c.place.x, c.place.y, c.place.x + w, c.place.y + h);
  }
}

// hop_choice: the hops between two regions are chosen by the first wave's region among a few boundary tiles and are
// the second wave's fixed endpoints; both regions still have an answer and the report counts the waves and free hops.
TEST(Legroute, HopChoiceSolvesInTwoWaves) {
  Fixture f(400, 8);  // a 20 x 2 um core: two regions of 10 um
  f.add_horizontal_transit_layer();
  f.random_design(24, 4, 5);
  Legroute_options opt;
  opt.threads        = 1;
  opt.clock_spine    = false;
  opt.hop_choice     = 2;
  opt.global_share   = .95;
  const auto       d = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  Legroute_regions regions;
  regions.rects = {
      {    0, 0, 10000, 2000},
      {10000, 0, 20000, 2000}
  };
  auto            g     = lr::build_grid(d, regions);
  auto            boxes = lr::build_boxes(d, g);
  lr::Routing     R;
  Legroute_report rep;
  lr::sat_place(d, g, boxes, regions, R, rep);
  EXPECT_EQ(rep.sat_regions, 2u);
  EXPECT_EQ(rep.sat_waves, 2u);
  EXPECT_GT(rep.sat_hops_free, 0u);
  EXPECT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
  std::set<int> used;  // the boxes have distinct tiles
  for (const auto& b : boxes.boxes) {
    EXPECT_TRUE(used.insert(b.tile).second) << "two boxes on tile " << b.tile;
  }
}

// The low-level entry point must honor region_pins too: the router's crossings are independent of refinement
// anchors when disabled, while enabled anchors remain endpoints even when far from the net's shortest path.
TEST(Legroute, GlobalCrossingsHonorRegionPins) {
  Fixture    f(400, 8);
  const auto a = f.cell("dff", 800, 0), b = f.cell("dff", 15200, 0);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  Legroute_options opt;
  opt.threads      = 1;
  opt.clock_spine  = false;
  opt.global_route = true;
  opt.legal_boxes  = true;
  opt.tiers.clear();
  Legroute_regions regions;
  regions.rects = {
      {    0, 0, 10000, 2000},
      {10000, 0, 20000, 2000}
  };
  regions.pins = {
      {0, 10000, 1750}
  };
  for (bool fixed : {false, true}) {
    opt.region_pins       = fixed;
    const auto      d     = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
    auto            g     = lr::build_grid(d, regions);
    auto            boxes = lr::build_boxes(d, g);
    lr::Routing     R;
    Legroute_report rep;
    lr::sat_place(d, g, boxes, regions, R, rep);
    ASSERT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
    EXPECT_EQ(rep.global_unrouted, 0u);
    EXPECT_EQ(rep.global_nets, 1u);
    std::set<int> crossing_rows;
    for (const auto& c : R.conns) {
      const auto& nodes = R.trees[size_t(c.net)].nodes;
      const int   ta = nodes[size_t(c.a)].tile, tb = nodes[size_t(c.b)].tile;
      if (g.region[size_t(ta)] != g.region[size_t(tb)]) {
        crossing_rows.insert(g.row(ta));
      }
    }
    EXPECT_FALSE(crossing_rows.empty());
    if (fixed) {
      EXPECT_TRUE(crossing_rows.contains(3));
    } else {
      EXPECT_EQ(crossing_rows, std::set<int>{0});
    }
  }
}

TEST(Legroute, FixedInterfaceRerouteMustPassGlobalAdmissionBeforeSat) {
  Fixture    f(64, 8);
  const auto local_a = f.cell("inv", 0, 0), local_b = f.cell("inv", 100, 0);
  f.net({
      {local_a, "Y"},
      {local_b, "A"}
  });  // not in the compact crossing-router request list
  const auto a = f.cell("dff", 800, 0), b = f.cell("dff", 1600, 0);
  f.net({
      {a, "Q"},
      {b, "D"}
  });
  Legroute_options opt;
  opt.threads     = 1;
  opt.clock_spine = opt.final = false;
  opt.legal_boxes = opt.global_route = true;
  opt.global_all                     = false;
  opt.global_rounds                  = 0;
  opt.global_net_pops                = 2;
  opt.tiers.clear();
  Legroute_regions regions;
  regions.rects = {
      {   0, 0, 1600, 2000},
      {1600, 0, 3200, 2000}
  };
  regions.pins = {
      {1, 1600, 1750}
  };  // distant mandatory interface, beyond the two-pop budget
  const auto original_cells = f.fp.components;
  const auto original_nets  = f.fp.nets;
  for (bool strict : {false, true}) {
    opt.fail_fast   = strict;
    opt.region_pins = true;
    opt.output_dir  = std::string(std::getenv("TEST_TMPDIR")) + (strict ? "/fixed_interface_strict" : "/fixed_interface_relaxed");
    const auto       design = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
    auto             grid   = lr::build_grid(design, regions);
    auto             boxes  = lr::build_boxes(design, grid);
    std::vector<int> h(grid.size()), v(grid.size());
    for (size_t layer = 0; layer < grid.layers.size(); ++layer) {
      for (size_t tile = 0; tile < grid.size(); ++tile) {
        (grid.layers[layer].horizontal ? h : v)[tile] += grid.cap[layer][tile];
      }
    }
    const auto prepass = lr::route_all_signals(design, grid, boxes, h, v);
    ASSERT_EQ(prepass.unrouted, 0u);
    ASSERT_FALSE(lr::assess_legroute_gates(design, grid, &boxes, prepass, {}, h, v, true).requires_replan());
    lr::Routing     routes;
    Legroute_report report;
    EXPECT_THROW(lr::sat_place(design, grid, boxes, regions, routes, report), Legroute_replan);
    EXPECT_EQ(report.sat_regions, 0u);
    EXPECT_EQ(report.sat_solves, 0u);
    EXPECT_FALSE(report.wires_emitted);
    EXPECT_TRUE(routes.conns.empty());
    try {
      (void)legroute(f.tech, f.fp, f.timing, opt, regions);
      FAIL() << "secondary fixed-interface global failure entered SAT";
    } catch (const Legroute_replan& e) {
      EXPECT_EQ(e.gates.stage, Legroute_gate_report::Stage::secondary);
      EXPECT_EQ(e.gates.unrouted, 1u);
      EXPECT_EQ(e.gates.search_limited, 1u);
      EXPECT_EQ(e.gates.routing_dense_edges, 0u);
      EXPECT_EQ(e.gates.affected_components, (std::vector<size_t>{a, b}));
      EXPECT_TRUE(std::ranges::any_of(e.gates.hotspots, [](const auto& p) { return p.yh >= 1750; }));
    }
    EXPECT_EQ(f.fp.components, original_cells);
    EXPECT_EQ(f.fp.nets, original_nets);
    std::ifstream     saved(opt.output_dir + "/pre_sat.json");
    const std::string text{std::istreambuf_iterator<char>(saved), {}};
    EXPECT_NE(text.find("\"requires_replan\":true"), std::string::npos);
    EXPECT_NE(text.find("\"stage\":\"secondary\""), std::string::npos);
    EXPECT_NE(text.find("\"search_limited\":1"), std::string::npos);
  }
  // The same physical pose and budget pass when the distant fixed anchor is removed.
  opt.region_pins = false;
  opt.output_dir.clear();
  const auto control = legroute(f.tech, f.fp, f.timing, opt, regions);
  EXPECT_EQ(control.global_unrouted, 0u);
  EXPECT_EQ(control.sat_unsat_regions + control.sat_unknown_regions, 0u);
  EXPECT_GT(control.sat_solves, 0u);
}

TEST(Legroute, FixedInterfaceOvercapacityStopsBeforeSatAndCountsEachNetOnce) {
  Fixture f(64, 8);
  for (int row : {0, 1}) {
    const auto a = f.cell("dff", 800, row * 250), b = f.cell("dff", 1600, row * 250);
    f.net({
        {a, "Q"},
        {b, "D"}
    });
  }
  Legroute_options opt;
  opt.threads     = 1;
  opt.clock_spine = opt.final = false;
  opt.legal_boxes = opt.global_route = true;
  opt.global_all                     = false;
  opt.global_rounds                  = 0;
  opt.tiers.clear();
  Legroute_regions regions;
  regions.rects = {
      {   0, 0, 1600, 2000},
      {1600, 0, 3200, 2000}
  };
  regions.pins = {
      {0, 1600, 1750},
      {1, 1600, 1750}
  };
  const auto original_cells = f.fp.components;
  for (int tracks : {1, 3, 4}) {
    SCOPED_TRACE(tracks);
    const auto       design = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
    auto             grid   = lr::build_grid(design, regions);
    auto             boxes  = lr::build_boxes(design, grid);
    std::vector<int> h(grid.size()), v(grid.size());
    const auto       horizontal = std::ranges::find(grid.layers, true, &lr::Layer::horizontal);
    ASSERT_NE(horizontal, grid.layers.end());
    for (size_t layer = 0; layer < grid.layers.size(); ++layer) {
      for (size_t tile = 0; tile < grid.size(); ++tile) {
        if (grid.layers[layer].horizontal && grid.row(int(tile)) == 3) {
          grid.cap[layer][tile] = layer == size_t(horizontal - grid.layers.begin()) ? tracks : 0;
        }
        (grid.layers[layer].horizontal ? h : v)[tile] += grid.cap[layer][tile];
      }
    }
    const auto prepass = lr::route_all_signals(design, grid, boxes, h, v);
    ASSERT_EQ(prepass.unrouted, 0u);
    ASSERT_FALSE(lr::assess_legroute_gates(design, grid, &boxes, prepass, {}, h, v, true).requires_replan());
    lr::Routing     routes;
    Legroute_report report;
    if (tracks < 4) {
      try {
        lr::sat_place(design, grid, boxes, regions, routes, report);
        FAIL() << "fixed interfaces overload the secondary global route";
      } catch (const Legroute_replan& e) {
        EXPECT_EQ(e.gates.unrouted, 0u);
        EXPECT_EQ(e.gates.search_limited, 0u);
        EXPECT_GT(e.gates.overflow_tracks, 0u);
        if (tracks == 1) {
          EXPECT_GT(e.gates.routing_dense_edges, 0u);
          EXPECT_DOUBLE_EQ(e.gates.max_routing_density, 2);
        } else {
          // Two mandatory hops fit the raw 95% ceiling, but exceed the one
          // track allowed by the configured .5 share. The prepass omitted them.
          EXPECT_EQ(e.gates.routing_dense_edges, 0u);
          EXPECT_DOUBLE_EQ(e.gates.max_routing_density, 2. / 3);
        }
      }
      EXPECT_EQ(report.sat_regions, 0u);
      EXPECT_EQ(report.sat_solves, 0u);
      EXPECT_TRUE(routes.conns.empty());
      EXPECT_FALSE(report.wires_emitted);
    } else {
      // Four raw tracks allow two distinct nets at .5 share. A fixed hop
      // already in its routed tree must not be charged a second time.
      EXPECT_NO_THROW(lr::sat_place(design, grid, boxes, regions, routes, report));
      EXPECT_EQ(report.global_unrouted, 0u);
      EXPECT_EQ(report.sat_unsat_regions + report.sat_unknown_regions, 0u);
      EXPECT_GT(report.sat_solves, 0u);
    }
    EXPECT_EQ(f.fp.components, original_cells);
  }
}

// Window-only retries keep the global problem unchanged; spilling boxes must rebuild it. Exercise both in one
// failing boundary fixture, including the eventual routable answer after boxes move to the neighbouring region.
TEST(Legroute, LocalWindowRetriesReuseCrossingsUntilBoxesSpill) {
  Fixture f(400, 8);
  // The 47 DBU pitch keeps 20 DBU width/spacing legal and restores the outer-band track lost to die-edge clipping.
  f.add_horizontal_transit_layer(47);
  for (int col = 0; col < 6; ++col) {
    for (int row = 0; row < 4; ++row) {
      const auto a                    = f.cell("dff", 800 * col + 200, 500 * row);
      const auto b                    = f.cell("dff", 10400 + 1600 * col + 200, 500 * row + 250);
      f.fp.components[b].place.status = ph::Status::FIXED;
      f.net({
          {a, "Q"},
          {b, "D"}
      });
    }
  }
  Legroute_options opt;
  opt.threads         = 1;
  opt.clock_spine     = false;
  opt.reserved_tracks = 1;  // coarse physical capacity passes; the regional model deliberately retains less
  opt.cap_scale       = .25;
  opt.spill_rounds    = 8;
  opt.spill_fraction  = .2;
  opt.window_tiles    = 1;
  opt.window_grow     = 1;
  opt.global_route    = true;
  opt.global_rounds   = 2;
  const auto       d  = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  Legroute_regions regions;
  regions.rects = {
      {    0, 0, 10000, 2000},
      {10000, 0, 20000, 2000}
  };
  auto            g     = lr::build_grid(d, regions);
  auto            boxes = lr::build_boxes(d, g);
  lr::Routing     R;
  Legroute_report rep;
  lr::sat_place(d, g, boxes, regions, R, rep);
  EXPECT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
  EXPECT_GT(rep.windows_grown, 0u);
  EXPECT_GT(rep.spilled_boxes, 0u);
  EXPECT_GT(rep.crossings_built, 1u);
  EXPECT_EQ(rep.crossings_reused, 2u);
  EXPECT_EQ(rep.crossings_built + rep.crossings_reused, rep.spill_rounds + 1);
}

// A region whose boxes all talk to fixed cells in the neighbour cannot route (more hops than the boundary has
// tracks) until the spill rounds push boxes out: min cuts inside the failing region (VieCut) pick the boxes and the
// neighbour they go to (the one their nets reach), and the regions that changed solve again.
TEST(Legroute, SpillRoundsPushAFailingRegionsBoxesOut) {
  Fixture f(400, 8);  // a 20 x 2 um core: two regions of 10 um (96 and 104 tiles), a 4-tile boundary
  // The 47 DBU pitch keeps 20 DBU width/spacing legal and restores the outer-band track lost to die-edge clipping.
  f.add_horizontal_transit_layer(47);
  std::vector<size_t> movable, fixed;
  for (int col = 0; col < 6; ++col) {
    for (int row = 0; row < 4; ++row) {
      movable.push_back(f.cell("dff", 800 * col + 200, 500 * row));               // the left region's flops
      fixed.push_back(f.cell("dff", 10400 + 1600 * col + 200, 500 * row + 250));  // fixed flops spread over the right one
      f.fp.components[fixed.back()].place.status = ph::Status::FIXED;
    }
  }
  for (size_t k = 0; k < movable.size(); ++k) {  // 24 nets across the boundary: 4 tiles x 6 tracks cannot take them
    f.net({
        {movable[k], "Q"},
        {  fixed[k], "D"}
    });
  }
  Legroute_options opt;
  opt.threads         = 1;
  opt.clock_spine     = false;
  opt.reserved_tracks = 1;   // physical tracks remain feasible under the pre-SAT screen
  opt.cap_scale       = .2;  // local model pressure still requires spilling
  opt.spill_rounds    = 6;
  opt.spill_fraction  = .2;
  const auto       d  = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  Legroute_regions regions;
  regions.rects = {
      {    0, 0, 10000, 2000},
      {10000, 0, 20000, 2000}
  };
  auto            g     = lr::build_grid(d, regions);
  auto            boxes = lr::build_boxes(d, g);
  lr::Routing     R;
  Legroute_report rep;
  lr::sat_place(d, g, boxes, regions, R, rep);
  EXPECT_EQ(rep.sat_regions, 2u);
  EXPECT_GE(rep.spill_rounds, 1u) << boxes.boxes.size() << " boxes";
  EXPECT_GT(rep.spilled_boxes, 0u);
  EXPECT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
  std::set<int> used;
  std::set<int> in_region;
  for (const auto& b : boxes.boxes) {
    EXPECT_TRUE(used.insert(b.tile).second) << "two boxes on tile " << b.tile;
    in_region.insert(g.region[size_t(b.tile)]);
  }
  EXPECT_EQ(in_region.size(), 2u) << "the right region took boxes";
}

// The same failure, mended by moving the boundary: the failing region hands the tile strip next to the neighbour its
// nets reach to it, boxes included, so those nets' endpoints become the neighbour's without touching its fill.
TEST(Legroute, SpillShiftMovesTheBoundaryWithItsBoxes) {
  Fixture f(400, 8);
  // The 47 DBU pitch keeps 20 DBU width/spacing legal and restores the outer-band track lost to die-edge clipping.
  f.add_horizontal_transit_layer(47);
  std::vector<size_t> movable, fixed;
  for (int col = 6; col < 12; ++col) {  // the left region's flops sit next to the boundary
    for (int row = 0; row < 4; ++row) {
      movable.push_back(f.cell("dff", 800 * col + 200, 500 * row));
      fixed.push_back(f.cell("dff", 10400 + 1600 * (col - 6) + 200, 500 * row + 250));
      f.fp.components[fixed.back()].place.status = ph::Status::FIXED;
    }
  }
  for (size_t k = 0; k < movable.size(); ++k) {
    f.net({
        {movable[k], "Q"},
        {  fixed[k], "D"}
    });
  }
  Legroute_options opt;
  opt.threads         = 1;
  opt.clock_spine     = false;
  opt.reserved_tracks = 1;
  opt.cap_scale       = .25;
  opt.spill_rounds    = 6;
  opt.spill_fraction  = .2;
  opt.spill_shift     = true;
  const auto       d  = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  Legroute_regions regions;
  regions.rects = {
      {    0, 0, 10000, 2000},
      {10000, 0, 20000, 2000}
  };
  auto            g     = lr::build_grid(d, regions);
  auto            boxes = lr::build_boxes(d, g);
  lr::Routing     R;
  Legroute_report rep;
  lr::sat_place(d, g, boxes, regions, R, rep);
  EXPECT_EQ(rep.sat_regions, 2u);
  EXPECT_GE(rep.spill_rounds, 1u);
  EXPECT_GT(rep.shifted_tiles, 0u);
  EXPECT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
  EXPECT_GT(g.region_tiles[0].size(), 48u) << "the failing right region handed its strip to the left one";
  EXPECT_EQ(g.region_tiles[0].size() + g.region_tiles[1].size(), 100u);
  std::set<int> used;
  for (const auto& b : boxes.boxes) {
    EXPECT_TRUE(used.insert(b.tile).second) << "two boxes on tile " << b.tile;
    EXPECT_EQ(g.region[size_t(b.tile)], b.region);
  }
}

// Two failing regions want the same neighbour's tracks. Only one pair can use that neighbour's old tile/box
// lists; the other repair must wait for fresh crossings and verdicts, leaving its region untouched this round.
TEST(Legroute, MeshRepairDefersOverlappingPairs) {
  Fixture f(400, 24);
  f.add_horizontal_transit_layer();
  for (int region = 0; region < 3; ++region) {
    for (int col = 0; col < 6; ++col) {
      for (int row = 0; row < 4; ++row) {
        const auto a                    = f.cell("dff", 800 * col + 200, 2000 * region + 500 * row);
        const auto b                    = f.cell("dff", 10400 + 1600 * col + 200, 2000 * region + 500 * row + 250);
        f.fp.components[b].place.status = ph::Status::FIXED;
        f.net({
            {a, "Q"},
            {b, "D"}
        });
      }
    }
  }
  Legroute_options opt;
  opt.threads         = 1;
  opt.clock_spine     = false;
  opt.reserved_tracks = 1;
  opt.cap_scale       = .2;
  opt.spill_rounds    = 1;
  opt.spill_fraction  = 0;
  opt.mesh_join       = true;
  const auto       d  = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  Legroute_regions regions;
  regions.rects = {
      {0,    0, 20000, 2000},
      {0, 2000, 20000, 4000},
      {0, 4000, 20000, 6000}
  };
  auto            g         = lr::build_grid(d, regions);
  const auto      untouched = g.region_tiles[2];
  auto            boxes     = lr::build_boxes(d, g);
  lr::Routing     R;
  Legroute_report rep;
  lr::sat_place(d, g, boxes, regions, R, rep);
  EXPECT_EQ(rep.mesh_joins, 1u);
  EXPECT_EQ(rep.spill_rounds, 1u);
  EXPECT_EQ(g.region_tiles[2], untouched);
  for (const auto& b : boxes.boxes) {
    EXPECT_EQ(b.region, g.region[size_t(g.tile_of(b.cx, b.cy))]);
    EXPECT_EQ(b.region, g.region[size_t(b.tile)]);
  }
}

// The failing region first tries alternative placements of its boxes (noise, anchors, the orthogonal drawing), solved
// in parallel; whichever routes is adopted, else the spill goes on.
TEST(Legroute, AlternativePlacementsRunBeforeTheSpill) {
  Fixture f(400, 8);
  // The 47 DBU pitch keeps 20 DBU width/spacing legal and restores the outer-band track lost to die-edge clipping.
  f.add_horizontal_transit_layer(47);
  std::vector<size_t> movable, fixed;
  for (int col = 0; col < 6; ++col) {
    for (int row = 0; row < 4; ++row) {
      movable.push_back(f.cell("dff", 800 * col + 200, 500 * row));
      fixed.push_back(f.cell("dff", 10400 + 1600 * col + 200, 500 * row + 250));
      f.fp.components[fixed.back()].place.status = ph::Status::FIXED;
    }
  }
  for (size_t k = 0; k < movable.size(); ++k) {
    f.net({
        {movable[k], "Q"},
        {  fixed[k], "D"}
    });
  }
  Legroute_options opt;
  opt.threads         = 1;
  opt.clock_spine     = false;
  opt.reserved_tracks = 1;
  opt.cap_scale       = .2;
  opt.spill_rounds    = 6;
  opt.spill_fraction  = .2;
  opt.alternatives    = "noise,anchor,spread,tsm";
  const auto       d  = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  Legroute_regions regions;
  regions.rects = {
      {    0, 0, 10000, 2000},
      {10000, 0, 20000, 2000}
  };
  auto            g     = lr::build_grid(d, regions);
  auto            boxes = lr::build_boxes(d, g);
  lr::Routing     R;
  Legroute_report rep;
  lr::sat_place(d, g, boxes, regions, R, rep);
  EXPECT_EQ(rep.sat_regions, 2u);
  EXPECT_GE(rep.alt_tried, 4u) << "four kinds tried for the failing region";
  EXPECT_EQ(rep.sat_unsat_regions + rep.sat_unknown_regions, 0u);
  std::set<int> used;
  for (const auto& b : boxes.boxes) {
    EXPECT_TRUE(used.insert(b.tile).second) << "two boxes on tile " << b.tile;
  }
}

// The checker alternative: a relaxed model (no capacity on a checkerboard of tiles, wide windows) finds far tiles the
// windows of two never reach, and the full model from its answer routes the region the cut bound rejected.
TEST(Legroute, CheckerAlternativeReachesFarTiles) {
  Fixture f(400, 8);
  f.add_horizontal_transit_layer();
  std::vector<size_t> movable, fixed;
  for (int col = 0; col < 6; ++col) {
    for (int row = 0; row < 4; ++row) {
      movable.push_back(f.cell("dff", 800 * col + 200, 500 * row));
      fixed.push_back(f.cell("dff", 10400 + 1600 * col + 200, 500 * row + 250));
      f.fp.components[fixed.back()].place.status = ph::Status::FIXED;
    }
  }
  for (size_t k = 0; k < movable.size(); ++k) {
    f.net({
        {movable[k], "Q"},
        {  fixed[k], "D"}
    });
  }
  Legroute_options opt;
  opt.threads         = 1;
  opt.clock_spine     = false;
  opt.reserved_tracks = 1;
  opt.cap_scale       = .25;
  opt.spill_rounds    = 1;
  opt.alternatives    = "checker";
  const auto       d  = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
  Legroute_regions regions;
  regions.rects = {
      {    0, 0, 10000, 2000},
      {10000, 0, 20000, 2000}
  };
  auto            g     = lr::build_grid(d, regions);
  auto            boxes = lr::build_boxes(d, g);
  lr::Routing     R;
  Legroute_report rep;
  lr::sat_place(d, g, boxes, regions, R, rep);
  EXPECT_GE(rep.alt_tried, 1u);
  EXPECT_GE(rep.alt_rescued, 1u) << "the left region routes from the relaxed answer's tiles";
  int near = 0;  // the relaxed answer moved most boxes next to the boundary, past the windows of two; the rest stay
  for (const auto& b : boxes.boxes) {
    near += g.region[size_t(b.tile)] == 0 && g.col(b.tile) >= 6;
  }
  EXPECT_GE(near, 3) << "boxes next to the boundary";
}
TEST(Legroute, PlanBeforeBoxingAcceptsOverlappingCellsAndIgnoresRegionPins) {
  Fixture    f(64, 4);
  const auto a = f.cell("inv", 100, 0), b = f.cell("inv", 100, 0), c = f.cell("inv", 2500, 250);
  f.net({
      {a, "Y"},
      {c, "A"}
  });
  f.net({
      {b, "Y"},
      {c, "Y"}
  });
  Legroute_options opt;
  opt.clock_spine         = false;
  const auto       before = f.fp.components;
  const auto       empty  = legroute_plan(f.tech, f.fp, f.timing, opt);
  Legroute_regions stale;
  stale.rects = {
      {0, 0, 50, 50}
  };
  stale.pins = {
      {999, 0, 0}
  };  // a previous region interface is irrelevant to planning
  const auto other = legroute_plan(f.tech, f.fp, f.timing, opt, stale);
  EXPECT_EQ(empty.signals, 2u);
  EXPECT_EQ(empty.timed_signals, 2u);
  EXPECT_EQ(empty.unrouted, 0u);
  EXPECT_EQ(empty.overflow_tracks, 0u);
  EXPECT_EQ(empty.hot_tiles, 0u);
  EXPECT_TRUE(std::ranges::all_of(empty.component_hot_distance,
                                  [](unsigned distance) { return distance == std::numeric_limits<unsigned>::max(); }));
  EXPECT_EQ(empty.component_pressure, other.component_pressure);
  EXPECT_EQ(empty.search_pops, other.search_pops);
  for (size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(f.fp.components[i].place.x, before[i].place.x);
    EXPECT_EQ(f.fp.components[i].place.y, before[i].place.y);
    EXPECT_DOUBLE_EQ(empty.component_pressure[i], 1);
  }
}

TEST(Legroute, BoundaryOwnersAlwaysPrecedeLowerPriorityNeighbors) {
  lr::Grid g;
  g.cols             = 4;
  g.rows             = 1;
  g.regions          = 4;
  g.region           = {0, 1, 2, 3};
  // Greedy coloring produced 0,1,0,1: region 2 wrongly chose pins for harder
  // region 1. Difficulty must order every adjacency, not only the first pair.
  const auto ordered = lr::region_solve_waves(g, {0, 1, 2, 3});
  for (size_t i = 1; i < ordered.size(); ++i) {
    EXPECT_GT(ordered[i], ordered[i - 1]);
  }
  const auto independent = lr::region_solve_waves(g, {0, 2, 1, 3});
  EXPECT_EQ(independent[0], independent[2]);
  EXPECT_GT(independent[1], independent[0]);
  EXPECT_GT(independent[1], independent[2]);
  EXPECT_GT(independent[3], independent[2]);
  EXPECT_THROW(lr::region_solve_waves(g, {0, 1, 2, 2}), std::invalid_argument);
}

TEST(Legroute, BoxedPlanningMatchesTheActualAllSignalPrepass) {
  Fixture f(100, 8);
  f.cell("dff", 4500, 1000);  // establish the ordinary cellbox grid
  for (int i = 0; i < 12; ++i) {
    const auto a = f.cell("inv", i * 100, 0), b = f.cell("inv", 3000 + i * 100, 0);
    f.net({
        {a, "Y"},
        {b, "A"}
    });
  }
  Legroute_options options;
  options.legal_boxes = options.global_all = options.global_route = true;
  options.region_pins = options.clock_spine = options.final = false;
  options.global_share                                      = .1;
  options.global_rounds                                     = 2;
  options.spill_rounds                                      = 0;
  options.threads                                           = 1;
  const auto before                                         = f.fp.components;
  const auto estimate = legroute_plan(f.tech, f.fp, f.timing, options, {}, nullptr, 100000, true);
  ASSERT_EQ(estimate.unrouted, 0u);
  EXPECT_GT(estimate.overflow_tracks, 0u);
  EXPECT_EQ(f.fp.components, before);
  try {
    (void)legroute(f.tech, f.fp, f.timing, options);
    FAIL() << "congested boxed handoff entered SAT";
  } catch (const Legroute_replan& blocked) {
    EXPECT_EQ(estimate.unrouted, blocked.gates.unrouted);
    EXPECT_EQ(estimate.overflow_edges, blocked.gates.overflow_edges);
    EXPECT_EQ(estimate.overflow_tracks, blocked.gates.overflow_tracks);
    EXPECT_DOUBLE_EQ(estimate.gates.max_routing_density, blocked.gates.max_routing_density);
  }
  EXPECT_EQ(f.fp.components, before);
}

TEST(Legroute, FixedMacroPinsUseTheirTransformedAccessAndRotatedFootprint) {
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
    options.legal_boxes = true;
    options.clock_spine = false;
    const auto d        = lr::build_design(f.tech, f.fp, f.timing, options, nullptr);
    const auto g        = lr::build_grid(d, {});
    const auto boxes    = lr::build_boxes(d, g);
    const auto requests = lr::all_signal_requests(d, g, boxes);
    ASSERT_EQ(requests.size(), 2u);
    const auto left  = lr::physical_pin_access(d, g, d.nets[0].pins[1]);
    const auto right = lr::physical_pin_access(d, g, d.nets[1].pins[1]);
    EXPECT_NE(left.tile, right.tile);  // distinct macro pins must not collapse to its center
    EXPECT_EQ(requests[0].endpoints.back(), left.tile);
    EXPECT_EQ(requests[1].endpoints.back(), right.tile);
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
        const auto physical = legroute_plan(f.tech, f.fp, f.timing, options);
        const auto boxed    = legroute_plan(f.tech, f.fp, f.timing, options, {}, nullptr, 100000, true);
        EXPECT_EQ(physical.unrouted, 0u);
        EXPECT_EQ(boxed.unrouted, 0u);
        EXPECT_EQ(physical.overflow_tracks, 0u);
        EXPECT_EQ(boxed.overflow_tracks, 0u);
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

TEST(Legroute, MacroBoundaryAccessCannotCrossAnExternalObstruction) {
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
  Legroute_regions regions;
  regions.obstacles.push_back({
      "M4",
      {3800, 1600, 3900, 1650}
  });
  const auto    g = lr::build_grid(d, regions);
  const lr::Pin pin{int(hard), -1, &macro.pins.at("L"), "L"};
  const auto    access = lr::physical_pin_access(d, g, pin);
  EXPECT_DOUBLE_EQ(access.x, 4050);
  EXPECT_DOUBLE_EQ(access.y, 1625);  // retain the blocked pin, do not jump the obstruction
  EXPECT_EQ(access.tile, g.tile_of(4050, 1625));
}

TEST(Legroute, GlobalSearchBudgetReturnsToPlanningWithoutEnteringSat) {
  Fixture f(100, 8);
  f.cell("dff", 4500, 1000);
  const auto a = f.cell("inv", 0, 0), b = f.cell("inv", 3000, 0);
  f.net({
      {a, "Y"},
      {b, "A"}
  });
  Legroute_options options;
  options.legal_boxes = options.global_all = options.global_route = true;
  options.region_pins = options.clock_spine = options.final = false;
  options.global_net_pops                                   = 1;
  options.global_rounds                                     = 0;
  options.threads                                           = 1;
  const auto before                                         = f.fp.components;
  for (bool keep : {false, true}) {
    options.global_keep     = keep;
    options.partition_cells = keep ? 0 : 2;
    try {
      (void)legroute(f.tech, f.fp, f.timing, options);
      FAIL() << "retained global trees or partitioning accepted a partial search";
    } catch (const Legroute_replan& error) {
      EXPECT_EQ(error.gates.unrouted, 1u);
      EXPECT_EQ(error.gates.search_limited, 1u);
      EXPECT_EQ(error.gates.routing_dense_edges, 0u);
      EXPECT_EQ(error.gates.overflow_tracks, 0u);
      EXPECT_EQ(error.gates.cell_dense_regions, 0u);
      EXPECT_FALSE(error.gates.affected_components.empty());
    }
    EXPECT_EQ(f.fp.components, before);
  }
}

TEST(Legroute, PlanCongestionPressuresMovableCellsWithoutInflatingFixedCells) {
  Fixture    f(200, 16);
  const auto fixed                    = f.cell("inv", 1400, 750);
  f.fp.components[fixed].place.status = ph::Status::FIXED;
  const auto resident                 = f.cell("inv", 100, 0);  // no signal endpoint: transit pressure still identifies this cell
  const auto distant                  = f.cell("inv", 9000, 3500);
  for (int i = 0; i < 12; ++i) {
    const auto a = f.cell("inv", 100, 0), b = f.cell("inv", 2500, 0);
    f.net({
        {a, "Y"},
        {b, "A"}
    });
  }
  Legroute_options opt;
  opt.clock_spine   = false;
  opt.global_share  = .1;
  opt.global_rounds = 2;
  const auto plan   = legroute_plan(f.tech, f.fp, f.timing, opt);
  EXPECT_GT(plan.overflow_tracks + plan.unrouted, 0u);
  EXPECT_EQ(plan.component_pressure[fixed], 1);
  EXPECT_GT(plan.hot_tiles, 0u);
  EXPECT_EQ(plan.component_hot_distance[fixed], std::numeric_limits<unsigned>::max());
  EXPECT_EQ(plan.component_hot_distance[resident], 0u);
  EXPECT_GT(plan.component_hot_distance[distant], 2u);
  EXPECT_LT(plan.component_hot_distance[distant], std::numeric_limits<unsigned>::max());
  EXPECT_GT(plan.tile_width_dbu, 0);
  EXPECT_GT(plan.tile_height_dbu, 0);
  EXPECT_TRUE(std::ranges::any_of(plan.component_pressure, [](double value) { return value > 1; }));
  // Search exhaustion is recorded, but does not manufacture inflation pressure.
  const auto limited = legroute_plan(f.tech, f.fp, f.timing, opt, {}, nullptr, 1);
  EXPECT_EQ(limited.search_limited, 12u);
  EXPECT_EQ(limited.unrouted, 12u);
  EXPECT_GT(limited.hot_tiles, 0u);  // movement feedback, while bounded search still creates no inflation pressure
  EXPECT_EQ(limited.gates.search_limited, 12u);
  EXPECT_TRUE(std::ranges::all_of(limited.component_pressure, [](double value) { return value == 1; }));
}

TEST(Legroute, FullPhysicalRowReturnsToPlanningBeforeSat) {
  Fixture f(8, 1);
  f.cell("dff", 0, 0);  // one cell fills the only real row; the absent second tile row contributes no area
  Legroute_options options;
  options.legal_boxes   = true;
  options.clock_spine   = false;
  auto            d     = lr::build_design(f.tech, f.fp, f.timing, options, nullptr);
  auto            g     = lr::build_grid(d, {});
  auto            boxes = lr::build_boxes(d, g);
  lr::Routing     routing;
  Legroute_report report;
  try {
    lr::sat_place(d, g, boxes, {}, routing, report);
    FAIL() << "100% physical density entered SAT";
  } catch (const Legroute_replan& blocked) {
    EXPECT_EQ(blocked.gates.cell_dense_regions, 1u);
    EXPECT_DOUBLE_EQ(blocked.gates.max_cell_density, 1);
    EXPECT_DOUBLE_EQ(blocked.gates.regions[0].placeable_area_dbu2, 100000);
  }
  EXPECT_EQ(report.sat_regions, 0u);
  EXPECT_EQ(report.sat_solves, 0u);
}

TEST(Legroute, PlanningRejectsWholeCoreDensityWithoutPublishingAMeasuredMap) {
  const auto populate = [](Fixture& f) {
    const auto a = f.cell("dff", 0, 0), b = f.cell("inv", 400, 0);
    f.net({
        {a, "Q"},
        {b, "A"}
    });
    f.net(
        {
            {a, "CLK"}
    },
        1,
        "CLOCK");
    f.tech.macros["tap"]       = f.tech.macros.at("inv");
    f.tech.macros["tap"].klass = "CORE WELLTAP";
    f.tech.macros["tap"].pins.clear();
    const auto tap                    = f.cell("tap", 500, 0);
    f.fp.components[tap].place.status = ph::Status::FIXED;
  };
  Fixture dense(12, 1), roomy(12, 2);
  populate(dense);
  populate(roomy);
  Legroute_options options;
  options.clock_spine     = false;
  options.global_route    = true;
  options.region_pins     = false;
  options.partition_cells = 2;
  Legroute_congestion map;
  const auto          control = legroute_plan(roomy.tech, roomy.fp, roomy.timing, options, {}, nullptr, 100000, false, &map);
  ASSERT_TRUE(control.route_attempted);
  EXPECT_EQ(control.route_skip_reason, Legroute_plan_report::Route_skip_reason::none);
  EXPECT_EQ(control.unchecked_signals, 0u);
  ASSERT_FALSE(map.horizontal.empty());
  const auto before   = dense.fp.components;
  const auto rejected = legroute_plan(dense.tech, dense.fp, dense.timing, options, {}, nullptr, 100000, false, &map);
  EXPECT_FALSE(rejected.route_attempted);
  EXPECT_EQ(rejected.route_skip_reason, Legroute_plan_report::Route_skip_reason::physical_density);
  EXPECT_EQ(rejected.signals, 1u);  // the clock remains outside coarse signal demand
  EXPECT_EQ(rejected.unchecked_signals, rejected.signals);
  EXPECT_EQ(rejected.unrouted, 0u);  // no failed searches were attempted
  EXPECT_EQ(rejected.search_pops, 0u);
  EXPECT_EQ(rejected.pattern_attempts, 0u);
  EXPECT_TRUE(rejected.failures.empty());
  EXPECT_TRUE(rejected.gates.requires_replan());
  EXPECT_DOUBLE_EQ(rejected.gates.max_cell_density, 1);  // fixed tap footprint was removed from free row area
  EXPECT_TRUE(std::isnan(rejected.gates.max_routing_density));
  EXPECT_TRUE(map.horizontal.empty());  // the previous successful map was cleared
  EXPECT_EQ(map.cols, 0);
  EXPECT_THROW(resample_legroute_congestion(map, dense.fp.die, 2, 2), std::invalid_argument);
  EXPECT_EQ(dense.fp.components, before);
  // Explicit measurements used by external GP feedback must still route this
  // pose; they cannot turn an unattempted route into an all-zero label.
  const auto measured = legroute_plan(dense.tech, dense.fp, dense.timing, options, {}, nullptr, 100000, false, &map, true);
  EXPECT_TRUE(measured.route_attempted);
  EXPECT_EQ(measured.unchecked_signals, 0u);
  EXPECT_EQ(measured.route_skip_reason, Legroute_plan_report::Route_skip_reason::none);
  EXPECT_TRUE(measured.gates.requires_replan());
  EXPECT_FALSE(map.horizontal.empty());
  EXPECT_EQ(map.signals, 1u);
}

TEST(Legroute, PlanningRejectsALocallyOverfullWindowBeforeRoutingWithLocalPressure) {
  // A 10 x 4 um core at about 6% overall density, with one fully packed 1.6 x 1.5 um corner: the whole-core and
  // region screens pass, but the route would start from cells that cannot legally sit there.
  Fixture             f(200, 16);
  std::vector<size_t> packed;
  for (int y = 0; y < 6; ++y) {
    for (int x = 0; x < 4; ++x) {
      packed.push_back(f.cell("dff", x * 400, y * 250));
    }
  }
  const auto far = f.cell("inv", 8000, 3000);
  f.net({
      {packed.front(), "Q"},
      {           far, "A"}
  });
  Legroute_options options;
  options.clock_spine  = false;
  options.global_route = true;
  options.region_pins  = false;
  const auto before    = f.fp.components;  // physical pins, no boxes: window density decides
  const auto rejected  = legroute_plan(f.tech, f.fp, f.timing, options);
  EXPECT_FALSE(rejected.route_attempted);
  EXPECT_EQ(rejected.route_skip_reason, Legroute_plan_report::Route_skip_reason::window_density);
  EXPECT_STREQ(rejected.route_skip_name(), "window_density");
  EXPECT_GT(rejected.fit_windows, 1u);
  EXPECT_GE(rejected.fit_dense_windows, 1u);
  EXPECT_GT(rejected.fit_max_density, .9);
  EXPECT_TRUE(rejected.gates.requires_replan());
  EXPECT_EQ(rejected.unchecked_signals, rejected.signals);
  EXPECT_EQ(rejected.search_pops, 0u);
  EXPECT_GT(rejected.component_pressure[packed.front()], 1);  // the packed corner is pushed apart
  EXPECT_EQ(rejected.component_pressure[far], 1);             // the rest of the core is left alone
  EXPECT_EQ(rejected.component_hot_distance[packed.front()], 0u);
  EXPECT_GT(rejected.component_hot_distance[far], 0u);
  EXPECT_EQ(f.fp.components, before);
  // With cell-boxes the authoritative test is local's joint box matching: these boxes find distinct tiles
  // next to the corner, so the pose is routed, and the dense window stays reported.
  options.partition_cells = 2;
  const auto boxed        = legroute_plan(f.tech, f.fp, f.timing, options);
  EXPECT_TRUE(boxed.route_attempted);
  EXPECT_TRUE(boxed.fit_matched);
  EXPECT_EQ(boxed.fit_far_boxes, 0u);
  EXPECT_GE(boxed.fit_dense_windows, 1u);
  options.partition_cells = 0;
  // Disabled, the same pose proceeds to the route.
  options.fit_density     = 0;
  const auto routed       = legroute_plan(f.tech, f.fp, f.timing, options);
  EXPECT_TRUE(routed.route_attempted);
  EXPECT_EQ(routed.fit_windows, 0u);
  // Invalid settings are configuration errors.
  options.fit_density = 1.5;
  EXPECT_THROW(legroute_plan(f.tech, f.fp, f.timing, options), std::invalid_argument);
  options.fit_density = .9;
  options.fit_window  = 0;
  EXPECT_THROW(legroute_plan(f.tech, f.fp, f.timing, options), std::invalid_argument);
}

TEST(Legroute, PlanningChecksProposedCutsAndConfiguredSeedRegionLimits) {
  Fixture f(64, 2);
  for (int x : {0, 800, 1600, 2400}) {
    f.cell("dff", x, 0);
  }
  Legroute_options options;
  options.clock_spine       = false;
  options.region_cell_limit = 1;
  Legroute_regions seed;
  seed.rects = {
      {   0, 0, 1600, 500},
      {1600, 0, 3200, 500}
  };
  auto plan = legroute_plan(f.tech, f.fp, f.timing, options, seed);
  EXPECT_EQ(plan.gates.region_target, 1u);
  EXPECT_EQ(plan.gates.oversized_regions, 2u);
  options.legal_boxes     = true;
  options.global_route    = true;
  options.region_pins     = false;
  options.partition_cells = 1;
  plan                    = legroute_plan(f.tech, f.fp, f.timing, options, seed);
  EXPECT_FALSE(plan.gates.requires_replan());
  EXPECT_EQ(plan.gates.regions.size(), 4u);
  for (const auto& region : plan.gates.regions) {
    EXPECT_LE(region.cells, 1u);
  }
}

TEST(Legroute, PhysicalDensitySubtractsFixedObjectsButNotMovableOverhangTwice) {
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
  f.cell("inv", 750, 0);  // center on tile1, one site of movable overhang reserved in tile0
  f.cell("dff", 1200, 250);
  Legroute_options options;
  options.legal_boxes = true;
  options.clock_spine = false;
  const auto d        = lr::build_design(f.tech, f.fp, f.timing, options, nullptr);
  const auto g        = lr::build_grid(d, {});
  EXPECT_EQ(std::accumulate(g.placeable_sites.begin(), g.placeable_sites.end(), int64_t{}), 56);
  int64_t packing_sites = 0;
  for (size_t t = 0; t < g.size(); ++t) {
    for (const auto& segment : g.segs(int(t))) {
      packing_sites += segment.hi - segment.lo;
    }
  }
  EXPECT_EQ(packing_sites, 55);
  const auto plan = legroute_plan(f.tech, f.fp, f.timing, options);
  ASSERT_EQ(plan.gates.regions.size(), 1u);
  EXPECT_DOUBLE_EQ(plan.gates.regions[0].cell_area_dbu2, 125000);
  EXPECT_DOUBLE_EQ(plan.gates.regions[0].placeable_area_dbu2, 700000);
  EXPECT_DOUBLE_EQ(plan.gates.max_cell_density, 125000.0 / 700000);
  EXPECT_FALSE(plan.gates.requires_replan());
}

TEST(Legroute, PlanReportsTopologyDisconnectionSeparatelyFromBudgetExhaustion) {
  Fixture    f(100, 8);
  const auto a = f.cell("inv", 0, 0), b = f.cell("inv", 3000, 0), c = f.cell("inv", 4500, 1000);
  f.net({
      {a, "Y"},
      {b, "A"}
  });
  f.net({
      {b, "Y"},
      {c, "A"}
  });
  Legroute_options options;
  options.legal_boxes   = true;
  options.clock_spine   = false;
  options.global_share  = .001;  // Every shared edge rounds down to zero capacity.
  options.global_rounds = 4;
  for (bool boxed : {false, true}) {
    const auto plan = legroute_plan(f.tech, f.fp, f.timing, options, {}, nullptr, 1, boxed);
    EXPECT_EQ(plan.signals, 2u);
    EXPECT_EQ(plan.unrouted, 2u);
    EXPECT_EQ(plan.topology_disconnected, 2u);
    EXPECT_EQ(plan.search_limited, 0u);
    EXPECT_EQ(plan.search_pops, 0u);
    ASSERT_EQ(plan.failures.size(), 2u);
    for (const auto& failure : plan.failures) {
      EXPECT_TRUE(failure.topology_disconnected);
      EXPECT_FALSE(failure.search_limited);
      EXPECT_EQ(failure.last_attempt_pops, 0u);
      EXPECT_EQ(failure.search_pops, 0u);
      EXPECT_EQ(failure.search_attempts, 1u);
    }
    const auto path = std::filesystem::path(testing::TempDir()) / (boxed ? "boxed_topology.tsv" : "topology.tsv");
    write_legroute_plan_failures(plan, path.string());
    std::ifstream file(path);
    std::string   line;
    ASSERT_TRUE(bool(std::getline(file, line)));
    EXPECT_TRUE(line.ends_with("search_limited\ttopology_disconnected\tlast_attempt_pops\tsearch_pops\tsearch_attempts"));
    for (size_t k = 0; k < plan.failures.size(); ++k) {
      ASSERT_TRUE(bool(std::getline(file, line)));
      EXPECT_TRUE(line.ends_with("\t0\t1\t0\t0\t1"));
    }
    EXPECT_FALSE(bool(std::getline(file, line)));
    std::filesystem::remove(path);
  }
}

TEST(Legroute, PlanFailuresKeepOriginalNetIdsAndActualRoutingEndpoints) {
  Fixture    f(100, 8);
  const auto ff = f.cell("dff", 4500, 1000);
  const auto a = f.cell("inv", 0, 0), nearby = f.cell("inv", 100, 0), far = f.cell("inv", 3000, 0);
  f.net(
      {
          {  a, "Y"},
          {far, "A"}
  },
      1,
      "POWER");
  f.net(
      {
          { a,   "Y"},
          {ff, "CLK"}
  },
      1,
      "CLOCK");
  f.net({
      {     a, "Y"},
      {nearby, "A"}
  });  // one endpoint tile, no search needed
  f.net({
      {   far, "Y"},
      {     a, "A"},
      {nearby, "A"}
  });
  f.net({
      {  a, "Y"},
      {far, "A"}
  });
  f.fp.nets[3].name = "hard_a";
  f.fp.nets[4].name = "hard_b";
  Legroute_options options;
  options.legal_boxes   = true;
  options.clock_spine   = false;
  options.global_rounds = 0;
  for (bool boxed : {false, true}) {
    const auto plan = legroute_plan(f.tech, f.fp, f.timing, options, {}, nullptr, 1, boxed);
    EXPECT_EQ(plan.signals, 3u);
    EXPECT_EQ(plan.unrouted, 2u);
    ASSERT_EQ(plan.failures.size(), plan.unrouted);
    for (size_t k = 0; k < plan.failures.size(); ++k) {
      const auto& failure = plan.failures[k];
      EXPECT_EQ(failure.net_index, k + 3);
      EXPECT_EQ(failure.net_name, f.fp.nets[k + 3].name);
      EXPECT_EQ(failure.pin_count, k ? 2u : 3u);
      EXPECT_EQ(failure.endpoint_tiles, 2u);
      EXPECT_GT(failure.bbox_width_tiles, 0);
      EXPECT_EQ(failure.bbox_height_tiles, 0);
      EXPECT_TRUE(failure.search_limited);
      EXPECT_FALSE(failure.topology_disconnected);
      EXPECT_EQ(failure.last_attempt_pops, 1u);
      EXPECT_EQ(failure.search_pops, 1u);
      EXPECT_EQ(failure.search_attempts, 1u);
    }
    const auto path = std::filesystem::path(testing::TempDir()) / (boxed ? "boxed_plan_failures.tsv" : "plan_failures.tsv");
    write_legroute_plan_failures(plan, path.string());
    std::ifstream            file(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);) {
      lines.push_back(std::move(line));
    }
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_TRUE(lines[0].starts_with("net_index\tnet_name\tpin_count\tendpoint_tiles\t"));
    EXPECT_TRUE(lines[1].starts_with("3\thard_a\t3\t2\t"));
    EXPECT_TRUE(lines[2].starts_with("4\thard_b\t2\t2\t"));
    std::filesystem::remove(path);
  }
}
}  // namespace livehd::placement
