// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// Shared fixture of the legroute unit tests: a small synthetic technology and floorplan.
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "inou/lefdef/physical.hpp"
#include "legroute_internal.hpp"

namespace livehd::placement {
namespace ph = livehd::physical;
// Fake technology: 50 x 250 DBU sites (dbu 1000), M1 pins, M2 escape layer, M3/M5 vertical and M4/M6 horizontal
// routing.
struct Legroute_test_frame {
  ph::Technology   tech;
  ph::Floorplan    fp;
  timing::Feedback timing;
  explicit Legroute_test_frame(int sites = 200, int rows = 16) {
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
    const auto master = [&](const std::string& name, double w, std::vector<std::pair<std::string, double>> pins, bool flop) {
      auto& m      = tech.macros[name];
      m.width      = w;
      m.height     = .25;
      m.klass      = "CORE";
      m.site       = "site";
      // These masters' pin rectangles are invariant under the X reflection
      // required by the explicitly alternating N/FS placement rows.
      m.symmetry_x = true;
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
};
}  // namespace livehd::placement
