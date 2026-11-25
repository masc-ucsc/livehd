// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace livehd::routing {

struct Settings {
  std::string report;
  bool        agent   = false;
  std::string order   = "short";
  double      m2_cost = 1.6, m3_cost = 1.1, via_cost = 40, hi_cost = 1.0, hmin = 1.0, hweight = 1.0, pd_alpha = 0.0;
  int         pd_min_pins = 4;
  int         margin      = 720;
  int64_t     max_expand  = 4000000;
  int         limit       = -1;
  int ripup = 0, rip_width = 4, explain = 0, shift = 0, shift_reach = 4, final_passes = 0, flop_reach = 0, shift_cap = 8, chain = 0;
  bool        verbose = false, aim_link = true;
  std::string trace, timing, clock_nets, verify_report, only, reserve_cells;
  bool        prelegal = false, tile_move = false, reserve = false, verify = false;
  double      anchor_frac   = 0;
  double      time_limit    = 0;  // CPU seconds for the routing loop; 0 is no limit
  double      long_frac     = 0;  // share of the data nets, by box, routed ahead of the others
  double      space_frac    = 0;  // worst-slack share of the nets whose long wires keep the next tracks empty
  int         space_min_len = 5000, space_min_layer = 1;
  double      crit_frac = 0;
  double      pin_cost  = 0;   // extra M2/M3 wire cost over pin-dense tiles: factor 1 + pin_cost * (pins / pin_tile - 1)
  int         pin_tile  = 27;  // pins in a 32-track tile above which the extra cost starts (20 pins per um2 on ASAP7)
};

struct Frame {
  std::string              input_def;
  std::string              output_def;
  std::vector<std::string> lefs;
};

struct Outcome {
  std::string                    stats;
  std::map<std::string, int64_t> findings;
};

Settings parse_settings(const std::vector<std::string>& args);
Outcome  route_frame(const Frame& frame, const Settings& settings);

// Read back existing wires and physical rules without starting a routing search.
Outcome verify_frame(const Frame& frame, const Settings& settings);

// Private process entry used by lhd's independently scheduled native workers.
int worker(int argc, char** argv);

}  // namespace livehd::routing
