// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "core/timing_feedback.hpp"
#include "inou/lefdef/physical.hpp"
#include "inou/lefdef/tech_profile.hpp"

namespace livehd::placement {
// legroute: the routability step at the end of placement (todo/livehd/2r-route.html). What exists today is its
// minimal core: a tile grid with per-layer track capacity, a clock charge, a negotiated global router with optional
// FastRoute pattern seeds, and congestion maps. legroute_plan() routes one placement on that grid and reports what
// does not fit; the caller (place.cpp) alternates it with cell inflation and resumed global placement, keeps the best
// measured round and hands the result to the regular terminal legalizer. Nothing here moves or legalizes a cell.
struct Legroute_options {
  // The share of an edge's tracks (the narrower tile's, per direction) the routed trees may take.
  double   capacity_share      = 1;
  // Tracks per routing layer and tile kept out of the capacity. 0 for planning; the place.area=auto oracle keeps
  // the value it was calibrated with (area_fit.cpp).
  unsigned reserved_tracks     = 0;
  // The clock budget: a spine track per tile, a rib track every rib_tiles tile rows, and the estimated spine tree
  // of each clock net where it takes more.
  bool     clock_charge        = true;
  unsigned rib_tiles           = 4;
  double   crit_window_ps      = 200;  // nets this close to WNS route first (criticality 1 at WNS, 0 at WNS + window)
  // Opt-in bounded FastRoute-derived L/Z seed for <=32 endpoints; failed patterns fall back to the native maze.
  bool     global_pattern      = false;
  unsigned global_margin       = 8;  // tiles around a net's bounding box the path search covers (grown when no path fits)
  // negotiation rounds of the router: the nets on an edge over its share reroute under a present-congestion cost
  // that doubles each round plus the edge's overuse history (PathFinder / FastRoute); 0: one pass, a full edge closed
  unsigned global_rounds       = 16;
  // Opt-in weighted A*: 1 preserves shortest-path ordering; >1 favors the goal over path cost. Large-fanout nets
  // retain Dijkstra. This changes no capacity or connectivity check.
  double   global_astar_weight = 1;
  int64_t  macro_halo_x_dbu = 0, macro_halo_y_dbu = 0;
  // Route on the pin-escape layer too (a profile's access.escape, e.g. ASAP7 M2), as FastRoute does from
  // MIN_ROUTING_LAYER: its tracks less the power rails count toward capacity. false keeps it out of the stack
  // (the place.area=auto oracle's calibration).
  bool     route_escape     = false;
};
void validate_legroute_options(const Legroute_options&);

// Metal that exists later but not yet in the floorplan (DBU): the planned power grid. Its shapes (rails, stripes and
// via landings) take the tracks they cover; the nets and vias are kept for the snapshot of a measured pose.
struct Legroute_power {
  std::vector<physical::Shape> obstacles;
  std::vector<physical::Net>   power_nets;
  std::vector<physical::Via>   power_vias;
  // the obstacles are the technology profile's planned power grid (its rails and stripes, no vias or pins): a
  // planning-only budget for a flow that adds the PDN later (ORFS pdngen), not geometry that is exported
  bool                         planned = false;
};

// One global route of a placement on the tile grid. Endpoints are the physical pins of the cells where they are
// (legal or not). Overflow is demand over the admitted capacity of a tile edge; it is this router's estimate, not a
// detailed-routing verdict.
struct Legroute_plan_report {
  // no POWER/GROUND special-net geometry in the floorplan: the routing capacities carry no power budget, so a
  // passing route is optimistic for a design that will get a power grid
  bool pg_missing = false;
  bool pg_planned = false;  // the power budget is the profile's planned grid (Legroute_power::planned)
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
  int                   cols = 0, rows = 0;
  size_t                hot_tiles = 0;
  size_t                signals = 0, timed_signals = 0, unrouted = 0, overflow_edges = 0, overflow_tracks = 0;
  // The router is 2D: demand exists per direction, not per layer. overflow_edges/overflow_tracks are the sums.
  size_t                overflow_edges_h = 0, overflow_edges_v = 0, overflow_tracks_h = 0, overflow_tracks_v = 0;
  // Capacity is kept per layer (bottom-up, the routed stack): the tile tracks each layer gives the router after
  // power, macros and blockages, before the clock charge and capacity_share.
  struct Layer {
    std::string name;
    bool        horizontal = true;
    size_t      capacity_tracks = 0;
  };
  std::vector<Layer> layers;
  size_t             search_pops = 0, search_limited = 0;
  size_t             pattern_attempts = 0, pattern_completed = 0, pattern_fallbacks = 0;
  size_t             pattern_work = 0, pattern_work_limited = 0;
  size_t             topology_disconnected = 0;
  double             seconds               = 0;
  // What the rounds rank a placement by: unresolved nets, overflowing tracks and overflowing edges. 0: the route fits.
  size_t             residual() const { return unrouted + overflow_tracks + overflow_edges; }
};
// Coarse edge utilization projected onto incident tiles. X-major (x*rows+y),
// relative to the admitted residual capacity, including PG/clock reservations.
// Missing-route estimates are separate from routed demand; neither is a DRC label.
struct Legroute_congestion {
  physical::Rect      bounds;
  int                 cols = 0, rows = 0;
  double              admission_share = 1;
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
// net_pops: queue pops per net routing attempt, across window growth (0: unbounded). Exhaustion is an unresolved
// search, never proof that capacity or area is insufficient. Throws std::invalid_argument on unsupported frames
// (no rows, one row site, a routing stack without both directions). The floorplan is not modified.
Legroute_plan_report legroute_plan(const physical::Technology&, physical::Floorplan&, const timing::Feedback&,
                                   const Legroute_options&, const Legroute_power& = {}, const physical::Tech_profile* = nullptr,
                                   size_t net_pops = 100000, Legroute_congestion* congestion = nullptr, std::ostream* tree_report = nullptr);
void                 write_legroute_plan_failures(const Legroute_plan_report&, const std::string& path);
// The routing part of a plan report as one JSON object (no trailing newline): counts, overflow per direction and
// the per-layer capacity.
std::string          legroute_plan_json(const Legroute_plan_report&);

// Macro tile snap: the origin of a macro (with its halo) rounded to the tile grid of the design's widest flop
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
