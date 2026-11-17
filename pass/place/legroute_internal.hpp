// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// legroute internals shared by its files (legroute_*.cpp); not a public interface.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "legroute.hpp"

namespace livehd::placement::lr {
using Clock = std::chrono::steady_clock;
inline double seconds_since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// ---------------------------------------------------------------- netlist view
struct Pin {
  int                  comp = -1, term = -1;  // floorplan component or terminal
  const physical::Pin* lef = nullptr;
  std::string_view     name;
  bool                 output = false, clock = false;
};
struct Net {
  size_t           frame = 0;  // floorplan net index
  std::vector<Pin> pins;       // driver first
  int              driver = -1;
  bool             clock = false, power = false, reset = false;
  bool             routed = false;  // signal net between two or more places: the router's job
  double           slack  = std::numeric_limits<double>::infinity();
  double           crit   = 0;  // window criticality: 1 at WNS, 0 at WNS + crit_window
};
struct Design {
  const physical::Technology&         tech;
  physical::Floorplan&                fp;
  const Legroute_options&             opt;
  const physical::Tech_profile*       profile = nullptr;
  std::vector<const physical::Macro*> macro;    // per component
  std::vector<int>                    movable;  // component ids of movable cells (one row, or two rows: a tile's height)
  std::vector<int>                    cell;     // component -> movable index, -1 otherwise
  std::vector<Net>                    nets;
  int64_t                             flop_w = 0, widest = 0;  // widest single-row flop and widest movable cell (DBU)
};
Design build_design(const physical::Technology&, physical::Floorplan&, const timing::Feedback&, const Legroute_options&,
                    const physical::Tech_profile*);

// ---------------------------------------------------------------- the tile grid
struct Layer {
  std::string name;
  bool        horizontal = true;
  int64_t     pitch = 0, offset = 0, width = 0;
};
// A tile is two placement rows tall and twice the widest flop wide (step D makes the size a free parameter, R9).
struct Grid {
  unsigned                                 rows_per_tile = 2;
  int64_t                                  x0 = 0, y0 = 0, site = 0, row_h = 0, tile_w = 0, tile_h = 0, dbu = 1000;
  physical::Rect                           die{-1e18, -1e18, 1e18, 1e18};  // routed metal stays inside
  int                                      sites = 0, cols = 0, rows = 0;
  // Free physical placement sites per tile after fixed obstacles and hard blockages (the site capacity).
  std::vector<int64_t>                     placeable_sites;
  // The routed stack: the routing layers above the escape layer, bottom-up; capacity per layer and tile (usable
  // tracks across the tile after power, macros, blockages and Legroute_options::reserved_tracks).
  std::string                              escape_layer;
  std::vector<Layer>                       layers;
  std::vector<std::vector<int16_t>>        cap;
  std::vector<std::vector<physical::Rect>> obstacles;  // per layer: power metal and macro bodies (DBU)
  int                                      tile(int c, int r) const { return r * cols + c; }
  int                                      col(int t) const { return t % cols; }
  int                                      row(int t) const { return t / cols; }
  size_t                                   size() const { return size_t(cols) * rows; }
  int64_t                                  left(int c) const { return x0 + c * tile_w; }
  int64_t                                  bottom(int r) const { return y0 + r * tile_h; }
  int64_t                                  cx(int c) const { return left(c) + tile_w / 2; }
  int64_t                                  cy(int r) const { return bottom(r) + tile_h / 2; }
  int col_of(double x) const { return std::clamp(int(std::floor((x - x0) / tile_w)), 0, cols - 1); }
  int row_of(double y) const { return std::clamp(int(std::floor((y - y0) / tile_h)), 0, rows - 1); }
  int tile_of(double x, double y) const { return tile(col_of(x), row_of(y)); }
  // first and last track index of a layer inside a tile band (H: the tile row, V: the tile column)
  std::pair<int64_t, int64_t> tracks(int layer, int band) const;
  int64_t track_coord(int layer, int64_t index) const { return layers[layer].offset + index * layers[layer].pitch; }
};
Grid                                          build_grid(const Design&, const Legroute_power&);
// Horizontal/vertical tile capacity: the per-layer capacities summed by direction.
std::pair<std::vector<int>, std::vector<int>> transit_capacity(const Grid&);
// A physical pin's coarse attachment. Boundary access may extend along a clear preferred-layer track from a macro
// pin that reaches its body's edge; it never opens macro capacity. Detailed pin/via access is a detailed router's job.
struct Pin_access {
  double x = 0, y = 0;
  int    tile = -1;
};
Pin_access physical_pin_access(const Design&, const Grid&, const Pin&);

// ---------------------------------------------------------------- the clock charge
// One clock's flop tiles: the trunk column, the rib rows every `pitch` tile rows centred on the flop rows, each
// rib's column span, and the rib each flop taps.
struct Clock_domain {
  int                                trunk_c = 0;
  std::vector<int>                   ribs;
  std::map<int, std::pair<int, int>> span;     // rib row -> column range
  std::vector<int>                   tap_rib;  // per input tile: the rib row it taps
};
Clock_domain plan_clock_domain(const Grid&, const std::vector<int>& tiles, int pitch);
// The tracks a spine clock tree would take per tile and direction (trunk, ribs, one tap per clock sink cell, and
// the joins of the remaining pins), from each clock sink cell's own tile. An estimate: the real tree is built
// after placement (CTS).
struct Clock_charge {
  std::vector<int> h, v;
};
Clock_charge clock_tree_charge(const Design&, const Grid&, unsigned rib_tiles);

// ---------------------------------------------------------------- the global router
// A net routed over the tile grid: a tree of tile edges joining its endpoint tiles by shortest paths on the edges
// with room. An edge carries at most a share of the narrower tile's tracks in its direction, the fuller an edge the
// costlier, most critical nets first.
struct Global_net {
  int              net  = -1;
  double           crit = 0;   // order: highest first, then the short nets
  std::vector<int> endpoints;  // distinct tiles, the driver's first
};
struct Global_tree {
  bool                             routed = false;
  std::vector<std::pair<int, int>> edges;  // adjacent tile pairs, lower tile first
  bool                             search_limited        = false;
  bool                             topology_disconnected = false;  // exact only in the current coarse capacity graph
  size_t                           last_attempt_pops = 0, search_pops = 0, search_attempts = 0;
};
struct Global_routes {
  std::vector<Global_tree> trees;                                    // per Global_net
  size_t                   unrouted = 0;                             // nets without a path
  size_t                   overflow_edges = 0, overflow_tracks = 0;  // edges over their share after the rounds, by how much
  size_t                   overflow_edges_h = 0, overflow_edges_v = 0, overflow_tracks_h = 0, overflow_tracks_v = 0;
  unsigned                 rounds      = 0;  // negotiation rounds run
  bool                     stalled     = false;  // negotiation stopped: a round cut the overflow tracks by under 2%
  size_t                   search_pops = 0, search_pushes = 0, search_restarts = 0;
  size_t                   search_limited        = 0;
  size_t                   topology_disconnected = 0;  // final nets rejected by the static capacity precheck
  size_t                   pattern_attempts = 0, pattern_completed = 0, pattern_fallbacks = 0;
  size_t                   pattern_work = 0, pattern_work_limited = 0;
  double                   seconds = 0;
};
// cap_h[t], cap_v[t]: the tile's tracks per direction; margin_tiles: the search box around a net's endpoints; rounds:
// negotiation rounds (rip-up and reroute of the nets on edges over their share; 0: a full edge is closed, one pass)
Global_routes       global_route(const Grid&, const std::vector<int>& cap_h, const std::vector<int>& cap_v, double share,
                                 const std::vector<Global_net>&, unsigned margin_tiles, unsigned rounds, size_t net_pops = 0,
                                 double astar_weight = 1, bool pattern_seed = false);
Legroute_congestion route_congestion(const Design&, const Grid&, const Global_routes&, const std::vector<Global_net>&,
                                     const std::vector<int>& cap_h, const std::vector<int>& cap_v);
}  // namespace livehd::placement::lr
