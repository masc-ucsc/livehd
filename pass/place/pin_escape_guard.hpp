// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>

#include "inou/lefdef/pin_access.hpp"
#include "inou/lefdef/routing_occupancy.hpp"

namespace livehd::placement {
class Pin_escape_guard;
struct Pin_escape_trial {
  bool        legal = false;
  std::string reason;

private:
  friend class Pin_escape_guard;
  const Pin_escape_guard* owner      = nullptr;
  size_t                  generation = 0;
  std::string             net;
  std::vector<size_t>     blocked;
};
// Preserve at least one initially legal pin-to-escape access for each supported,
// unrouted standard-cell pin, including CLOCK/POWER/GROUND pins in special
// nets and unassigned control/supply pins. Partial signal wiring does not retire
// a pin until full-net connectivity is established. Only the supported access
// stack is enumerated precisely. Unresolved control/supply pins reserve the
// local cell volume (or known supply footprint) with per-layer via/spacing
// margins; this may reject feasible routes.
// This is not a proof of whole-design routability.
// Candidate checks do not mutate reservations.
// The frame/technology must not change except by committing complete routes;
// rebuild the guard after inserting/moving cells or changing supply geometry.
class Pin_escape_guard {
  struct Pin {
    std::string         net, label;
    size_t              remaining;
    size_t              group;
    std::vector<size_t> points;
  };
  struct Point {
    size_t          pin;
    physical::Shape escape, landing, cut, probe;
    bool            active = true;
  };
  const physical::Pin_access_layers                  layers_;
  std::unique_ptr<physical::Signal_metal_rules>      owned_rules_;
  const physical::Signal_metal_rules&                rules_;
  physical::Routing_occupancy                        index_;
  physical::Routing_occupancy                        uncertain_index_;
  std::vector<std::string>                           uncertain_labels_;
  std::map<std::string, std::pair<int64_t, int64_t>> uncertainty_margin_;
  std::vector<Pin>                                   pins_;
  std::vector<Point>                                 points_;
  std::vector<std::vector<size_t>>                   groups_;
  std::vector<bool>                                  group_supported_;
  bool                  compatible(size_t, const std::set<size_t>&, const std::string&, const std::function<bool()>&) const;
  std::vector<size_t>   indexed_points_;
  std::set<std::string> routed_;
  int64_t               metal_clearance_ = 0, cut_clearance_ = 0;
  size_t                generation_ = 0;
  std::string           incomplete_reason_;
  size_t                unresolved_pins_ = 0;

public:
  // Layers come from the technology profile. An optional compiled rule oracle
  // must match this frame and outlive the guard.
  Pin_escape_guard(const physical::Technology&, const physical::Floorplan&, const physical::Routing_occupancy&,
                   size_t access_budget, const std::function<void()>& checkpoint, physical::Pin_access_layers layers,
                   const physical::Signal_metal_rules* rules = nullptr);
  Pin_escape_trial check(const std::vector<physical::Shape>&, const std::string& net,
                         const std::function<bool()>& expired = {}) const;
  void             commit(const Pin_escape_trial&);
  size_t           protected_pins() const { return pins_.size(); }
  size_t           unresolved_pins() const { return unresolved_pins_; }
};
}  // namespace livehd::placement
