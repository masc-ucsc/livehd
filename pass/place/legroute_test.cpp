// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "legroute.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <set>

#include "gtest/gtest.h"
#include "inou/lefdef/routing_connectivity.hpp"
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

TEST(Legroute, PackSegmentsWidestFirstIntoRoomiestSegment) {
  EXPECT_EQ(lr::pack_segments({3, 2, 1}, {4, 3}), (std::vector<int>{0, 1, 0}));
  EXPECT_TRUE(lr::pack_segments({3, 2, 2}, {4, 3}).empty());
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
      {ph::Route_point{0, 2025, {}}, ph::Route_point{10000, 2025, {}}},
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
  const auto g        = lr::build_grid(d, regions);
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
  // the stripe on the M4 track at y = 2025 takes it in tile row 4 (10 tracks, one reserved, one stripe)
  EXPECT_EQ(g.cap[m4][g.tile(2, 4)], 10 - 1 - 1);
  EXPECT_EQ(g.cap[m4][g.tile(2, 3)], 10 - 1);
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
  const auto       g     = lr::build_grid(d, {});
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
      if (!n.wires.empty() && n.use != "CLOCK") {
        const auto c = ph::check_route_connectivity(f.tech, f.fp, n);
        EXPECT_TRUE(c.connected) << n.name << ": " << c.reason << " (" << c.components << " components)";
      }
    }
    EXPECT_EQ(wired, r.wires_emitted ? r.routed_nets : 0u);
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
    f.random_design(12, 4, 3);
    Legroute_options opt;
    opt.threads           = 1;
    opt.reserved_tracks   = reserved;
    opt.clock_spine       = false;
    const auto      d     = lr::build_design(f.tech, f.fp, f.timing, opt, nullptr);
    const auto      g     = lr::build_grid(d, {});
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
      int cap = h ? g.box_cap[t] : 0;  // the box layer's tracks count as H
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
}  // namespace livehd::placement
