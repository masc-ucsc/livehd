// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "power_grid.hpp"
#include "preroute_demand.hpp"
#include "preroute_design.hpp"
#include "preroute_timing.hpp"
#include "routed_rc.hpp"
#include "signal_router.hpp"

namespace livehd::placement {
// Host supplies exact physical/netlist/RC correspondence and logical exclusions;
// names are never used to guess clocks, resets, constants or dont_touch cones.
struct Preroute_binding {
  size_t physical_net = 0, demand_net = 0, rc_net = 0;
  bool   clock = false, reset = false, tie = false, dont_touch = false;
};
// Join the final DEF, RC and demand views by exact endpoint identity. DEF net
// names and array ordering may differ. Singleton physical nets have no RC and
// are omitted. All other missing/ambiguous correspondence is an error.
// dont_touch_pins must be expanded by the host from its protected logical cones.
// Snapshot-owned exclusions always apply; the argument adds exclusions and
// cannot override or clear the snapshot's protections.
// Every protected label must exist in the final demand snapshot; stale or empty
// labels are errors, never silently discarded exclusions.
std::vector<Preroute_binding> bind_preroute(const physical::Floorplan&, const physical::Parasitics&, const Preroute_design&,
                                            const std::set<std::string>& dont_touch_pins = {});
struct Preroute_options {
  // Technology profile name, or "auto" to detect it from the technology passed in.
  std::string                                         tech_profile = "auto";
  // Internal stage reuse only; the seed validates unchanged supply geometry.
  std::shared_ptr<const physical::Routing_power_grid> power_grid_occupancy;
  std::shared_ptr<const Route_capacity_seed>          capacity_seed;
  Route_capacity_grid                                 capacity;
  DreamPlace::native::RudyOptions                     demand;
  Preroute_timing_limits                              timing;
  std::chrono::milliseconds                           per_net_budget{10};
  size_t                                              access_candidates = 10000, patterns = 10000;
  int                                                 max_rows = 0, max_sites = 0;
  // Experimental signal-first mode: input has no PG special nets/terminals.
  // Each accepted route must admit a rebuilt connected grid and pass capacity
  // against that grid. Followpin rails remain obstacles during signal search.
  std::optional<Pg_profile>                           deferred_pg;
  bool                                                straight_only     = false;
  // Worst setup-slack fraction of eligible aligned nets; zero disables lifting.
  double                                              critical_fraction = .10;
  std::map<std::string, physical::Shape>              straight_routes;
};
// Optional original-net pin-span caps in placement row/site units. Empty means allowed.
std::string preroute_span_exclusion(const DreamPlace::native::Design&, size_t net, const Preroute_options&);
struct Preroute_record {
  std::string                          net, reason;
  bool                                 accepted = false;
  size_t                               vias = 0, patterns = 0;
  double                               wire_length_um        = 0;
  size_t                               pin_count             = 0;
  double                               estimated_length_um   = 0;
  bool                                 attempted             = false;
  bool                                 critical_layer_search = false;
  size_t                               layer_candidates      = 0;
  std::map<std::string, double>        layer_wire_length_um;
  std::map<std::string, double>        timings_ms;
  std::string                          capacity_detail;
  // Snapshot at this net's acceptance, not final-stage or extracted timing.
  // Scratch/report data only; never persisted as a graph attribute.
  std::optional<Preroute_timing_trial> timing_at_acceptance;
};
// Diagnostic record of one bound net before, or instead of, a routing attempt.
Preroute_record preroute_record(const physical::Floorplan&, const physical::Parasitics&, const Preroute_binding&,
                                std::string reason);
struct Preroute_result {
  physical::Floorplan           frame;
  physical::Parasitics          parasitics;
  std::vector<Preroute_record>  records;
  // Diagnostic wall times; the stage deadline still includes setup.
  std::map<std::string, double> timings_ms;
};
// Stage-level transaction: input frame/parasitics/design are never modified.
// Temporarily installs accepted RC for subsequent timing checks, protects every
// accepted pin, and restores the caller's timer mark before return or exception.
// Only the returned result contains FIXED routes and endpoint status changes.
// Caller must publish the result as one placement/cache transaction and then
// evaluate its RC in the physical session. Changed RC clears its old identity;
// the host must assign the final cache identity. A finite stage deadline is required.
// Search uses the technology profile's escape-layer spans and multi-pin
// escape/branch patterns, followed by bounded maze routing on its signal layers
// over snapped FLUTE branches. Wires follow each layer's
// preferred direction; turns require vias. Unsupported/unroutable candidates
// remain unwired.
Preroute_result preroute(const physical::Technology&, const physical::Floorplan&, const physical::Parasitics&,
                         const Preroute_design&, const std::vector<Preroute_binding>&, const Route_technology_rc&, timing::Whatif&,
                         const Preroute_options&);
// Route every branch of one privately proposed repeater tree as a transaction.
// The caller owns the draft netlist, legalized repeater sites and matching
// timing/demand snapshot, and publishes them only if result is present. Failure
// returns diagnostics but no partially fixed frame, reservations, or RC edits.
struct Preroute_tree_trial {
  std::optional<Preroute_result> result;
  std::vector<Preroute_record>   rejected_records;
  std::string                    reason;
  std::string                    capacity_detail;
  // Route setup/search times survive rollback, including setup-deadline failures.
  std::map<std::string, double>  timings_ms;
};
Preroute_tree_trial preroute_tree(const physical::Technology&, const physical::Floorplan&, const physical::Parasitics&,
                                  const Preroute_design&, const std::vector<Preroute_binding>& branches, const Route_technology_rc&,
                                  timing::Whatif&, const Preroute_options&);
// The frame/RC/demand describe a private legally placed repeater proposal, but
// timer still describes the original unsplit net. Install temporary buffers,
// route the whole tree, validate actual routed RC against the original state,
// and roll back timing on every exit. Host publishes the matching private graph
// only when result is present; existing cells must not have moved in the draft.
Preroute_tree_trial preroute_repeated_tree(const physical::Technology&, const physical::Floorplan&, const physical::Parasitics&,
                                           const Preroute_design&, const std::vector<Preroute_binding>& branches,
                                           const Route_technology_rc&, timing::Whatif&, const Preroute_options&,
                                           const physical::Rc_net&         original, const std::vector<Preroute_repeater>&,
                                           const std::vector<std::string>& locked_pins);
}  // namespace livehd::placement
