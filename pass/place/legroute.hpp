// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
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
  // tree moves (within tree_moves) keep the most tracks free on the tiles they touch first, the tree length second
  bool        tree_room        = false;
  // tree moves and trunk axes also avoid congestion: a Poisson potential of the per-tile routing demand over the tracks
  // (DCGP-style), this many tile lengths per tile at full potential, times 4 (0: off)
  double      tree_congestion  = 0;
  // a second policy (comma-separated overrides of crit_boxes, tree_boxes, tree_moves, tree_load, tree_clauses,
  // tree_by_crit, tree_room, tree_congestion, io_preroute, window_tiles, e.g. "tree_load=0.3,io_preroute=false"): each region
  // solves under both in parallel and keeps a routable answer, the lower weighted route length when both are (empty:
  // one policy)
  std::string sat_alt;
  // density push: after a pass, push_fraction of the boxes of each region without a routable answer move to
  // neighbor regions with free tiles (least critical, nearest first), and the changed regions solve again, at most
  // push_rounds times (0: off)
  unsigned    push_rounds     = 0;
  double      push_fraction   = 0.05;
  // a region whose boxes fill at most this fraction of its free tiles is taken as routable without a solve (0: every
  // region solves); the routability probe skips the sparse regions
  double      sat_min_fill    = 0;
  // experiment: bisect each region by minimum cuts (VieCut) into sub-regions of about this many cells (0: off)
  unsigned    subregion_cells = 0;
  unsigned    reserved_tracks = 1;  // per layer and tile: pin access, clock and power drops
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
  // policies and density push: regions that kept the second policy, regions both solved, push rounds, boxes pushed
  size_t  sat_alt_regions = 0, sat_both_regions = 0, push_rounds = 0, push_moved = 0;
  size_t  push_kept_round     = 0;  // the round whose answers were kept (the most routable regions)
  size_t  sat_skipped_regions = 0;  // sat_min_fill: regions taken as routable without a solve
  // per region (grid region id): 0 routable, 1 UNSAT, 2 no answer in time, 3 skipped (sat_min_fill); its boxes and
  // free tiles. unroutable_components: the floorplan components of the regions with status 1 or 2
  struct Region_stat {
    int    status = 0;
    size_t boxes = 0, tiles = 0;
  };
  std::vector<Region_stat>      region_stats;
  std::vector<size_t>           unroutable_components;
  std::vector<int>              unroutable_regions;  // per unroutable component: its region
  // sub-regions (experiment): crossings (pieces per net beyond the first) for the regions, the min-cut sub-regions
  // and a geometric bisection to the same size
  size_t                        cut_subregions = 0, cut_unbalanced = 0, cut_crossings_regions = 0, cut_crossings_mincut = 0;
  size_t                        cut_crossings_geometric = 0;
  double                        cut_seconds             = 0;
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
