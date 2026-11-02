// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/timing_feedback.hpp"
#include "inou/lefdef/physical.hpp"
#include "inou/lefdef/tech_profile.hpp"

namespace livehd::placement {
// legroute: route-aware legalization (todo/livehd/2l-legroute.html). Cells are grouped into 2x2 cell-boxes; per
// region a SAT solver places the boxes on a tile grid so that their routes fit the tracks by construction, and the
// metal between them is laid on tracks (legroute.local). legroute.final places the cells inside each box and taps
// their pins onto the routed metal, so the floorplan leaves with legal cells and routed signal wires.
struct Legroute_options {
  // L2 cell-boxes (variant c1_T20_pa_cd4 of the prototype)
  double      box_fill       = 1;    // fill cap of each box row (the measured fill lands near 0.8)
  unsigned    box_net_max    = 10;   // nets with more pins never drive a merge
  double      merge_cost     = 20;   // no merge above this cost
  double      crit_fraction  = .2;   // top slack-rank fraction that counts as critical
  double      crit_window_ps = 200;  // nets this close to WNS get timing weight (1 + 8 crit^2)
  // legroute.local: the SAT placement per region (CaDiCaL) and the tracks it routes on
  unsigned    sat_conflicts = 10000000, sat_ms = 60000;  // per solve
  unsigned    window_tiles     = 4;  // a box moves at most this many tiles from its placement tile (an 8x8 window)
  unsigned    sat_relax_rounds = 0;  // diagnostic: on UNSAT, drop this many cores of tile bounds and report the deficit
  double      cap_scale        = 1;  // experiment: scales every tile's tracks in the SAT model (how far from routable)
  // in-box H demand of the internal nets: false = one box-layer track each; true = the most internal-net spans that
  // overlap at one x of the packed box (nets end to end share a track)
  bool        inbox_span       = false;
  // route ahead of the SAT one track per feedthrough (pin to pin, no box) and per pin -> box -> pin pair, the box held
  // to the track's tiles: those nets leave the model
  bool        io_preroute      = false;
  // the most critical boxes (this fraction of all boxes) get their tiles for timing first, fixed in the SAT (0: off)
  double      crit_boxes       = 0;
  // fixed 1:n trees: the highest-fanout nets' boxes move at most tree_moves tiles to shorten a single-trunk Steiner
  // tree, then stay fixed with the tree's tracks reserved, while the fixed boxes stay within this fraction (0: off)
  double      tree_boxes       = 0;
  unsigned    tree_moves       = 2;
  // tree selection by load (on when tree_load > 0): try every 1:n net, highest fanout first (or most critical), skip
  // one that would leave less than 1 - tree_load of some tile's H or V tracks free (tree tracks, fixed boxes' in-box
  // demand, straight connections between fixed boxes); a region stops once its model estimate is under tree_clauses
  double      tree_load        = 0;
  double      tree_clauses     = 10e6;
  bool        tree_by_crit     = false;
  // tree moves and trunk axes also avoid congestion: a Poisson potential of the per-tile routing demand over the tracks
  // (DCGP-style), this many tile lengths per tile at full potential, times 4 (0: off)
  double      tree_congestion  = 0;
  // experiment: a net with 3 to this many endpoints in a region is one single-trunk Steiner tree in the SAT (a trunk on
  // a row or column, ribs to it) instead of a star of L-shapes from its first endpoint (below 3: stars only; no gain
  // measured on sha3 and minimax)
  unsigned    steiner_pins     = 0;
  // the fewest endpoints a net needs for the trunk encoding (2: a 2-pin net is a Z-shape, its trunk on any row or
  // column between its endpoints, instead of an L)
  unsigned    steiner_min      = 3;
  // the share of a boundary tile's tracks the hops of region-crossing nets may take; past it a hop takes the least
  // loaded tile of its boundary (counted as over the share), so every crossing is modeled
  double      hop_share        = .5;
  // hop_choice: a crossing net's hop may move up to this many tiles along its boundary, chosen by the first of its two
  // regions to solve: the regions solve in checkerboard waves (no two neighbours in one wave), a wave's choices are the
  // next wave's fixed endpoints. 0: every hop fixed ahead (the hop chooser's tile)
  unsigned    hop_choice       = 0;
  // spill_rounds: after the regions solve, a region without a routable answer pushes about spill_fraction of its boxes
  // into the regions around it with room (up to the fill that evens them with it; the next ring too when the
  // neighbours are short of room): min cuts inside the region, the neighbours its terminals, pick the boxes and where
  // each goes (spill_cut). The crossings are redone and only the regions whose boxes, hops or fixed routes changed
  // solve again; up to spill_rounds times. A pathological placement spreads out until it routes. 0: off
  unsigned    spill_rounds     = 0;
  double      spill_fraction   = .2;
  // window_grow: in a spill round, a region without a routable answer first widens its boxes' windows by this many
  // tiles (twice at most) and solves again before it spills: more freedom of movement where the routing is tight,
  // bigger models only there. 0: off
  unsigned    window_grow      = 0;
  // spill_shift: a failing region moves its boundary instead of its boxes: the tile strip next to the neighbour its
  // nets reach most (one to four tiles deep, enough to hold spill_fraction of its boxes, the region keeping a
  // connected body) goes to that neighbour with the boxes on it, so the crossing nets' endpoints become the
  // neighbour's without raising its fill
  bool        spill_shift      = false;
  // the share of a tile's tracks, per direction, that the routes fixed ahead of the SAT (hops, IO pre-routes) may
  // take together; the rest stays for the region's own routes
  double      reserve_share    = .5;
  // legroute.global: a region-crossing net whose slack rank is under this fraction joins its regions with an L trace
  // over the tile grid ahead of the regions: every boundary it crosses is a hop, every region it only passes
  // through keeps the run as a fixed piece (reserved on its edges, routed straight), so the long trace is as short
  // as the grid allows and the regions' models never see it (0: every crossing joins by the hop chain)
  double      global_fraction  = .05;
  // legroute.global (global_route): every region-crossing net is routed over the tile grid ahead of the regions, a
  // tree of tile edges by shortest paths on the edges with room (at most global_share of the narrower tile's tracks
  // per edge, the fuller the costlier, most critical nets first; the search box is the net's bounding box plus
  // global_margin tiles, grown when no path is found): its boundary crossings are the hops, its runs through the
  // regions holding none of the net's pins are fixed pieces. A net without a path keeps the hop chain
  bool        global_route     = false;
  double      global_share     = .5;
  // experiment: the router charges a net's own-region edges against this share of their tracks too (0: free, as the
  // region routes them), so a net picks the exit that loads its region's internal cuts least and the pass-throughs
  // see that load
  double      global_own_share = 0;
  unsigned    global_margin    = 8;
  // chained hops of a pass-through net (hop to hop inside a region) avoid the rows and columns earlier pass-throughs
  // took: this many tile widths of distance per fully loaded edge on the run (0: nearest only)
  double      hop_spread       = 6;
  // experiment: capacity bounds added only when an answer overflows them, the model solved again incrementally until
  // none does (0: every bound up front; 1: the V bounds lazily; 2: every bound lazily)
  unsigned    capacity_lazy    = 2;
  // calibration: every box pinned on the tile the fallback placement gives it (the placement a run without an answer
  // keeps); the SAT decides the route shapes only, so a region's verdict tests the model's rules, not the placement
  bool        freeze           = false;
  // the fallback placement (boxes on their nearest fitting tiles) is tried first, as assumptions: a routable answer
  // there is kept, otherwise the free model continues with what the solver learned
  bool        sat_seed         = true;
  // a second policy (comma-separated overrides of crit_boxes, tree_boxes, tree_moves, tree_load, tree_clauses,
  // tree_by_crit, tree_congestion, io_preroute, window_tiles, e.g. "tree_load=0.3,io_preroute=false"): each region
  // solves under both in parallel and keeps a routable answer, the lower weighted route length when both are (empty:
  // one policy)
  std::string sat_alt;
  // a region whose boxes fill at most this fraction of its free tiles is taken as routable without a solve (0: every
  // region solves); the routability probe skips the sparse regions
  double      sat_min_fill    = 0;
  // timing tiers after a routable answer, in this order, each tried in batches of tier_batch constraints, most
  // critical first, every box held on its tile so the answer moves only where a constraint needs it (an UNSAT core
  // frees the boxes in it, or drops the constraints in it, at most tier_rounds times per batch): adjacent = a
  // critical 1-1 net's boxes are neighbors (in-box metal, no lane); pins = the box of a critical net on or next to
  // the net's fixed tile (virtual pin, hop, fixed cell); home = a critical box within one tile of its placement
  // tile; near = a critical connection's two boxes within one tile of each other. Critical: a net's slack rank
  // under tier_fraction. Comma-separated names; empty: no tier
  std::string tiers           = "adjacent,pins,home";
  double      tier_fraction   = .05;
  unsigned    tier_batch      = 8;
  unsigned    tier_rounds     = 8;
  unsigned    tier_ms         = 30000;  // per region, all tiers together (within sat_ms)
  unsigned    reserved_tracks = 1;      // per layer and tile: pin access, clock and power drops
  bool        clock_spine     = true;
  unsigned    rib_tiles       = 4;  // clock rib pitch in tile rows
  // legroute.final: in-box placement, in-box routes, taps and routed wires
  bool        final           = true;
  // per router call (one local net or one pin tap): access candidates, search nodes and wall-clock budget
  unsigned    route_access = 2048, route_patterns = 8192, route_ms = 100;
  unsigned    route_passes     = 3;  // routing passes, failed nets first in the next (the router has no rip-up)
  unsigned    threads          = 0;  // 0: hardware concurrency
  int64_t     macro_halo_x_dbu = 0, macro_halo_y_dbu = 0;
  std::string output_dir;  // legroute.json when set
  // with output_dir: regions written as chips of their own (region_<r>.def: the region's cells at their tiles, a die
  // pin per hop on its track, the power grid and macros inside), for an external router to judge the region alone:
  // "all", "failed" (regions without a routable answer) or comma-separated region ids; empty: none
  std::string dump_regions;
  // the regions solve most at risk first (box fill plus the share of their tracks the routes fixed ahead took), so
  // a failing design fails early; with fail_fast the first region without a routable answer ends the solve (the
  // callers that restart the placement on any failure set it; a run that keeps the placement solves every region)
  bool        fail_fast = false;
};
void validate_legroute_options(const Legroute_options&);
// Region rectangles (DBU) of the busorder region mesh and its fixed virtual IO pins. Empty: one region.
struct Legroute_boundary_pin {
  size_t net = 0;  // floorplan net index
  double x = 0, y = 0;
};
// A region is any contiguous set of tiles: the tiles whose centers fall in its rectangles. rect_region[k] names the
// region of rects[k] (empty: one region per rectangle), so an L-shaped region is two rectangles with one id.
struct Legroute_regions {
  std::vector<physical::Rect>        rects;
  std::vector<int>                   rect_region;
  std::vector<Legroute_boundary_pin> pins;
  // Metal that exists later but not yet in the floorplan (DBU): the planned power grid's rails, stripes and via
  // landings. Stripes take their tracks; every shape blocks the track spans it covers.
  std::vector<physical::Shape>       obstacles;
  // The planned power grid itself (its special nets with wires, and the vias they use): legroute.final routes against
  // it, and the router sees the cells' supply pins attached to it. Empty: the obstacles stand in for it.
  std::vector<physical::Net>         power_nets;
  std::vector<physical::Via>         power_vias;
};
struct Legroute_step {
  std::string name;
  double      seconds = 0;
};
struct Legroute_report {
  int64_t tile_w = 0, tile_h = 0;
  size_t  cols = 0, rows = 0, regions = 0, tiles_free = 0;
  size_t  cells = 0, boxes = 0, internal_nets = 0, external_nets = 0, max_cells_per_box = 0;
  double  fill = 0, mean_cells_per_box = 0, displacement_um = 0, max_displacement_um = 0;
  size_t  spilled_regions = 0;
  size_t  connections = 0, critical_connections = 0, unassigned_segments = 0;
  // the SAT placement: regions solved, model size, solves, windows grown or dropped, regions without an answer
  size_t  sat_regions = 0, sat_variables = 0, sat_clauses = 0, sat_solves = 0, sat_relaxed_windows = 0;
  size_t  sat_unsat_regions = 0, sat_unknown_regions = 0, sat_binding_tiles = 0, sat_unjoined_crossings = 0;
  size_t  sat_cut_regions = 0;  // of the unsat regions, those over the cut bound (nets forced across a tile boundary
                                // beyond its tracks): unsat before any solving
  size_t  sat_waves = 0, sat_hops_free = 0, sat_hops_moved = 0;  // hop_choice: solve waves, hops left to a region's SAT, hops it moved
  size_t  spill_rounds = 0, spilled_boxes = 0;                    // spill: rounds run, boxes pushed out of failing regions
  size_t  windows_grown = 0;                                       // window_grow: regions that widened their windows
  size_t  shifted_tiles = 0;                                       // spill_shift: tiles handed to a neighbour
  size_t  sat_hops_over_share = 0;             // hops placed past hop_share (every boundary tile of their pair was at it)
  size_t  global_nets = 0, global_pieces = 0;  // legroute.global: nets joined by L traces, their pass-through pieces
  size_t  global_unrouted = 0, global_full_edges = 0;  // global_route: nets without a path, edges filled to the share
  size_t  sat_fail_fast_skipped = 0;                    // fail_fast: regions left unsolved after the first failure
  double  global_seconds  = 0;
  size_t  sat_relaxed_tiles = 0, sat_deficit_tracks = 0;  // sat_relax_rounds: dropped tile bounds, tracks short
  size_t  sat_adjacent_nets = 0;                          // 1-1 nets between neighbor boxes: in-box metal, no lane
  // critical pre-placement: boxes fixed before the SAT, and their nets' weighted star length (tiles) at the placement
  // positions and at the chosen tiles
  size_t  crit_pinned = 0, crit_unpinned_regions = 0;
  double  crit_cost_before = 0, crit_cost_after = 0;
  // fixed 1:n trees: nets, boxes they fixed, nets skipped (no free tile), regions that dropped theirs (the other boxes
  // no longer fit), tree length (um) at the placement and fixed, the worst tile's tree tracks over its tracks
  size_t  tree_nets = 0, tree_boxes = 0, tree_skipped_nets = 0, tree_dropped_regions = 0, tree_tiles_over_half = 0;
  double  tree_um_before = 0, tree_um_after = 0, tree_max_load = 0;
  // fixed demand over a tile's tracks before the SAT (fixed boxes' in-box demand, straight connections between fixed
  // endpoints): trees dropped and critical boxes unpinned until no tile is over
  size_t  forced_trees_dropped = 0, forced_crit_unpinned = 0;
  // tree_load selection: nets skipped for load, summed capacity-clause estimates before and after the trees
  size_t  tree_load_skipped = 0, tree_clauses_before = 0, tree_clauses_after = 0;
  // io_preroute: feedthroughs and pin -> box -> pin pairs routed ahead, candidates without room, boxes freed off a track
  size_t  io_feedthroughs = 0, io_boxes = 0, io_skipped = 0, io_lifted = 0;
  // policies: regions that kept the second policy, regions both policies solved
  size_t  sat_alt_regions = 0, sat_both_regions = 0;
  // capacity_lazy: re-solves, bounds added because an answer overflowed them, bounds never needed
  size_t  lazy_rounds = 0, lazy_bounds = 0, lazy_skipped = 0;
  size_t  frozen_boxes        = 0;  // freeze: boxes pinned on their fallback tiles
  size_t  sat_seeded          = 0;  // sat_seed: regions whose answer is the fallback placement
  size_t  sat_skipped_regions = 0;  // sat_min_fill: regions taken as routable without a solve
  // timing tiers: constraints kept, dropped (UNSAT cores), tiers dropped whole (out of time), solves, and the
  // weighted route length (tiles) of the routable answers before and after the tiers
  size_t  tier_kept = 0, tier_dropped = 0, tier_timeouts = 0, tier_solves = 0;
  double  tier_cost_before = 0, tier_cost_after = 0;
  double  tier_crit_before = 0, tier_crit_after = 0;  // the tier nets' connection length (tiles) before and after
  // per region (grid region id): 0 routable, 1 UNSAT, 2 no answer in time, 3 skipped (sat_min_fill); its boxes and
  // free tiles. unroutable_components: the floorplan components of the regions with status 1 or 2
  struct Region_stat {
    int    status = 0;
    size_t boxes = 0, tiles = 0;
  };
  std::vector<Region_stat>      region_stats;
  std::vector<size_t>           unroutable_components;
  std::vector<int>              unroutable_regions;  // per unroutable component: its region
  double                        sat_seconds = 0, sat_seconds_max = 0;
  size_t                        clock_domains = 0, clock_ribs = 0;
  double                        wire_um = 0, critical_wire_um = 0;
  size_t                        vias          = 0;
  // legroute.final routing (the shared signal router). All or nothing: wires_emitted only when every net legroute
  // routes is connected; otherwise no signal wire is written and an external router starts from the legal placement.
  bool                          wires_emitted = false;
  size_t                        route_passes  = 0;  // the routing pass whose result is kept
  size_t                        routed_nets = 0, local_nets = 0, joins = 0, taps = 0, connected_nets = 0, unchecked_nets = 0;
  // global_conflict_nets and tree_fallbacks were routed whole by the router; failed_nets did not close at all
  size_t                        failed_nets = 0, global_conflict_nets = 0, join_failures = 0, tap_failures = 0, tree_fallbacks = 0;
  size_t                        disconnected_nets = 0;
  std::map<std::string, size_t> failure_reasons;        // reason -> nets
  double                        critical_delay_ps = 0;  // worst Elmore wire delay over the critical nets' routed trees (diagnostic)
  double                        seconds           = 0;
  std::vector<Legroute_step>    steps;
  std::vector<std::string>      failure_sample;  // "net: reason" for the first few failed nets
  // Where routing fails: the boxes of the failed nets (worst first) and, per bucket of box fill, internal pins and
  // external pins, all boxes against the boxes with a failed net.
  struct Box_stat {
    size_t box = 0, col = 0, row = 0, cells = 0, internal_pins = 0, external_pins = 0, failed_nets = 0;
    double fill = 0, box_demand = 0, box_capacity = 0, vertical_demand = 0;
  };
  struct Bucket {
    std::string name;
    size_t      boxes = 0, failing = 0;
  };
  std::vector<Box_stat> failure_boxes;
  std::vector<Bucket>   fill_pattern, internal_pattern, external_pattern;
};
// The boxes do not fit the free tiles (a region with more boxes than fitting tiles, a box no tile fits): the
// placement is too dense for the tile grid, and a caller with a generated core may grow it and start over.
struct Legroute_unfit : std::runtime_error {
  using std::runtime_error::runtime_error;
};
// Legal placement of every movable single-row cell, routed signal wires on the floorplan nets (with `final`).
// Multi-row cells must be fixed (anchored) by the caller. Throws std::invalid_argument on unsupported frames and
// std::runtime_error when the boxes cannot fit the free tiles; the floorplan is unchanged on any throw.
Legroute_report legroute(const physical::Technology&, physical::Floorplan&, const timing::Feedback&, const Legroute_options&,
                         const Legroute_regions& = {}, const physical::Tech_profile* = nullptr);
void            write_legroute_report(const Legroute_report&, const std::string& path);

// L1 macro tile snap: the origin of a macro (with its halo) rounded to the tile grid of the design's widest flop
// (2 x flop width, 2 rows), relative to the core origin. Used by macro planning and macro legalization.
struct Legroute_tile {
  int64_t x0 = 0, y0 = 0, w = 0, h = 0;
  bool    valid() const { return w > 0 && h > 0; }
};
Legroute_tile               legroute_tile(const physical::Technology&, const physical::Floorplan&);
// Snapped lower-left of a w x h macro body (DBU) whose halo-expanded frame starts on a tile corner, nearest to
// (x, y), kept inside [lo, hi] when possible.
std::pair<int64_t, int64_t> snap_to_tiles(const Legroute_tile&, int64_t x, int64_t y, int64_t halo_x, int64_t halo_y);
}  // namespace livehd::placement
