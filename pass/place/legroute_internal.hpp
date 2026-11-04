// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// legroute internals shared by its steps (legroute_*.cpp); not a public interface.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "legroute.hpp"
#include "legroute_resources.hpp"

namespace livehd::placement {
struct Signal_route_via;
}
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
  bool             requires_final_route() const { return routed || (!power && clock && pins.size() >= 2); }
};
struct Design {
  const physical::Technology&         tech;
  physical::Floorplan&                fp;
  const Legroute_options&             opt;
  const physical::Tech_profile*       profile = nullptr;
  std::vector<const physical::Macro*> macro;    // per component
  std::vector<int>                    movable;  // component ids of movable cells (one row, or two rows: a tile's height)
  std::vector<int>                    cell;     // component -> movable index, -1 otherwise
  std::vector<int64_t>                width;    // per movable (DBU)
  std::vector<bool>                   flop;     // per movable: has a clock pin
  std::vector<char>                   tall;     // per movable: a two-row cell, taking the same sites in both rows of its tile
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
// Select the same physical reserved tracks in coarse capacity and track assignment.
// Density mode uses an absolute-coordinate phase, independent of tile dimensions.
class Track_reservations {
  unsigned                                 legacy_count_;
  long double                              per_um_;
  int64_t                                  dbu_;
  std::map<std::string, bool, std::less<>> layers_;

public:
  Track_reservations(const Legroute_options&, const physical::Technology&, int64_t dbu);
  bool density_mode() const { return per_um_ >= 0; }
  bool enabled(std::string_view layer) const { return layers_.empty() || layers_.contains(layer); }
  // previous is the preceding physical track, not the preceding tile's edge.
  // For the first finite M2 track use its originating lattice's virtual predecessor.
  bool selected(std::string_view layer, int64_t coordinate, int64_t previous, int64_t ordinal_in_band) const;
};
struct Grid {
  int64_t                                    x0 = 0, y0 = 0, site = 0, row_h = 0, tile_w = 0, tile_h = 0, dbu = 1000;
  physical::Rect                             die{-1e18, -1e18, 1e18, 1e18};  // routed metal stays inside
  int                                        sites = 0, cols = 0, rows = 0;
  std::vector<uint32_t>                      key;   // per tile: free-segment pattern
  std::vector<std::vector<Seg>>              keys;  // pattern -> segments (sorted)
  std::vector<uint8_t>                       blocked;
  std::vector<uint8_t>                       box_short;    // free tile short of the cellbox budget: hosts no box
  std::vector<physical::Orientation>         row_orient;   // per placement row (2 per tile row)
  std::vector<uint8_t>                       row_present;  // per placement row
  // Free physical placement sites after fixed obstacles, before legal-box overhang reservations.
  std::vector<int64_t>                       placeable_sites;
  // regions: tile sets (rectangles of the region mesh today; nothing below reads rectangle bounds)
  std::vector<int>                           region;
  int                                        regions = 1;
  std::vector<std::vector<int>>              region_tiles;
  // routing layers above the box layer, bottom-up; capacity per layer and tile (usable tracks across the tile)
  std::string                                pin_layer, box_layer, box_cut;
  std::vector<Layer>                         layers;
  std::vector<std::vector<int16_t>>          cap;
  std::vector<int16_t>                       box_cap;    // per tile: usable box-layer tracks (in-box routes and local nets only)
  Track_resource_catalog                     resources;  // physical identities underlying coarse counts; not a DRC certificate
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
// A physical pin's coarse attachment, shared by planning, region crossings and
// final tapping. Boundary access may extend along a clear preferred-layer track
// from a macro pin that reaches its body's edge; it never opens macro capacity.
// Detailed pin/via access remains the final router's responsibility.
struct Pin_access {
  double x = 0, y = 0;
  int    tile = -1;
};
Pin_access physical_pin_access(const Design&, const Grid&, const Pin&);

// Cells of widths (sites) packed into free segments: widest first into the roomiest segment that fits (a cell's
// preferred segment first when given). The segment of each cell, or empty when they do not fit.
std::vector<int> exact_pack_segments(const std::vector<int>& widths, const std::vector<int>& room);
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
  int              sites = 0;    // total cell width in sites
  int              window = -1;  // alternatives: this box moves at most this many tiles from its home (-1: window_tiles)
  bool             placed = false;  // legal_boxes: its cells sit legally on its home tile (cx, cy): that tile fits by construction
};
struct Boxes {
  std::vector<Box> boxes;
  std::vector<int> box_of;  // per movable
  size_t           merges = 0;
};
// One clock's boxed flops in one region (their tiles): the trunk column, the rib rows every `pitch` tile rows
// centred on the flop rows, each rib's column span, and the rib each flop taps. clock_spines builds exactly this.
struct Clock_domain {
  int                                trunk_c = 0;
  std::vector<int>                   ribs;
  std::map<int, std::pair<int, int>> span;     // rib row -> column range
  std::vector<int>                   tap_rib;  // per input tile
};
Clock_domain plan_clock_domain(const Grid&, int region, const std::vector<int>& tiles, int pitch);
// The tracks the L4 clock tree would take per tile and direction (trunk, ribs, one tap per boxed flop), from the
// boxes' tiles (a box without one: its centroid tile), or each clock sink cell's own tile without boxes. A
// provisional estimate before SAT moves boxes; clock_spines takes the real tree from the routing capacities later.
struct Clock_charge {
  std::vector<int> h, v;
};
Clock_charge clock_tree_charge(const Design&, const Grid&, const Boxes*, unsigned rib_tiles);
// fixed_slot: cells legroute.global placed (movable id -> tile); each slot's cells share one box.
Boxes build_boxes(const Design&, const Grid&, const std::map<int, int>& fixed_slot = {});
int   cell_sites(const Design&, const Grid&, int movable);
bool  box_fits(const Design&, const Grid&, const Box&, int tile);
struct Box_fit_repair {
  size_t before = 0, after = 0, eliminated_boxes = 0, moved_cells = 0, attempts = 0;
};
// Rebalance grouping only; physical cell poses and routing capacities stay fixed.
Box_fit_repair repair_box_slots(const Design&, const Grid&, Boxes&, double pin_limit);

struct Box_window {
  int c0 = 0, c1 = -1, r0 = 0, r1 = -1;
};
struct Box_domains {
  std::vector<Box_window> windows;   // indexed like B, around the original centers
  std::set<int>           lifted;    // allowed-track restrictions released by matching
  std::vector<int>        matching;  // indexed like B; empty when preparation stops incomplete
  size_t                  relaxed_windows = 0;
};
// Native window growth and joint fitting only: never mutates boxes or their
// physical pose. The caller owns its deadline through check().
Box_domains prepare_box_domains(const Design&, const Legroute_options&, const Grid&, int region, const std::vector<int>& B,
                                const std::vector<int>& T, const Boxes&, const std::unordered_map<int, std::vector<int>>& allowed,
                                const std::vector<char>& pinned_tile, const std::function<void()>& check);
std::vector<int> box_domain_tiles(const Design&, const Grid&, int box, const Box_window&, const Boxes&,
                                  const std::unordered_map<int, std::vector<int>>& allowed, const std::set<int>& lifted,
                                  const std::vector<char>& pinned_tile, const std::vector<int>& T,
                                  const std::function<void()>& check);

// ---------------------------------------------------------------- L3 legroute.global: the crossing nets' trees
// A region-crossing net routed over the tile grid ahead of the regions: a tree of tile edges joining its endpoint
// tiles (its boxes' placement tiles, its fixed pins, the hop tiles of its fixed virtual pins) by shortest paths on
// the edges with room: an edge carries at most a share of the narrower tile's tracks in its direction, the fuller
// an edge the costlier, most critical nets first. The fixed edges of a net (a virtual pin's hop) cost it nothing.
struct Global_net {
  int                              net  = -1;
  double                           crit = 0;  // order: highest first, then the short nets
  std::vector<int>                 endpoints;  // distinct tiles, the driver's first
  std::vector<std::pair<int, int>> fixed_edges;
  // the regions holding the net's own pins: the region's model routes the net there itself, so the tree's edges
  // inside them cost length only and take no room (only the boundary crossings and the pass-through runs do)
  std::vector<int>                 own_regions;
};
struct Global_tree {
  bool                             routed = false;
  std::vector<std::pair<int, int>> edges;  // adjacent tile pairs, lower tile first
  bool                             search_limited        = false;
  bool                             topology_disconnected = false;  // exact only in the current coarse capacity graph
  size_t                           last_attempt_pops = 0, search_pops = 0, search_attempts = 0;
};
struct Global_routes {
  std::vector<Global_tree> trees;  // per Global_net
  size_t                   unrouted = 0, edges_full = 0;  // nets without a path; edges filled to their share
  size_t                   overflow_edges = 0, overflow_tracks = 0;  // edges over their share after the rounds, by how much
  std::vector<std::pair<int, int>> over;  // those edges: (lower / left tile, h)
  unsigned                 rounds   = 0;                             // negotiation rounds run
  size_t                           search_pops = 0, search_pushes = 0, search_restarts = 0;
  size_t                           search_limited        = 0;
  size_t                           topology_disconnected = 0;  // final nets rejected by the static capacity precheck
  size_t                           pattern_attempts = 0, pattern_completed = 0, pattern_fallbacks = 0;
  size_t                           pattern_work = 0, pattern_work_limited = 0;
  double                   seconds  = 0;
};
struct Box_route_seed_result {
  bool          accepted = false;
  // Separate endpoint view: the original Boxes still define placement domains and physical cell membership.
  Boxes         boxes;
  Global_routes routes;  // native Design net indices
  size_t        matched = 0, moved = 0;
  double        matching_seconds = 0, seconds = 0;
  std::string   reason;
};
Box_route_seed_result prepare_box_route_seed(const Design&, const Grid&, const Boxes&, const std::vector<int>& cap_h,
                                             const std::vector<int>& cap_v, std::string_view stage);
// cap_h[t], cap_v[t]: the tile's tracks per direction; margin_tiles: the search box around a net's endpoints; rounds:
// negotiation rounds (rip-up and reroute of the nets on edges over their share; 0: a full edge is closed, one pass)
Global_routes         global_route(const Grid&, const std::vector<int>& cap_h, const std::vector<int>& cap_v, double share,
                                   double own_share, const std::vector<Global_net>&, unsigned margin_tiles, unsigned rounds,
                                   size_t net_pops = 0, double astar_weight = 1, bool pattern_seed = false);
// The density ceiling (Legroute_options::density_ceiling, user policy 0.90): physical cell density per region
// and the share of an edge's tracks any admitted, SAT or recounted demand may use. More conservative shares
// (global_share, hop_share) and the cap_scale experiment apply under it; neither multiplies it again.
inline double admission_share(const Legroute_options& options) { return std::min(options.global_share, options.density_ceiling); }
// Distinct nets an edge (or tile direction) of physical capacity C may carry: the narrower of the cap_scale
// experiment and the ceiling.
inline int    edge_limit(int capacity, const Legroute_options& options) {
  // Apply the ceiling before multiplying: a valid experimental scale can
  // exceed the integer range even though the admitted capacity never does.
  return std::max(0, int(std::floor(capacity * std::min(options.cap_scale, options.density_ceiling))));
}
inline bool movable_route_partition(const Legroute_options& options) { return options.partition_cells && !options.legal_boxes; }

// tree_nets maps route-tree indices to Design::nets (empty means identity). Region size is checked only after cuts are ready.
Legroute_gate_report assess_legroute_gates(const Design&, const Grid&, const Boxes*, const Global_routes&,
                                           const std::vector<int>& tree_nets, const std::vector<int>& cap_h,
                                           const std::vector<int>& cap_v, bool regions_ready,
                                           std::vector<char>* hot_tiles = nullptr);

// Cell fit at the coarse routing granularity, before any global route. Windows of whole coarse tiles, about
// `window` coarse-tile long sides square in DBU, overlap by half a window. A window is dense when its movable
// CORE area exceeds `cap` of its free row-site area (after fixed bodies and blockages). The route assumes cells
// where they are; a dense window cannot hold them, so routing such a pose is wasted work.
struct Window_fit {
  int                 cols = 0, rows = 0;  // window size in tiles
  size_t              windows = 0, dense_windows = 0;
  double              max_density = 0;
  std::vector<double> tile_pressure;  // per tile: max over its dense windows of density / cap (1 when none)
};
Window_fit assess_window_fit(const Design&, const Grid&, double cap, unsigned window);
// The authoritative fit test when cell-boxes exist: local's own joint matching of every box to a distinct fitting
// tile near its centre over the whole core (native windows, grown only as the matching needs). It fits when the
// matching is complete with at most 1% of the boxes beyond window_tiles and none beyond four times it. With apply, a
// fitting matching moves each movable box's private centre (cx, cy) to its matched tile, so planning and local
// route and cut from the same positions. Physical cells never move. Deterministic for equal inputs.
struct Fit_match {
  bool                matched = false, fits = false, applied = false;
  size_t              far_boxes = 0, max_move = 0;
  // why it does not fit: "fixed" (a fixed box off-range, blocked, colliding or not fitting its tile), "count"
  // (more movable boxes than free tiles: a pigeonhole, no search), "far_bound" (the strict-radius maximum matching
  // leaves more than 1% of the movable boxes unplaced: a proven lower bound), "budget" (the bounded search
  // stopped; not a proof), "domains" (no complete matching inside the grown windows), "far" (a complete heuristic
  // witness moves more than 1% of the movable boxes beyond the window, or one beyond four times it); empty when it fits
  std::string         reason;
  size_t              movable_boxes = 0, free_tiles = 0;
  std::vector<double> tile_pressure;  // per tile: > 1 at the homes of boxes moved beyond window_tiles
  // boxes whose cells should spread on a failure: unplaced boxes, the matched owners their failed (last-reach) alternating
  // search reached within that reach of the root (moving those releases the tiles it needs), and the far boxes of an
  // over-quota witness
  std::vector<int>    pressure_boxes;
};
Fit_match match_box_fit(const Design&, const Grid&, Boxes&, bool apply);

// Complete signal trees independent of any region mesh; trees are indexed by Design::nets, including local nets.
std::vector<Global_net> all_signal_requests(const Design&, const Grid&, const Boxes&);
Global_routes           route_all_signals(const Design&, const Grid&, const Boxes&, const std::vector<int>& cap_h,
                                          const std::vector<int>& cap_v);
// Per-edge constraints shared by initial partitioning and later mesh repairs.
struct Route_cuts {
  std::vector<size_t> load_h, load_v;
  std::vector<size_t> critical_h, critical_v;
  std::vector<int>    limit_h, limit_v;
  std::vector<char>   forbidden_h, forbidden_v;
  bool                allows(const Grid&, const std::vector<int>& regions) const;
  // capacity_only ignores protected critical trees and isolated tiles: only
  // components joined by overloaded edges constrain the congestion recovery.
  size_t              minimum_region_cells(const Grid&, const std::vector<size_t>& cells, bool capacity_only = false) const;
};
Route_cuts route_cuts(const Grid&, const Global_routes&, const std::vector<char>& critical, const std::vector<int>& cap_h,
                      const std::vector<int>& cap_v, double share, bool protect_critical = true);
struct Route_partition {
  std::vector<int> region;
  int              regions   = 0;
  size_t           oversized = 0, max_cells = 0, indivisible_cells = 0;
  size_t           crossings = 0, critical_crossings = 0;
};
bool             connected_regions(const Grid&, const std::vector<int>& regions);
std::vector<int> partition_mesh_regions(const Grid&, const std::vector<size_t>& cells, const Route_cuts&, double critical_cost,
                                        unsigned max_cells);
// Refine only regions with a nonzero target. Preserve every other tile label and
// reuse the parent's label for its first child; additional labels are appended.
std::vector<int> refine_route_regions(const Grid&, const std::vector<size_t>& cells, const Route_cuts&, double critical_cost,
                                      const std::vector<unsigned>& targets, bool mesh = false);
// Rectangular bisection never cuts overused edges. Negative critical_cost forbids critical crossings; otherwise
// it penalizes them. Missing trees cannot certify cuts.
Route_partition  partition_routes(const Grid&, const std::vector<size_t>& cells, const Global_routes&,
                                  const std::vector<char>& critical, const std::vector<int>& cap_h, const std::vector<int>& cap_v,
                                  double share, unsigned max_cells, double critical_cost = -1, bool mesh = false);
struct Partition_fit_report {
  size_t missing_before = 0, missing_after = 0, dense_before = 0, dense_after = 0, oversized_before = 0, oversized_after = 0;
  size_t transfers = 0, patches = 0, exchanges = 0, attempts = 0, work = 0, work_limit = 0;
  bool   assessed = false, resolved = false, limited = false;
  double seconds = 0;
};
// Repair only movable route-driven cuts. Transfer connected boundary tiles with
// their original home-box ownership; preserve boxes, pose, coarse paths and all
// forbidden cuts. The final admission check remains authoritative.
Partition_fit_report repair_partition_fit(const Design&, Grid&, Boxes&, const Route_cuts&);

// ---------------------------------------------------------------- L3 SAT placement
void                     pack_cells(const Design&, const Grid&, const Boxes&);  // trivial in-box packing (F1 refines)
std::vector<std::string> tier_names(const std::string& tiers);                  // Legroute_options::tiers split, blanks dropped

// ---------------------------------------------------------------- routing model
struct Route_piece {
  int  net = -1, ta = -1, tb = -1;
  bool h_first = true;
};
// Refine an accepted regional net-edge union into maximal straight runs.
// Preserve protected (net,tile) attachments, bends, branches, and explicit
// zero-length endpoints. Reject any change to the expanded edge union or region.
std::vector<Route_piece> coalesce_region_routes(const Grid&, int region, std::span<const Route_piece>,
                                                const std::set<std::pair<int, int>>& protected_nodes);
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
  uint8_t          riser_state = 0, stub_state = 0;  // Track_state
};
struct Conn {
  int                              net = -1, a = -1, b = -1;  // node ids of the net's tree
  int                              region   = -1;
  unsigned                         weight   = 1;
  bool                             critical = false, clock = false;
  std::vector<std::pair<int, int>> poly;  // tile (col, row) corners of the pattern route, a to b
  std::vector<int>                 segs;  // segments in poly order (L6 layers)
};
// How an L5 track was obtained: none; checked (passed every pick filter); prescribed (given, e.g. a virtual pin
// hop's fixed track, not checked by pick); fallback (the least-conflicting proposal after every check failed)
enum class Track_state : uint8_t { none, checked, prescribed, fallback };
inline uint8_t track_state(bool ok) { return uint8_t(ok ? Track_state::checked : Track_state::fallback); }
struct Segment {
  int     conn = -1, net = -1, layer = -1;
  int     band = 0;                    // tile row (horizontal) or column (vertical)
  int     lo = 0, hi = 0;              // tile range along the segment, inclusive
  int     node_lo = -1, node_hi = -1;  // the node an end attaches to (-1: a bend or a clock spine joint)
  int64_t track  = -1;                 // track index (L7)
  uint8_t track_state = 0;                  // Track_state: none / checked / prescribed / fallback
  int64_t jog_lo = -1, jog_hi = -1;    // L7: lowest horizontal layer track of the jog to a node's riser, per end
  uint8_t jog_lo_state = 0, jog_hi_state = 0;
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
  // the clock tracks local reserved per tile (H, V) before SAT, checked against the tree L4 builds afterwards
  std::vector<int> clock_reserved_h, clock_reserved_v;
  std::map<std::string, size_t> track_failures;
  std::map<std::string, size_t> track_pick_rejections;           // LAYER:filter -> failed pick attempts
  std::map<std::string, size_t> track_fallback_shadow;           // l5_fallback_shadow diagnostic counts
  std::vector<std::string>      track_failure_samples;           // l5_fallback_shadow: per-track blockers of failed final requests
  size_t layer_overflow_segments = 0, layer_overflow_tiles = 0;  // assign_layers: segments placed on an overfull layer
};
// In-box demand: local nets and pin taps per box, charged to the box layer first; the excess and the vertical part
// take tracks of the lowest routed layers over the box's tile. Marks the local nets.
void             box_demand(const Design&, const Grid&, const Boxes&, Routing&);
// L3: per region, boxes on tiles and connection shapes (straight or L) whose legs fit the tracks of every tile;
// fills the trees and connections of R.
void             sat_place(const Design&, Grid&, Boxes&, const Legroute_regions&, Routing&, Legroute_report&);
// Earlier entries choose shared interfaces before every lower-priority neighbor;
// independent regions may still share a wave.
std::vector<unsigned> region_solve_waves(const Grid&, const std::vector<size_t>& priority);
// the second SAT policy: the options with Legroute_options::sat_alt's overrides (std::invalid_argument on a bad one)
Legroute_options sat_alt_options(const Legroute_options&);
void             clock_spines(const Design&, const Grid&, const Boxes&, Routing&);       // L4
// Final's planned global metal per net (Design::nets index): wire paths from the L5 tracks (segments with their
// bends and node ends, node risers, jogs, IO/fixed-pin stubs, clock trunks/ribs/taps) and the layer joins to be
// made by the router (from layer, to layer, x, y). Final and the box dump both use it.
struct Final_planned_geometry {
  std::vector<std::vector<physical::Route_path>>                                   paths;
  std::vector<std::vector<std::tuple<std::string, std::string, int64_t, int64_t>>> joins;
  std::vector<std::string>                                                         io_stub_failure;
  std::vector<std::vector<uint8_t>> path_state;  // per path: Track_state of the L5 track it was drawn on
};
Final_planned_geometry final_planned_geometry(const Design&, const Grid&, const Boxes&, const Routing&);
void             assign_layers(const Design&, const Grid&, Routing&);                    // L5 layers
void             assign_tracks(const Design&, const Grid&, Routing&, unsigned threads);  // L5 tracks
double           wire_um(const Grid&, const Conn&);
// Elmore estimate (ps) of a net's worst sink over its routed tree, with the layer RC of its segments.
double           net_delay_ps(const Design&, const Grid&, const Boxes&, const Routing&, int net);

// ---------------------------------------------------------------- legroute.final
// Transactional cuts of a net's retained and pending route pieces. Exact via
// reuse is allowed; every distinct via keeps the shared native cut spacing.
class Final_cut_state {
  struct State;
  std::unique_ptr<State> state_;

public:
  Final_cut_state(const physical::Technology&, const physical::Floorplan&);
  ~Final_cut_state();
  // Empty on success. Failure leaves every earlier accepted piece unchanged.
  std::string                   add(std::span<const physical::Route_path>);
  // Exact retained/pending identities for a non-mutating route request.
  std::vector<Signal_route_via> vias() const;
};
// legroute.dump_regions: each chosen region ("all", "failed" or ids) as a DEF of its own in dir (region_<r>.def/.json)
void dump_regions(const Design&, const Grid&, const Boxes&, const Routing&, const Legroute_regions&, const Legroute_report&,
                  const std::string& dir, const std::string& which);
void final_place(const Design&, const Grid&, const Boxes&, const Routing&, unsigned threads);                           // F1
void final_wires(const Design&, const Grid&, const Boxes&, const Routing&, const Legroute_regions&, Legroute_report&);  // F2-F4
Legroute_congestion route_congestion(const Design&, const Grid&, const Global_routes&, const std::vector<Global_net>&,
                                     const std::vector<int>& cap_h, const std::vector<int>& cap_v);
}  // namespace livehd::placement::lr
