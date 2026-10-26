// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>

#include "inou/lefdef/pin_access.hpp"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_occupancy.hpp"
#include "signal_route_profile.hpp"

namespace livehd::placement {
class Pin_escape_guard;
struct Signal_route_limits {
  size_t                                access_candidates = 0, patterns = 0;
  std::chrono::steady_clock::time_point deadline;
};
struct Signal_route_geometry {
  std::optional<physical::Shape> straight_corridor;
  std::map<std::string, double>  layer_wire_length_um;  // Explicit wire spans, excluding via pads.

  std::vector<physical::Route_path>                     paths;
  std::vector<physical::Pin_access_point>               endpoints;  // connection order
  // Conservative geometric proxy, not extracted coupling capacitance.
  double                                                parallel_exposure = 0;
  double                                                wire_length_um    = 0;
  size_t                                                vias              = 0;
  std::map<std::string, std::map<std::string, int64_t>> nondefault_rules;
};
// Install proposal rules only in a trial frame or after all acceptance gates.
// Conflicting definitions throw before changing the frame.
void apply_signal_route_rules(physical::Floorplan&, const Signal_route_geometry&);
struct Signal_route_trial {
  std::optional<Signal_route_geometry> geometry;
  size_t                               access_examined = 0, patterns_examined = 0;
  bool                                 timed_out = false;
  std::string                          reason;
};
// One bounded, non-mutating net request. A corridor restricts the request to
// an aligned straight trunk; compare_layers adds same-center alternatives.
struct Signal_route_request {
  Signal_route_limits            limits;
  std::optional<physical::Shape> corridor;
  bool                           compare_layers        = false;
  bool                           straight_span         = false;  // discover a common access track, never a pattern/maze
  // Sum of parallel overlap / edge separation within two route widths.
  // Applies to same-layer foreign metal; a cheap conservative coupling screen.
  double                         max_parallel_exposure = 100;
  // Relax only the coupling screen, never DRC, access or downstream timing gates.
  bool                           try_hard              = false;
};
struct Signal_route_proposals {
  std::vector<Signal_route_geometry> candidates;  // baseline first; not timing-ranked
  size_t                             access_examined = 0, patterns_examined = 0;
  bool                               timed_out = false;
  std::string                        reason;
};
// Non-mutating existence query. A negative result means no witness was found
// within the supplied budget, not a proof that moving a cell cannot help.
struct Signal_route_probe {
  bool                           routable = false, timed_out = false, budget_exhausted = false;
  size_t                         access_examined = 0, patterns_examined = 0;
  std::optional<physical::Shape> corridor;
  std::string                    reason;
};
// Geometry proposal stage, not a pre-route acceptance or emission API. The
// caller still owns reset/tie/dont_touch eligibility, timing, capacity, final
// complete-net checks and locking. Neither frame nor occupancy is changed by
// a trial. Layers come from the profile (Route_policy): escape-layer spans join
// any number of pins on a common track; multi-pin escape/branch patterns connect
// separate tracks through a checked vertical trunk, merging branches and sharing
// trunk vias for pins on the same escape track. A bounded maze over the signal
// layers follows pattern search. Multi-pin nets use a FLUTE
// topology with snapped junctions and shared branch geometry. Timing/capacity
// acceptance remains a separate stage.
class Signal_router {
  const physical::Technology&                   tech_;
  const Signal_route_profile                    profile_;
  const physical::Floorplan&                    frame_;
  const physical::Routing_occupancy&            occupancy_;
  std::unique_ptr<physical::Signal_metal_rules> owned_rules_;
  const physical::Signal_metal_rules&           rules_;
  physical::Route_components                    components_;
  const Pin_escape_guard*                       pin_escapes_ = nullptr;

  Signal_route_trial propose_span_impl(const physical::Net&, const Signal_route_limits&, const physical::Shape*,
                                       const Pin_escape_guard* = nullptr) const;
  // One search stage's result. Past the deadline, expired() marks it timed
  // out with the "route deadline" reason; fail() records any other reason.
  struct Stage_trial {
    Signal_route_trial&                   out;
    std::chrono::steady_clock::time_point deadline;
    bool                                  expired() const;
    Signal_route_trial                    fail(std::string reason) const;
  };
  // Shared proposal plumbing. Access enumeration charges the trial's budget.
  physical::Pin_access_result  endpoint_access(const physical::Component&, const std::string& pin, const physical::Net&,
                                               const Signal_route_limits&, Signal_route_trial&) const;
  std::vector<physical::Shape> proposal_shapes(const physical::Net&, const std::vector<physical::Route_path>&,
                                               const physical::Route_width_rules* = nullptr) const;
  physical::Route_connectivity connectivity(const physical::Net&, const std::vector<physical::Route_path>&,
                                            const physical::Route_width_rules* = nullptr) const;
  // Appends a flush centerline path covering metal and accounts its length.
  void add_wire(Signal_route_geometry&, const physical::Shape& metal, bool horizontal, std::string taper_rule = {}) const;

public:
  // Profile is copied once; the immutable technology/frame and occupancy
  // must outlive the router. Only the host may commit returned geometry.
  // Repeated routing should borrow the host's current escape guard. Without one,
  // try_route builds a temporary guard against current occupancy within its budget.
  Signal_router(const physical::Technology&, const physical::Floorplan&, const physical::Routing_occupancy&, Signal_route_profile,
                const Pin_escape_guard* = nullptr);
  // Borrow a compiled oracle for repeated candidate checks; it must outlive this router.
  Signal_router(const physical::Technology&, const physical::Floorplan&, const physical::Routing_occupancy&,
                const physical::Signal_metal_rules&, Signal_route_profile, const Pin_escape_guard* = nullptr);
  Signal_route_proposals try_route(const physical::Net&, const Signal_route_request&) const;
  // Returns a witness using the requested layer alternatives, without timing ranking.
  Signal_route_probe     probe_route(const physical::Net&, const Signal_route_request&) const;
  // Shares access, work and deadline budgets across all search stages.
  // Stay inside the retained corridor. An escape-layer witness may use legal
  // pin-access vias while retaining one horizontal signal trunk and no detour.
  Signal_route_trial propose_aligned(const physical::Net&, const physical::Shape& corridor, const Signal_route_limits&) const;
  // Lift a checked alignment without moving its endpoint centers. One
  // same-direction trunk plus local endpoint via landings; never a detour.
  Signal_route_trial propose_aligned_layer(const physical::Net&, const Signal_route_geometry&, const std::string& layer,
                                           const Signal_route_limits&) const;
  Signal_route_trial propose_straight(const physical::Net&, const physical::Shape& corridor, const Signal_route_limits&) const;
  Signal_route_trial propose(const physical::Net&, const Signal_route_limits&) const;
  Signal_route_trial propose_pattern(const physical::Net&, const Signal_route_limits&) const;
  Signal_route_trial propose_maze(const physical::Net&, const Signal_route_limits&) const;
  Signal_route_trial propose_span(const physical::Net&, const Signal_route_limits&) const;
};
}  // namespace livehd::placement
