// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>
#include "inou/lefdef/routing_occupancy.hpp"
#include "inou/lefdef/routing_rules.hpp"

namespace livehd::placement {
struct Route_capacity_grid {
  physical::Rect           bounds;
  size_t                   bins_x = 0, bins_y = 0;
  std::vector<std::string> layers;
  double                   max_preroute_fraction    = .30;
  // For independently DRC-checked direct pin connections: a track already
  // unavailable due to pin/obstruction metal cannot lose further capacity.
  // Does not grant capacity or waive geometry checks. General routing retains
  // its stricter rejection policy by default.
  bool                     count_only_usable_tracks = false;
  friend bool operator==(const Route_capacity_grid&, const Route_capacity_grid&) = default;
};
// A fresh RUDY/RSMT snapshot of the remaining nets, excluding the trial net.
// Same bounds/bin counts as Route_capacity_grid, x-major indexing. Capacity is
// the track density used by DREAMPlace (tracks per DEF DBU). Enhanced RUDY's
// free-component normalization is retained: using full bin area to recover
// demand is conservative, never credits disconnected macro pockets.
struct Remaining_route_demand {
  std::vector<double> horizontal, vertical;
  double              horizontal_capacity = 0, vertical_capacity = 0;
  std::vector<double> pin_utilization;
  int                 blocked_pins = 0, unroutable_nets = 0;
};
struct Route_capacity_cell {
  std::string layer;
  size_t      x = 0, y = 0, usable_tracks = 0, reserved_tracks = 0;
  friend bool operator==(const Route_capacity_cell&, const Route_capacity_cell&) = default;
};
class Route_capacity;
struct Route_capacity_trial {
  bool        legal = false;
  std::string reason;
  // Specific bin/layer and track counts; reason remains a stable summary key.
  std::string detail;

private:
  friend class Route_capacity;
  const Route_capacity*                  owner      = nullptr;
  size_t                                 epoch      = 0;
  bool                                   approved   = false;
  bool                                   prechecked = false;
  std::vector<std::pair<size_t, size_t>> additions;
};
// Conservative per-layer capacity: a track obstructed anywhere in a bin is
// unavailable in that bin. Shapes reserve every usable track whose ordinary
// wire footprint would violate spacing against that shape. Partial lengths
// consume one whole track, and overlapping footprints count once.
class Route_capacity {
  friend class Route_capacity_seed;
  Route_capacity(const Route_capacity&) = default;
  void add_obstructions(const physical::Routing_occupancy&, const std::function<void()>&);
  struct Track {
    physical::Shape footprint;
    bool            usable = true, reserved = false;
  };
  struct Cell {
    Route_capacity_cell report;
    physical::Rect      bounds;
    bool                horizontal;
    std::vector<Track>  tracks;
  };
  struct Layer_index {
    size_t start;
    double reach;
  };
  std::map<std::string, Layer_index> layers_;
  Route_capacity_grid                grid_;
  physical::Signal_metal_rules       rules_;
  std::vector<Cell>                  cells_;
  std::vector<double>                total_h_, total_v_;
  std::set<std::string>              ignored_layers_;
  size_t                             epoch_ = 0;
  Route_capacity_trial               check_impl(const std::vector<physical::Shape>&, const Remaining_route_demand&, bool existing,
                                                const Route_capacity_trial* prechecked = nullptr, bool demand_only = false) const;

public:
  Route_capacity(const physical::Technology&, const physical::Floorplan&, const physical::Routing_occupancy&, Route_capacity_grid,
                 const std::function<void()>& checkpoint = {});
  Route_capacity&                  operator=(const Route_capacity&) = delete;
  Route_capacity_trial             check(const std::vector<physical::Shape>&, const Remaining_route_demand&) const;
  // Cheap necessary check before computing remaining-net demand. A passing
  // trial is NOT approved for commit; callers must still check remaining demand.
  Route_capacity_trial             precheck(const std::vector<physical::Shape>&) const;
  // Necessary check with no additional wire reservations. A passing result is
  // not committable and cannot serve as a geometry precheck.
  Route_capacity_trial             precheck_demand(const Remaining_route_demand&) const;
  // Reuse the footprint of a successful precheck at the same reservation epoch.
  // Rechecks all remaining-demand/headroom gates; rejects forged/stale/foreign input.
  Route_capacity_trial             check_remaining(const Route_capacity_trial&, const Remaining_route_demand&) const;
  // Restore already realized signal wiring after building against static
  // obstacles ONLY (pins, macros, PG), excluding these signal wires. Counts
  // their union toward subsequent headroom checks. Fails atomically if their
  // geometry cannot be represented; this is not an independent DRC check.
  void                             reserve_existing(const std::vector<physical::Shape>&);
  // Call only after all other acceptance gates pass. Rejects stale/foreign
  // trials; failed checks and rejected commits never change reservations.
  void                             commit(const Route_capacity_trial&);
  std::vector<Route_capacity_cell> report() const;
};
// Immutable static-capacity snapshot for additive repeater proposals. Technology
// must outlive the seed and remain unchanged. Signal reservations are never
// cached: each returned trial must restore its current FIXED wiring separately.
class Route_capacity_seed {
  const physical::Technology* technology_;
  physical::Floorplan signature_;
  std::unique_ptr<Route_capacity> capacity_;

public:
  Route_capacity_seed(const physical::Technology&, const physical::Floorplan&, Route_capacity_grid,
                      const physical::Routing_power_grid* = nullptr, const std::function<void()>& checkpoint = {});
  // Returns null on changed existing geometry/configuration. Added components
  // are decoded through the ordinary occupancy builder and block fresh tracks.
  // Returned capacities have independent ownership, epochs and reservations.
  std::unique_ptr<Route_capacity> instantiate(const physical::Technology&, const physical::Floorplan&,
                                               const Route_capacity_grid&,
                                               const std::function<void()>& checkpoint = {}) const;
};
}  // namespace livehd::placement
