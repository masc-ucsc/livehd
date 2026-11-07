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
// legroute: route-aware legalization (todo/livehd/2l-legroute.html). legroute.plan improves coarse routability
// while cells remain movable, then selects connected regions and their crossings. Optional legroute.global
// reserves routed pieces before legroute.local jointly places cell-boxes and routes them with regional SAT.
// A cell-box occupies one tile: two rows tall and twice the widest flop wide. legroute.final places the cells
// inside each box and connects their pins to physical metal. Coarse feasibility alone is not final signal/clock
// closure; legal_boxes and plan.handoff provide separate legalized witness modes.
struct Legroute_options {
  // Opt-in Plan admission. Physical widths, endpoints and the core stay fixed.
  bool                     critical_cellboxes = false;
  unsigned                 cellbox_sites = 22, cellbox_rows = 4;
  unsigned                 cellbox_external_pins = 28, cellbox_connections = 10;
  unsigned                 cellbox_critical_choices = 4, cellbox_other_choices = 8;
  bool                     cellbox_wide_exceptions = true;
  bool                     cellbox_far_fallback    = false;
  // Runtime retry-controller signal only: never inferred by fixed replay.
  bool                     cellbox_final_retry     = false;
  // Runtime native pre-CTS trace in the exact measured Floorplan::nets order.
  // Derived from control_ports/Liberty roles, never inferred from missing STA.
  std::vector<bool>        cellbox_ignored_nets;
  // L2 cell-boxes (variant c1_T20_pa_cd4 of the prototype)
  double      box_fill         = 1;    // fill cap of each box row (the measured fill lands near 0.8)
  unsigned    box_net_max      = 10;   // nets with more pins never drive a merge
  double      merge_cost       = 20;   // no merge above this cost
  double      crit_fraction    = .2;   // top slack-rank fraction that counts as critical
  double      crit_window_ps   = 200;  // nets this close to WNS get timing weight (1 + 8 crit^2)
  // legroute.local: the SAT placement per region (CaDiCaL) and the tracks it routes on
  unsigned    sat_conflicts    = 10000000;  // per solver call
  unsigned    sat_ms           = 60000;     // one unchanged-geometry regional attempt: construction, policies and internal retries
  unsigned    window_tiles     = 4;         // a box moves at most this many tiles from its placement tile (an 8x8 window)
  unsigned    sat_relax_rounds = 0;         // diagnostic: on UNSAT, drop this many cores of tile bounds and report the deficit
  // the one density ceiling (user policy 0.90): regional cell density, edge admission and every SAT/recount edge
  // bound; more conservative shares and cap_scale apply under it
  double      density_ceiling  = .90;
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
  // regions to solve: harder adjacent regions solve first, and a wave's choices are the
  // next wave's fixed endpoints. 0: every hop fixed ahead (the hop chooser's tile)
  unsigned    hop_choice       = 0;
  // spill_rounds: after the regions solve, a region without a routable answer pushes about spill_fraction of its boxes
  // into the regions around it with room (up to the fill that evens them with it; the next ring too when the
  // neighbours are short of room): min cuts inside the region, the neighbours its terminals, pick the boxes and where
  // each goes (spill_cut). The crossings are redone and only the regions whose boxes, hops or fixed routes changed
  // solve again; up to spill_rounds times. A pathological placement spreads out until it routes. 0: off
  unsigned    spill_rounds     = 0;
  double      spill_fraction   = .2;  // 0: the rounds run the alternatives, the windows and the shift, no box spill
  // window_grow: in a spill round, a region without a routable answer first widens its boxes' windows by this many
  // tiles (twice at most) and solves again before it spills: more freedom of movement where the routing is tight,
  // bigger models only there. 0: off
  unsigned    window_grow      = 0;
  // spill_shift: a failing region moves its boundary instead of its boxes: the tile strip next to the neighbour its
  // nets reach most (one to four tiles deep, enough to hold spill_fraction of its boxes, the region keeping a
  // connected body) goes to that neighbour with the boxes on it, so the crossing nets' endpoints become the
  // neighbour's without raising its fill
  bool        spill_shift      = false;
  // mesh_join: in a spill round a region without a routable answer joins a neighbour and their union splits the other
  // way: across the boundary its crossings overload (a row boundary bad: a column split, and the reverse), or beside
  // the line inside it the cut bound names (the split parallel to that line gives it the union's tracks)
  bool        mesh_join        = false;
  // alternatives: in a spill round, a region without a routable answer first tries alternative placements of its boxes,
  // each solved in parallel with the round's fixed hops, the first routable one in this order adopted: "noise" (every
  // box's home jittered by up to window_tiles tiles), "anchor" (a few boxes of the densest patch moved to the sparsest
  // patch and held within a tile), "tsm" (an orthogonal drawing of the box graph reduced to four edges per node:
  // connected boxes adjacent). Comma-separated; empty: off. alt_ms: per-alternative cap (0: sat_ms), always within
  // the remaining shared regional-attempt deadline; it cannot refresh an expired attempt.
  std::string alternatives;
  unsigned    alt_ms                   = 0;
  bool        alt_all                  = false;  // solve every alternative even after one routes (the trial log then compares them)
  // the "checker" alternative: a relaxed model first (no capacity bound on the edges a checkerboard of tiles owns, the
  // boxes homed on those tiles free to move checker_window tiles, the others twice the window), then the full model
  // from the relaxed answer's tiles with windows of four; an UNSAT relaxed model says the region needs less density
  unsigned    checker_window           = 8;
  // untracked: per tile, the capacity bounds of the edges it owns are skipped (set by the checker alternative only)
  const std::vector<char>* untracked   = nullptr;
  // region_pins: the bus-ordered virtual pins of the region refinement are fixed endpoints of the crossing nets;
  // false keeps the refinement's mesh but lets every crossing hop be chosen (hop_choice) or chained instead
  bool                     region_pins = true;
  // the share of a tile's tracks, per direction, that the routes fixed ahead of the SAT (hops, IO pre-routes) may
  // take together; the rest stays for the region's own routes
  double                   reserve_share            = .5;
  // legroute.global: a region-crossing net whose slack rank is under this fraction joins its regions with an L trace
  // over the tile grid ahead of the regions: every boundary it crosses is a hop, every region it only passes
  // through keeps the run as a fixed piece (reserved on its edges, routed straight), so the long trace is as short
  // as the grid allows and the regions' models never see it (0: every crossing joins by the hop chain)
  double                   global_fraction          = .05;
  // legroute.global (global_route): every region-crossing net is routed over the tile grid ahead of the regions, a
  // tree of tile edges by shortest paths on the edges with room (at most global_share of the narrower tile's tracks
  // per edge, the fuller the costlier, most critical nets first; the search box is the net's bounding box plus
  // global_margin tiles, grown when no path is found): its boundary crossings are the hops, its runs through the
  // regions holding none of the net's pins are fixed pieces. Any failed/overloaded route returns to planning before SAT.
  bool                     global_route             = false;
  // Opt-in bounded FastRoute-derived L/Z seed for <=32 endpoints; failed patterns fall back to the native maze.
  bool                     global_pattern           = false;
  // Match provisional box tiles inside the native adaptive placement domains, then reroute a candidate forest.
  // This supplies an optional local witness; original centers, physical cells and movement domains are unchanged.
  bool                     box_route_seed           = false;
  // Planning precondition: before any global route, every window of about fit_window x the longer coarse-tile
  // side (whole tiles, half-window overlap) must hold its movable cells within fit_density of its free row sites.
  // A dense window returns to placement with local pressure instead of routing a pose its cells cannot occupy.
  // fit_density 0 disables the check.
  double                   fit_density              = .90;
  unsigned                 fit_window               = 2;
  // Fit first (with cell-boxes): planning and local both apply the deterministic whole-core box matching before
  // the coarse route and the cuts, so every box starts on a distinct fitting tile and every region fits by
  // construction. Physical cells never move here; local still legalizes and routes them.
  bool                     fit_first                = false;
  // All-signal routing with uniform edge accounting while retaining the seed mesh (a partition control).
  bool                     global_all               = false;
  // Keep all-signal trees inside regions with fixed legal boxes; SAT still checks their capacity.
  bool                     global_keep              = false;
  // Opt-in route-driven partition: route all signals without the seed mesh, then bisect on admissible grid
  // lines to this cell target. Requires global_route and region_pins=false. With legal_boxes=false, plan/local
  // form the same movable connectivity boxes before cuts, ignoring legacy seed divisions. Zero keeps the seed mesh.
  unsigned                 partition_cells          = 0;
  // Existing region-mesh target supplied by the owner when route-driven partitioning is disabled.
  unsigned                 region_cell_limit        = 0;
  // A protected tree cannot be cut. If no admissible cut exists, report an oversized region instead.
  double                   partition_critical       = .05;
  // -1 forbids critical crossings; a nonnegative value adds this cost per critical crossing / cut capacity.
  double                   partition_critical_cost  = -1;
  // Connected nonrectangular tile regions from route-weighted source/sink cuts instead of straight lines.
  bool                     partition_mesh           = false;
  // Retry timed-out regions with the cell target halved each round, retaining the global trees and
  // unaffected regions. Bounded rounds, independent of spill rounds. Requires fixed boxes/hops and one SAT policy.
  unsigned                 partition_timeout_rounds = 0;
  double                   global_share             = .5;
  // experiment: the router charges a net's own-region edges against this share of their tracks too (0: free, as the
  // region routes them), so a net picks the exit that loads its region's internal cuts least and the pass-throughs
  // see that load
  double                   global_own_share         = 0;
  unsigned                 global_margin            = 8;
  // negotiation rounds of the router: the nets on an edge over its share reroute under a present-congestion cost
  // that doubles each round plus the edge's overuse history (PathFinder / FastRoute); 0: one pass, a full edge closed
  unsigned                 global_rounds            = 16;
  // Queue pops per net routing attempt, across window growth; 0 leaves the search unbounded.
  // Exhaustion is an unresolved search, never proof that capacity or area is insufficient.
  unsigned                 global_net_pops          = 0;
  // Opt-in weighted A*: 1 preserves shortest-path ordering; >1 favors the goal over path cost.
  // Fixed-edge and large-fanout nets retain Dijkstra. This changes no capacity or connectivity check.
  double                   global_astar_weight      = 1;
  // chained hops of a pass-through net (hop to hop inside a region) avoid the rows and columns earlier pass-throughs
  // took: this many tile widths of distance per fully loaded edge on the run (0: nearest only)
  double                   hop_spread               = 6;
  // experiment: capacity bounds added only when an answer overflows them, the model solved again incrementally until
  // none does (0: every bound up front; 1: the V bounds lazily; 2: every bound lazily)
  unsigned                 capacity_lazy            = 2;
  // calibration: every box pinned on the tile the fallback placement gives it (the placement a run without an answer
  // keeps); the SAT decides the route shapes only, so a region's verdict tests the model's rules, not the placement
  bool                     freeze                   = false;
  // legal_boxes: the cell-boxes are the tiles of the legal placement legroute is given (the cells on one tile are that
  // tile's box, fixed there; no cell moves): the SAT routes the placement the ordinary legalization made instead of
  // placing boxes of its own. The terminal stage runs that legalization first
  bool                     legal_boxes              = false;
  // with legal_boxes: a box may move up to this many tiles from its legal tile (its cells repacked on the tile it
  // takes), so the SAT repairs the legalization locally where the routes need it; 0: every box fixed
  unsigned                 legal_window             = 0;
  // with legal_boxes and global_route: the boxes on the tiles of an edge the global router leaves over its share may
  // move this many tiles (their cells repacked), the other legal boxes stay fixed; 0: none
  unsigned                 legal_shuffle            = 0;
  // the fallback placement (boxes on their nearest fitting tiles) is tried first, as assumptions: a routable answer
  // there is kept, otherwise the free model continues with what the solver learned
  bool                     sat_seed                 = true;
  // a second policy (comma-separated overrides of crit_boxes, tree_boxes, tree_moves, tree_load, tree_clauses,
  // tree_by_crit, tree_congestion, io_preroute, window_tiles, e.g. "tree_load=0.3,io_preroute=false"): each region
  // solves under both in parallel and keeps a routable answer, the lower weighted route length when both are (empty:
  // one policy)
  std::string              sat_alt;
  // a region whose boxes fill at most this fraction of its free tiles is taken as routable without a solve (0: every
  // region solves); the routability probe skips the sparse regions
  double                   sat_min_fill           = 0;
  // timing tiers after a routable answer, in this order, each tried in batches of tier_batch constraints, most
  // critical first, every box held on its tile so the answer moves only where a constraint needs it (an UNSAT core
  // frees the boxes in it, or drops the constraints in it, at most tier_rounds times per batch): adjacent = a
  // critical 1-1 net's boxes are neighbors (in-box metal, no lane); pins = the box of a critical net on or next to
  // the net's fixed tile (virtual pin, hop, fixed cell); home = a critical box within one tile of its placement
  // tile; near = a critical connection's two boxes within one tile of each other. Critical: a net's slack rank
  // under tier_fraction. Comma-separated names; empty: no tier
  std::string              tiers                  = "adjacent,pins,home";
  double                   tier_fraction          = .05;
  unsigned                 tier_batch             = 8;
  unsigned                 tier_rounds            = 8;
  unsigned                 tier_ms                = 30000;  // per region, all tiers together (within sat_ms)
  unsigned                 reserved_tracks        = 1;      // per layer and tile: pin access, clock and power drops
  // Opt-in physical-length reservation: -1 keeps reserved_tracks; >=0 reserves this many tracks per micron
  // perpendicular to travel, with the same physical track selection in capacity and track assignment.
  double                   reserved_tracks_per_um = -1;
  std::string              reserved_layers;  // empty: all routing layers; otherwise a comma-separated layer whitelist
  // Per-cellbox budget "LAYER:N,...": every free tile keeps N usable tracks of that layer (after PG and obstacles)
  // out of coarse capacity, the same in plan, global and local; Final allocates the actual tracks. The box layer
  // entry comes off the box-layer capacity, upper layers off their own (layer identity is preserved).
  std::string              box_reserve;  // lhd default "M2:1,M3:1"; empty here so unit fixtures keep their tiny capacities
  // Experimental fixed legal-box policy: replace box_reserve with ceil(external pin obligations / 2) tracks
  // on the lowest vertical layer and one box-layer track per physical row. This is a budget, not a pin-access proof.
  bool                     paired_pin_reserve = false;
  unsigned                 paired_pin_cap     = 0;  // positive even: require balanced even P<=cap and charge cap/2 V uniformly
  // Diagnostic: tiles "col:row;col:row..." (or "all": every placed box) whose box is exported after F1 (output_dir/box_dump: a full
  // post-F1 DEF with the planned PG merged, and one JSON per box: cells, free row segments, incident nets, pins and the L5 metal
  // reaching the tile, marked valid or a failed fallback proposal). Empty: off.
  std::string              final_box_dump;
  bool                     final_box_dump_stop = false;  // diagnostic: return right after the dump (no F2-F4)
  bool                     clock_spine         = true;
  unsigned                 rib_tiles           = 4;  // clock rib pitch in tile rows
  // legroute.final: in-box placement, in-box routes, taps and routed wires
  bool                     final               = true;
  // per router call (one local net or one pin tap): access candidates, search nodes and wall-clock budget
  unsigned                 route_access = 2048, route_patterns = 8192, route_ms = 100;
  // Opt-in: when > 0, a Final piece request's TOTAL shared access allowance is max(route_access, this x its pins); it is
  // not a per-pin allocation or guarantee. 0 (default): route_access is the exact per-call cap.
  unsigned                 route_access_per_pin         = 0;
  // Opt-in experiment: Final does not admit planned paths drawn on L5 fallback tracks (tracks that failed every L5
  // check) as fixed planned metal; default false keeps every planned path.
  bool                     final_drop_fallback_planned  = false;
  // Opt-in L5 diagnostic: a contribution ledger of every track occupancy plus a shadow occupancy without the FALLBACK
  // contributions; every failed pick is re-run with the same predicates on the shadow (assignment is unchanged).
  bool                     l5_fallback_shadow           = false;
  unsigned                 l5_repair_ms                 = 0;  // opt-in shared budget for bounded H track reassignment; <60 s
  // Opt-in L5 experiment: limit the conservative end envelope against signal neighbors to the track offsets where the
  // exact rules find an illegal nominal-width pair (hard/reserved neighbors keep the full query reach).
  bool                     l5_rule_reach                = false;
  // Opt-in: Final admits clock nets' planned metal before every signal net (default: critical, then criticality).
  bool                     final_clock_first            = false;
  // Opt-in: Final checks the complete net's connectivity (retained + pending metal) before committing it.
  bool                     final_precommit_connectivity = false;
  // Opt-in: when > 0, every Final router call for a clock net uses this wall budget (ms) instead of route_ms.
  unsigned                 final_clock_route_ms         = 0;
  // Opt-in: when > 0, every Final router call for a clock net uses this search-node budget instead of route_patterns.
  unsigned                 final_clock_route_patterns   = 0;
  // Opt-in diagnostic: Final writes final_boxes.tsv, final_box_events.tsv and final_box_usage.tsv (retained pass).
  bool                     final_box_report             = false;
  // Opt-in: Final's detailed routing order puts clock nets before every signal net (ahead of retry priority).
  bool                     final_clock_detail_first     = false;
  // Opt-in diagnostic: Final leaves clock nets (final_skip_clock) and nets whose name matches final_skip_nets
  // (ECMAScript regex, case-insensitive) out entirely: nothing admitted or routed, not counted.
  bool                     final_skip_clock             = false;
  std::string              final_skip_nets;
  unsigned                 route_passes          = 3;  // routing passes, failed nets first in the next (the router has no rip-up)
  // API/replay diagnostic: stop after this many detailed nets in the first pass, retaining all global metal.
  // Zero is unlimited; a partial result counts unchecked signal/clock nets and never emits their wires.
  unsigned                 final_net_limit       = 0;
  // API/replay diagnostic: false omits future supply-access reservations for a
  // signal-only comparison. Physical supply pins/wires remain obstacles.
  bool                     protect_supply_access = true;
  unsigned                 threads               = 0;  // 0: hardware concurrency
  int64_t                  macro_halo_x_dbu = 0, macro_halo_y_dbu = 0;
  std::string              output_dir;  // legroute.json when set
  // with output_dir: regions written as chips of their own (region_<r>.def: the region's cells at their tiles, a die
  // pin per hop on its track, the power grid and macros inside), for an external router to judge the region alone:
  // "all", "failed" (regions without a routable answer) or comma-separated region ids; empty: none
  std::string              dump_regions;
  // the regions solve most at risk first (box fill plus the share of their tracks the routes fixed ahead took), so
  // a failing design fails early; with fail_fast the first region without a routable answer ends the solve (the
  // callers that restart the placement on any failure set it; a run that keeps the placement solves every region)
  bool                     fail_fast = false;
};
// box_route_seed needs movable route-driven boxes (partition_cells, no legal_boxes) and none of the policies that
// fix or pre-place boxes; lhd enables it by default exactly when this holds
inline bool legroute_box_route_seed_compatible(const Legroute_options& o) {
  return o.partition_cells && !o.legal_boxes && !o.freeze && o.crit_boxes <= 0 && o.tree_boxes <= 0 && o.tree_load <= 0
         && !o.io_preroute && o.sat_alt.empty();
}
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
  size_t  box_route_seed_attempts = 0, box_route_seed_successes = 0;
  int64_t tile_w = 0, tile_h = 0;
  size_t  cols = 0, rows = 0, regions = 0, tiles_free = 0;
  size_t  cells = 0, boxes = 0, internal_nets = 0, external_nets = 0, max_cells_per_box = 0;
  double  fill = 0, mean_cells_per_box = 0, displacement_um = 0, max_displacement_um = 0;
  // displacement_um / max_displacement_um above are box-centre to selected-tile moves (after any fit_first anchor).
  // These are actual movable-cell moves from the poses legroute was given to the poses it leaves (Manhattan, um).
  double  cell_max_displacement_um = 0, cell_p95_displacement_um = 0;
  size_t  cells_moved     = 0;
  size_t  spilled_regions = 0;
  size_t  connections = 0, critical_connections = 0, unassigned_segments = 0;
  // A successful metal-only audit does not certify vias or complete net connectivity; a failed audit rejects the handoff.
  bool    local_metal_audit_attempted = false, local_metal_audit_complete = false;
  size_t  local_metal_conflict_shapes = 0, local_metal_unknown_shapes = 0;
  bool    local_join_probe_attempted = false, local_join_probe_complete = false;
  bool    local_metal_rejected() const {
    return local_metal_audit_attempted
           && (!local_metal_audit_complete || local_metal_conflict_shapes || local_metal_unknown_shapes);
  }
  bool local_handoff_rejected() const {
    return local_metal_rejected() || (local_join_probe_attempted && !local_join_probe_complete);
  }
  size_t                        l5_repair_attempts = 0, l5_repair_successes = 0, l5_repair_unknown = 0, l5_repair_unsat = 0;
  std::map<std::string, size_t> track_failures;         // assignment failures by wire/attachment kind
  std::map<std::string, size_t> track_pick_rejections;  // L5 failed pick attempts by LAYER:filter stage
  std::map<std::string, size_t> track_fallback_shadow;  // l5_fallback_shadow: LAYER:kind:{failed,shadow_free} counts
  std::vector<std::string>      track_failure_samples;  // l5_fallback_shadow: per-track blockers of failed final requests
  // the SAT placement: regions solved, model size, solves, windows grown or dropped, regions without an answer
  size_t                        sat_regions = 0, sat_variables = 0, sat_clauses = 0, sat_solves = 0, sat_relaxed_windows = 0;
  size_t                        sat_root_fallbacks = 0;  // unsupported fixed star replaced by a domain-supported endpoint root
  size_t                        sat_unsat_regions = 0, sat_unknown_regions = 0, sat_binding_tiles = 0, sat_unjoined_crossings = 0;
  size_t sat_cut_regions = 0;  // of the unsat regions, those over the cut bound (nets forced across a tile boundary
                               // beyond its tracks): unsat before any solving
  size_t sat_waves = 0, sat_hops_free = 0,
         sat_hops_moved = 0;                   // hop_choice: solve waves, hops left to a region's SAT, hops it moved
  size_t spill_rounds = 0, spilled_boxes = 0;  // spill: rounds run, boxes pushed out of failing regions
  size_t windows_grown = 0;                    // window_grow: regions that widened their windows
  size_t shifted_tiles = 0;                    // spill_shift: tiles handed to a neighbour
  size_t alt_tried = 0, alt_rescued = 0;       // alternatives: candidates solved, regions rescued
  double alt_seconds = 0;
  // alternatives: one trial per failing region and round: the region's traits and each family's outcome
  struct Alt_trial {
    int         region = 0;
    unsigned    round  = 0;
    double      fill = 0, cut = 0;  // movable boxes over free tiles; the placement cut ratio (risk order)
    size_t      boxes = 0, hops = 0, pinned = 0;
    size_t      boundary_tracks = 0;  // tracks across the region's boundary edges (the hops' capacity)
    std::string status;               // the region's own answer: unsat, cut or unknown
    std::vector<std::tuple<std::string, std::string, double>> families;  // (kind, status, seconds)
    std::string                                               adopted;
  };
  std::vector<Alt_trial> alt_trials;
  size_t                 sat_hops_over_share = 0;  // hops placed past hop_share (every boundary tile of their pair was at it)
  size_t                 global_nets = 0, global_pieces = 0;  // legroute.global: nets joined by L traces, their pass-through pieces
  size_t   global_unrouted = 0, global_full_edges = 0;        // global_route: nets without a path, edges filled to the share
  size_t   global_topology_disconnected = 0;                  // disconnected coarse capacity graph; not physical infeasibility
  size_t   crossings_built = 0, crossings_reused = 0;         // local retries reuse crossings when mesh and box homes are unchanged
  size_t   global_retained_parts = 0, sat_fixed_overflow = 0;
  bool     coarse_backbone_fixed = false;  // movable route-driven mode: exact backbone, fixed internal junctions
  bool     coarse_corridor_free  = false;  // cells and tile paths jointly chosen inside the admitted corridor
  size_t   corridor_components = 0, corridor_candidate_edges = 0, corridor_connectivity_cuts = 0;
  size_t   corridor_connectivity_rounds = 0, corridor_checked_components = 0, corridor_selected_edges = 0;
  size_t   corridor_seed_attempts = 0, corridor_seed_successes = 0, corridor_seed_solves = 0;
  size_t   corridor_seed_variables = 0, corridor_seed_clauses = 0;
  double   corridor_seed_seconds = 0;
  size_t   coarse_backbone_parts = 0, coarse_backbone_edges = 0, coarse_attachment_stubs = 0;
  size_t   coarse_attachment_choices = 0, coarse_attachments_changed = 0;
  bool     fit_matched_boxes   = false;  // local routed and cut from the fit-first matched box tiles
  size_t   fit_max_move        = 0;
  size_t   partition_oversized = 0, partition_max_cells = 0, partition_route_calls = 0, partition_rejected_repairs = 0;
  size_t   partition_indivisible_cells = 0;
  size_t   partition_timeout_rounds = 0, partition_timeout_regions = 0, sat_reused_regions = 0;
  size_t   partition_crossings = 0, partition_critical_crossings = 0;
  double   partition_seconds     = 0;
  size_t   global_overflow_edges = 0, global_overflow_tracks = 0;  // edges still over their share after the negotiation
  unsigned global_rounds         = 0;                              // negotiation rounds the router ran
  size_t   sat_fail_fast_skipped = 0;                              // fail_fast: regions left unsolved after the first failure
  double   global_seconds        = 0;
  double   global_astar_weight   = 1;
  size_t   global_search_pops = 0, global_search_limited = 0;  // cumulative work and exhausted attempts
  size_t   global_pattern_attempts = 0, global_pattern_completed = 0, global_pattern_fallbacks = 0;
  size_t   global_pattern_work = 0, global_pattern_work_limited = 0;
  size_t   sat_relaxed_tiles = 0, sat_deficit_tracks = 0;  // sat_relax_rounds: dropped tile bounds, tracks short
  size_t   sat_adjacent_nets = 0;                          // 1-1 nets between neighbor boxes: in-box metal, no lane
  // critical pre-placement: boxes fixed before the SAT, and their nets' weighted star length (tiles) at the placement
  // positions and at the chosen tiles
  size_t   crit_pinned = 0, crit_unpinned_regions = 0;
  double   crit_cost_before = 0, crit_cost_after = 0;
  // fixed 1:n trees: nets, boxes they fixed, nets skipped (no free tile), regions that dropped theirs (the other boxes
  // no longer fit), tree length (um) at the placement and fixed, the worst tile's tree tracks over its tracks
  size_t   tree_nets = 0, tree_boxes = 0, tree_skipped_nets = 0, tree_dropped_regions = 0, tree_tiles_over_half = 0;
  double   tree_um_before = 0, tree_um_after = 0, tree_max_load = 0;
  // fixed demand over a tile's tracks before the SAT (fixed boxes' in-box demand, straight connections between fixed
  // endpoints): trees dropped and critical boxes unpinned until no tile is over
  size_t   forced_trees_dropped = 0, forced_crit_unpinned = 0;
  // tree_load selection: nets skipped for load, summed capacity-clause estimates before and after the trees
  size_t   tree_load_skipped = 0, tree_clauses_before = 0, tree_clauses_after = 0;
  // io_preroute: feedthroughs and pin -> box -> pin pairs routed ahead, candidates without room, boxes freed off a track
  size_t   io_feedthroughs = 0, io_boxes = 0, io_skipped = 0, io_lifted = 0;
  // policies: regions that kept the second policy, regions both policies solved
  size_t   sat_alt_regions = 0, sat_both_regions = 0;
  // capacity_lazy: re-solves, bounds added because an answer overflowed them, bounds never needed
  size_t   lazy_rounds = 0, lazy_bounds = 0, lazy_skipped = 0;
  size_t   frozen_boxes        = 0;  // freeze: boxes pinned on their fallback tiles
  size_t   legal_boxes         = 0;  // legal_boxes: boxes built from the legal placement, fixed on their tiles
  size_t   legal_shuffled      = 0;  // legal_shuffle: legal boxes freed at the global router's overflowed edges
  size_t   mesh_joins          = 0;  // mesh_join: region pairs joined and resplit
  size_t   sat_seeded          = 0;  // sat_seed: regions whose answer is the fallback placement
  size_t   sat_skipped_regions = 0;  // sat_min_fill: regions taken as routable without a solve
  // timing tiers: constraints kept, dropped (UNSAT cores), tiers dropped whole (out of time), solves, and the
  // weighted route length (tiles) of the routable answers before and after the tiers
  size_t   tier_kept = 0, tier_dropped = 0, tier_timeouts = 0, tier_solves = 0;
  double   tier_cost_before = 0, tier_cost_after = 0;
  double   tier_crit_before = 0, tier_crit_after = 0;  // the tier nets' connection length (tiles) before and after
  // per region (grid region id): 0 routable, 1 UNSAT, 2 no answer in time, 3 skipped (sat_min_fill); its boxes and
  // free tiles. unroutable_components: the floorplan components of the regions with status 1 or 2
  struct Region_stat {
    int    status = 0;
    size_t boxes = 0, tiles = 0;
  };
  std::vector<Region_stat> region_stats;
  std::vector<size_t>      unroutable_components;
  std::vector<int>         unroutable_regions;  // per unroutable component: its region
  double                   sat_seconds = 0, sat_seconds_max = 0;
  size_t                   clock_domains = 0, clock_ribs = 0;
  // tiles (and tracks) where the built clock tree takes more than local reserved for it before SAT moved boxes
  size_t                   clock_excess_tiles = 0, clock_excess_tracks = 0;
  // legacy fixed-IO path: SAT rounds whose published boundary crossings exceeded the density policy
  // Final: each clock net's first planned-metal conflict per pass, in full (phase, offending shape, blocker, rule)
  std::vector<std::string> clock_conflicts;
  size_t                   crossing_over_policy          = 0;
  // after L5: (tile, budgeted layer) pairs where shared metal left fewer usable tracks than the cellbox budget
  size_t                   cellbox_allowance_short_tiles = 0, cellbox_allowance_short_tracks = 0;
  bool                     crossing_policy_invalid = false;  // then the result does not meet density_ceiling
  double                   wire_um = 0, critical_wire_um = 0;
  size_t                   vias          = 0;
  // legroute.final routing (the shared physical router). All or nothing: signal and clock jobs must all connect;
  // otherwise neither signal nor clock wires are written. Clock connectivity does not imply CTS or skew closure.
  bool                     wires_emitted = false;
  size_t                   route_passes  = 0;  // the routing pass whose result is kept
  size_t                   routed_nets = 0, local_nets = 0, joins = 0, taps = 0, connected_nets = 0, unchecked_nets = 0;
  // Clock counters are subsets of the corresponding total net counts, including bounded-prefix unchecked jobs.
  size_t                   clock_required_nets = 0, clock_connected_nets = 0, clock_failed_nets = 0, clock_unchecked_nets = 0;
  // global_conflict_nets and tree_fallbacks were routed whole by the router; failed_nets did not close at all
  size_t                   failed_nets = 0, global_conflict_nets = 0, join_failures = 0, tap_failures = 0, tree_fallbacks = 0;
  // Candidate events across all detailed-route passes, including discarded
  // passes. Search avoidance is distinct from the two transactional backstops.
  size_t                   final_prior_cut_rejections = 0, final_pending_cut_rejections = 0, final_union_cut_rejections = 0;
  size_t                   final_precommit_disconnected = 0;
  size_t                   final_skipped_nets
      = 0;  // final_skip_clock / final_skip_nets diagnostic  // opt-in final_precommit_connectivity rejections
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
// Necessary pre-SAT admission checks. These are planning feedback, not a proof of physical unroutability.
// Cell density uses physical movable CORE area / free placement-site area after fixed bodies and blockages.
// Routing density uses routed edge demand / raw directional track capacity; the admission limit is density_ceiling.
struct Legroute_gate_report {
  double density_limit = .90;  // the ceiling these checks used (Legroute_options::density_ceiling)
  enum class Stage { primary, secondary };
  Stage stage = Stage::primary;
  struct Region {
    int    region           = -1;
    size_t cells            = 0;
    size_t unfittable_boxes = 0;
    double cell_area_dbu2 = 0, placeable_area_dbu2 = 0, cell_density = 0;
  };
  double                      max_cell_density = 0, max_routing_density = 0;
  size_t                      region_target = 0, oversized_regions = 0, cell_dense_regions = 0, routing_dense_edges = 0;
  size_t                      overflow_edges = 0, overflow_tracks = 0;
  size_t                      unfittable_regions = 0, unfittable_boxes = 0;
  size_t                      unrouted = 0, search_limited = 0, topology_disconnected = 0;
  std::vector<Region>         regions;
  std::vector<size_t>         affected_components;  // floorplan component indices, including failed-net movable endpoints
  std::vector<physical::Rect> hotspots;  // exact union of affected tile footprints, compressed into horizontal runs (DBU)
  bool                        requires_replan() const {
    return oversized_regions || cell_dense_regions || routing_dense_edges || overflow_tracks || unrouted || unfittable_regions;
  }
};
std::string legroute_gate_json(const Legroute_gate_report&);
struct Legroute_replan : std::runtime_error {
  Legroute_gate_report gates;
  explicit Legroute_replan(Legroute_gate_report report);
};

// Boxes do not fit the free tiles, or complete all-signal paths overload the tracks (including an overloaded-edge
// component above the partition target). A caller with a generated core may replan/grow and start over;
// this is a failed routing attempt, not proof of physical infeasibility.
struct Legroute_unfit : std::runtime_error {
  using std::runtime_error::runtime_error;
};
// Legal placement of every movable single-row cell, routed signal and clock wires on floorplan nets (with `final`).
// Multi-row cells must be fixed (anchored) by the caller. Throws std::invalid_argument on unsupported frames and
// std::runtime_error when the boxes cannot fit the free tiles; the floorplan is unchanged on any throw.
Legroute_report legroute(const physical::Technology&, physical::Floorplan&, const timing::Feedback&, const Legroute_options&,
                         const Legroute_regions& = {}, const physical::Tech_profile* = nullptr);
void            write_legroute_report(const Legroute_report&, const std::string& path);

// Planning before region cuts. Endpoints are physical pins except for movable
// route-driven cuts, which share local's connectivity-box centroids. The explicit
// boxed_handoff diagnostic scores legal-box centroids. box_route_seed can replace
// these endpoints with matched provisional box tiles. endpoint_basis reports
// this distinction; none of these modes certifies detailed routability.
struct Legroute_plan_report {
  bool box_route_seed_attempted = false, box_route_seed_accepted = false;
  enum class Route_skip_reason { none, physical_density, window_density, paired_pin_supply, paired_pin_grouping };
  Legroute_gate_report gates;
  std::string          endpoint_basis    = "physical_pins";
  // A whole-core physical-density rejection precedes route construction. Its
  // zero failure/overflow counters are unmeasured, not a zero-overflow witness.
  bool                 route_attempted   = false;
  Route_skip_reason    route_skip_reason = Route_skip_reason::none;
  size_t               unchecked_signals = 0;
  // Window fit precondition (fit_density): windows checked, windows over the cap, the densest window.
  size_t               fit_windows = 0, fit_dense_windows = 0;
  double               fit_max_density = 0;
  // With boxes: whole-core joint box-to-tile matching near the box centres (local's own fit model). Far boxes
  // moved beyond window_tiles; the fit holds when matched, at most 1% far and none beyond twice the window.
  bool                 fit_matched     = false;
  bool                 fit_applied     = false;  // the route and cuts used the matched tiles as box positions
  size_t               fit_far_boxes = 0, fit_max_move = 0;
  std::string          fit_reason;
  // no POWER/GROUND special-net geometry in the floorplan: the routing capacities carry no power budget, so a
  // passing route is optimistic for a design that will get a power grid
  bool                 pg_missing = false;
  size_t      fit_movable_boxes = 0, fit_free_tiles = 0;  // count | budget | domains | far (see lr::Fit_match), empty when it fits
  const char* route_skip_name() const {
    return route_skip_reason == Route_skip_reason::physical_density      ? "physical_density"
           : route_skip_reason == Route_skip_reason::window_density      ? "window_density"
           : route_skip_reason == Route_skip_reason::paired_pin_supply   ? "paired_pin_supply"
           : route_skip_reason == Route_skip_reason::paired_pin_grouping ? "paired_pin_grouping"
                                                                         : "none";
  }
  struct Failure {
    size_t      net_index = 0;  // original floorplan net index, not request order
    std::string net_name;
    size_t      pin_count = 0, endpoint_tiles = 0;
    int         bbox_width_tiles = 0, bbox_height_tiles = 0;
    bool        search_limited        = false;
    bool        topology_disconnected = false;
    size_t      last_attempt_pops = 0, search_pops = 0, search_attempts = 0;
  };
  std::vector<Failure>  failures;            // unresolved signals only; complete but overflowing trees are counted separately
  std::vector<double>   component_pressure;  // floorplan component order; >= 1
  // Manhattan distance in routing tiles to a hot tile; UINT_MAX for fixed cells
  // or no hotspot. Transit congestion, not a net's entire bounding box, seeds it.
  std::vector<unsigned> component_hot_distance;
  int64_t               tile_width_dbu = 0, tile_height_dbu = 0;
  size_t                hot_tiles = 0;
  size_t                signals = 0, timed_signals = 0, unrouted = 0, overflow_edges = 0, overflow_tracks = 0;
  size_t                search_pops = 0, search_limited = 0;
  size_t                pattern_attempts = 0, pattern_completed = 0, pattern_fallbacks = 0;
  size_t                pattern_work = 0, pattern_work_limited = 0;
  size_t                topology_disconnected = 0;
  double                global_astar_weight   = 1;
  double                seconds               = 0;
};
// Coarse edge utilization projected onto incident tiles. X-major (x*rows+y),
// relative to the admitted residual capacity, including PG/clock reservations.
// Missing-route estimates are separate from routed demand; neither is a DRC label.
struct Legroute_congestion {
  physical::Rect      bounds;
  int                 cols = 0, rows = 0;
  double              admission_share = .90;
  std::vector<double> horizontal, vertical, pin_utilization;
  // Resampled diagnostics: area-weighted tile means; horizontal/vertical retain
  // the peak of every overlapping tile so a thin saturated channel stays hot.
  std::vector<double> mean_horizontal, mean_vertical;
  std::vector<double> unresolved_horizontal, unresolved_vertical;
  // Canonical positive-direction edges; terminal boundary entries are zero.
  // Present only on the original tile grid, not on resampled maps.
  std::vector<size_t> demand_h, demand_v;
  std::vector<int>    limit_h, limit_v;
  size_t              signals = 0, unrouted = 0, search_limited = 0;
};
Legroute_congestion  resample_legroute_congestion(const Legroute_congestion&, const physical::Rect&, int cols, int rows);
// Ordinary admission may reject whole-core physical density before routing;
// route_attempted then stays false and a requested congestion map is cleared.
// measure_only performs routing even on an inadmissible pose for actual feedback
// measurements, and does not select or test proposed region cuts.
Legroute_plan_report legroute_plan(const physical::Technology&, physical::Floorplan&, const timing::Feedback&,
                                   const Legroute_options&, const Legroute_regions& = {}, const physical::Tech_profile* = nullptr,
                                   size_t net_pops = 100000, bool boxed_handoff = false, Legroute_congestion* congestion = nullptr,
                                   bool measure_only = false);
void                 write_legroute_plan_failures(const Legroute_plan_report&, const std::string& path);

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
