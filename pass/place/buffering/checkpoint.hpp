// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>

#include "density.hpp"
#include "gap_forces.hpp"
#include "pass/place/forces/pin_weights.hpp"

namespace livehd::placement::buffering {
struct Checkpoint_state;
struct Checkpoint_input {
  const DreamPlace::native::Design&                       design;
  const late::Result&                                     timing;
  const timeclosure::Pin_weights&                         weights;
  const physical::Parasitics&                             parasitics;
  const std::map<std::string, std::pair<double, double>>& locations;
  int                                                     iteration = 0;
  const Checkpoint_state*                                 previous  = nullptr;
};
struct Checkpoint_problem {
  // Current legal inventory (fixed-only virtual inventory before phase 7).
  Legal_sites                     sites;
  std::vector<Allocation_net>     nets;
  std::map<std::string, uint64_t> capacities;
  std::map<std::string, int>      materialized;
  std::set<std::string>           retired;
  uint64_t                        geometry_epoch = 0;
};
struct Checkpoint_demand {
  bool                        supported_api = false;
  std::vector<Allocation_net> nets;
  size_t                      critical_nets = 0, window_nets = 0, outside_nets = 0, missing_contexts = 0;
  size_t                      omitted_nets = 0;
};
// Discover actual critical/window RC owners (plus previously managed owners),
// ordered by class, fresh LR severity and identity. Bounded selection retains
// missing contexts explicitly. Source arcs must share one same-instance input
// predecessor for both transitions; IO/clock-launch/related-output contexts that
// cannot satisfy the common evaluator are unsupported, never guessed.
Checkpoint_demand discover_checkpoint_demand(const Checkpoint_input&, const late::Driver_options&, double native_units_per_um,
                                             size_t max_nets);
struct Checkpoint_settings {
  int                                                        start               = 200;
  double                                                     native_units_per_um = 1000, site_dbu_per_um = 1000;
  Allocation_settings                                        allocation;
  // Native in-memory geometry/alternative producer. No external timer, graph
  // mutation or I/O in this synchronous hot-loop callback. Source arrival/slew,
  // sink required times, LR mass and net class are refreshed below, never
  // trusted from saved contexts. Alternatives must use current geometry.
  std::function<Checkpoint_problem(const Checkpoint_input&)> generate;
  void                                                       validate() const;
};
struct Checkpoint_state {
  Allocation_state             allocation;
  Density_reservation_state    density;
  std::vector<Gap_entry_pull>  pulls;
  timeclosure::Objective_epoch epoch;
  int                          iteration = -1;
  explicit Checkpoint_state(Site_reservations sites) : allocation(std::move(sites)) {}
};
struct Checkpoint_result {
  bool                             supported_api = false;
  std::optional<Checkpoint_state>  proposal;
  std::optional<Allocation_result> negotiation;
  Gap_force_result                 forces;
  Density_binding_result           density;
  size_t                           critical_nets = 0, window_nets = 0, outside_nets = 0, missing_contexts = 0;
};
// Prepares one atomic timing/gap/density checkpoint, with shared ownership and
// fresh native LR/window/source state. Old unmet/unmanaged pulls stay paired with
// their protected density reservations; only replaced/retired owners release.
// Geometry changes must preserve protected slots or fail before mutation.
// The caller commits proposal only after this returns AND timing policy commits.
// This does not materialize buffers or certify virtual whole-graph timing.
Checkpoint_result prepare_checkpoint(const Checkpoint_input&, const Checkpoint_problem&, const std::optional<Checkpoint_state>&,
                                     const liberty::timing::Library&, const Gap_force_payload&,
                                     const std::map<std::string, int>& physical_drivers, const late::Driver_options&,
                                     const Checkpoint_settings&);
}  // namespace livehd::placement::buffering
