// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// legroute internals shared by its steps (legroute_*.cpp); not a public interface.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "legroute.hpp"

namespace livehd::placement::lr {
using Clock = std::chrono::steady_clock;
inline double seconds_since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// Join workers and propagate failures to the transactional driver.
template <class Fn>
void parallel(size_t n, unsigned threads, Fn&& fn) {
  const size_t                    workers = std::min(n, size_t(std::max(1u, threads)));
  std::atomic<size_t>             next    = 0;
  std::vector<std::exception_ptr> errors(workers);
  {
    std::vector<std::jthread> pool;
    for (size_t w = 0; w < workers; ++w) {
      pool.emplace_back([&, w] {
        try {
          for (size_t k; (k = next++) < n;) {
            fn(k);
          }
        } catch (...) {
          errors[w] = std::current_exception();
        }
      });
    }
  }
  for (const auto& error : errors) {
    if (error) {
      std::rethrow_exception(error);
    }
  }
}

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
  bool             routed   = false;  // signal net between two or more places: the router's job
  double           slack    = std::numeric_limits<double>::infinity();
  double           crit     = 0;      // window criticality: 1 at WNS, 0 at WNS + crit_window
  double           rank     = 1;      // slack rank fraction among timed nets (0 = worst)
  double           ramp     = 0;      // max(0, 1 - rank / crit_fraction)
  bool             critical = false;  // in the top crit_fraction
  unsigned         weight   = 1;      // 1 + round(8 crit^2)
};
struct Design {
  const physical::Technology&         tech;
  physical::Floorplan&                fp;
  const Legroute_options&             opt;
  const physical::Tech_profile*       profile = nullptr;
  std::vector<const physical::Macro*> macro;    // per component
  std::vector<int>                    movable;  // component ids of movable single-row cells
  std::vector<int>                    cell;     // component -> movable index, -1 otherwise
  std::vector<int64_t>                width;    // per movable (DBU)
  std::vector<bool>                   flop;     // per movable: has a clock pin
  std::vector<Net>                    nets;
  std::vector<std::vector<int>>       nets_of;  // per movable: its net ids (each once)
  double                              wns    = 0;
  int64_t                             flop_w = 0, widest = 0;
};
Design build_design(const physical::Technology&, physical::Floorplan&, const timing::Feedback&, const Legroute_options&,
                    const physical::Tech_profile*);

// ---------------------------------------------------------------- L1 tile grid
struct Seg {
  uint8_t     row = 0;         // 0 bottom, 1 top
  int32_t     lo = 0, hi = 0;  // free sites [lo, hi) from the tile's left edge
  friend auto operator<=>(const Seg&, const Seg&) = default;
};
struct Layer {
  std::string name, cut_below;  // cut_below: the via layer under it
  bool        horizontal = true;
  int64_t     pitch = 0, offset = 0, width = 0;
  double      r_um = 0, c_um = 0;  // kohm/um, fF/um (Elmore estimate)
};
struct Grid {
  int64_t                                    x0 = 0, y0 = 0, site = 0, row_h = 0, tile_w = 0, tile_h = 0, dbu = 1000;
  physical::Rect                             die{-1e18, -1e18, 1e18, 1e18};  // routed metal stays inside
  int                                        sites = 0, cols = 0, rows = 0;
  std::vector<uint32_t>                      key;   // per tile: free-segment pattern
  std::vector<std::vector<Seg>>              keys;  // pattern -> segments (sorted)
  std::vector<uint8_t>                       blocked;
  std::vector<physical::Orientation>         row_orient;   // per placement row (2 per tile row)
  std::vector<uint8_t>                       row_present;  // per placement row
  // regions: tile sets (rectangles of the region mesh today; nothing below reads rectangle bounds)
  std::vector<int>                           region;
  int                                        regions = 1;
  std::vector<std::vector<int>>              region_tiles;
  // routing layers above the box layer, bottom-up; capacity per layer and tile (usable tracks across the tile)
  std::string                                pin_layer, box_layer, box_cut;
  std::vector<Layer>                         layers;
  std::vector<std::vector<int16_t>>          cap;
  std::vector<int16_t>                       box_cap;    // per tile: usable box-layer tracks (in-box routes and local nets only)
  std::vector<std::vector<physical::Rect>>   obstacles;  // per layer: power metal and macro bodies (DBU)
  std::vector<std::vector<std::vector<int>>> obstacle_band;  // [layer][band]: obstacles near that band's tracks
  int                                        tile(int c, int r) const { return r * cols + c; }
  int                                        col(int t) const { return t % cols; }
  int                                        row(int t) const { return t / cols; }
  size_t                                     size() const { return size_t(cols) * rows; }
  int64_t                                    left(int c) const { return x0 + c * tile_w; }
  int64_t                                    bottom(int r) const { return y0 + r * tile_h; }
  int64_t                                    cx(int c) const { return left(c) + tile_w / 2; }
  int64_t                                    cy(int r) const { return bottom(r) + tile_h / 2; }
  int                         col_of(double x) const { return std::clamp(int(std::floor((x - x0) / tile_w)), 0, cols - 1); }
  int                         row_of(double y) const { return std::clamp(int(std::floor((y - y0) / tile_h)), 0, rows - 1); }
  int                         tile_of(double x, double y) const { return tile(col_of(x), row_of(y)); }
  const std::vector<Seg>&     segs(int t) const { return keys[key[t]]; }
  // first and last track index of a layer inside a tile band (H: the tile row, V: the tile column)
  std::pair<int64_t, int64_t> tracks(int layer, int band) const;
  int64_t track_coord(int layer, int64_t index) const { return layers[layer].offset + index * layers[layer].pitch; }
};
Grid             build_grid(const Design&, const Legroute_regions&);
// Cells of widths (sites) packed into free segments: widest first into the roomiest segment that fits (a cell's
// preferred segment first when given). The segment of each cell, or empty when they do not fit.
std::vector<int> pack_segments(const std::vector<int>& widths, const std::vector<int>& room,
                               const std::vector<int>* prefer = nullptr);

// ---------------------------------------------------------------- L2 cell-boxes
struct Box {
  std::vector<int> cells;  // movable ids
  int              region = 0, tile = -1;
  double           cx = 0, cy = 0;  // width-weighted cell centroid (DBU)
  double           crit     = 0;    // max window criticality of its nets
  double           ramp     = 0;    // max rank ramp of its nets
  bool             critical = false, clock = false, fixed = false;
  int              external = 0, internal = 0;
  int              sites = 0;  // total cell width in sites
};
struct Boxes {
  std::vector<Box> boxes;
  std::vector<int> box_of;  // per movable
  size_t           merges = 0;
};
// fixed_slot: cells legroute.global placed (movable id -> tile); each slot's cells share one box.
Boxes build_boxes(const Design&, const Grid&, const std::map<int, int>& fixed_slot = {});
int   cell_sites(const Design&, const Grid&, int movable);
bool  box_fits(const Design&, const Grid&, const Box&, int tile);

// ---------------------------------------------------------------- L3 SAT placement
void                     pack_cells(const Design&, const Grid&, const Boxes&);  // trivial in-box packing (F1 refines)
std::vector<std::string> tier_names(const std::string& tiers);                  // Legroute_options::tiers split, blanks dropped

// ---------------------------------------------------------------- routing model
struct Vpin {  // a virtual IO pin pair on a boundary tile edge: inside node (tile) -> outside node (tile)
  int net = -1, inside = -1, outside = -1;  // node ids of the net's tree
  int seg = -1;                             // the hop across the edge, layer and track fixed by L5
};
struct Node {
  int              tile = -1, box = -1, term = -1, vpin = -1;  // box pins, die IO terminal, virtual pin end, Steiner point
  std::vector<int> pins;                                       // the net's pins (Net::pins indices) in this tile
  int              riser_layer = -1;                           // L7: the vertical wire every attachment of the node meets
  int64_t          riser_track = -1;
  int64_t          stub_track  = -1;  // L7: lowest horizontal layer track from a die IO or fixed-cell pin to the riser
};
struct Conn {
  int                              net = -1, a = -1, b = -1;  // node ids of the net's tree
  int                              region   = -1;
  unsigned                         weight   = 1;
  bool                             critical = false, clock = false;
  std::vector<std::pair<int, int>> poly;  // tile (col, row) corners of the pattern route, a to b
  std::vector<int>                 segs;  // segments in poly order (L6 layers)
};
struct Segment {
  int     conn = -1, net = -1, layer = -1;
  int     band = 0;                    // tile row (horizontal) or column (vertical)
  int     lo = 0, hi = 0;              // tile range along the segment, inclusive
  int     node_lo = -1, node_hi = -1;  // the node an end attaches to (-1: a bend or a clock spine joint)
  int64_t track  = -1;                 // track index (L7)
  int64_t jog_lo = -1, jog_hi = -1;    // L7: lowest horizontal layer track of the jog to a node's riser, per end
  bool    critical = false, clock = false;
};
struct Spine {  // one clock domain in one region: a trunk, ribs on fixed tracks, one tap per flop box
  int                              net = -1, region = -1, trunk = -1, node = -1;  // node: where the clock tree enters the trunk
  std::vector<int>                 ribs;
  std::vector<std::pair<int, int>> taps;  // (box, tap segment from the box's tile to its rib)
};
struct Tree {
  std::vector<Node> nodes;
  std::vector<int>  conns;  // conn ids
};
struct Routing {
  std::vector<Tree>                 trees;  // per net
  std::vector<Conn>                 conns;
  std::vector<Vpin>                 vpins;
  std::vector<Segment>              segments;
  std::vector<Spine>                spines;
  // 2D demand (H, V) per tile and per-layer demand per tile
  std::vector<int32_t>              dem_h, dem_v, cap_h, cap_v;
  std::vector<double>               hist_h, hist_v;
  std::vector<std::vector<int32_t>> layer_dem;
  // per net: all its pins sit in boxes within one 2x2 tile window; legroute.final routes it whole on the low
  // layers, the global router never sees it
  std::vector<char>                 local;
  // per tile: in-box demand of its box on the box layer (H) and the lowest vertical layer, in tracks
  std::vector<double>               box_dem_h, box_dem_v;
  size_t overflow = 0, critical_conns = 0, pin_edge_moves = 0, vpin_overflow = 0, clock_domains = 0, clock_ribs = 0, unassigned = 0;
};
// In-box demand: local nets and pin taps per box, charged to the box layer first; the excess and the vertical part
// take tracks of the lowest routed layers over the box's tile. Marks the local nets.
void             box_demand(const Design&, const Grid&, const Boxes&, Routing&);
// L3: per region, boxes on tiles and connection shapes (straight or L) whose legs fit the tracks of every tile;
// fills the trees and connections of R.
void             sat_place(const Design&, const Grid&, Boxes&, const Legroute_regions&, Routing&, Legroute_report&);
// the second SAT policy: the options with Legroute_options::sat_alt's overrides (std::invalid_argument on a bad one)
Legroute_options sat_alt_options(const Legroute_options&);
void             clock_spines(const Design&, const Grid&, const Boxes&, Routing&);       // L4
void             assign_layers(const Design&, const Grid&, Routing&);                    // L5 layers
void             assign_tracks(const Design&, const Grid&, Routing&, unsigned threads);  // L5 tracks
double           wire_um(const Grid&, const Conn&);
// Elmore estimate (ps) of a net's worst sink over its routed tree, with the layer RC of its segments.
double           net_delay_ps(const Design&, const Grid&, const Boxes&, const Routing&, int net);

// ---------------------------------------------------------------- legroute.final
void final_place(const Design&, const Grid&, const Boxes&, const Routing&, unsigned threads);                           // F1
void final_wires(const Design&, const Grid&, const Boxes&, const Routing&, const Legroute_regions&, Legroute_report&);  // F2-F4
}  // namespace livehd::placement::lr
