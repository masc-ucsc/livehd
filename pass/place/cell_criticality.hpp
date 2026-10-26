// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <optional>
#include <vector>

#include "core/timing_feedback.hpp"
#include "dreamplace/native/placement.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Shared timing inputs of the region and bin consumers (bus order, boundary
// pins, SAT spill, terminal critical selection). Each consumer keeps its own
// movability rule; this only answers the per-cell questions once.
struct Cell_criticality {
  bool                               timed_units = false;  // finite positive Feedback::time_unit_ps
  std::vector<std::optional<double>> slack_ps;             // min finite own-pin slack
  std::vector<bool>                  clock_connected;      // touches a net with a clock pin
  std::vector<bool>                  untimed;              // some own pin has no finite slack
  std::vector<bool>                  connected;            // has at least one considered net connection
  // Every own pin has a finite slack in valid units; then slack_ps is set.
  bool                               fully_timed(size_t cell) const { return timed_units && connected[cell] && !untimed[cell]; }
  // `ignored` (optional, one flag per net) removes nets, e.g. pre-CTS networks.
  static Cell_criticality from(const timing::Feedback&, const physical::Floorplan&, const std::vector<bool>* ignored = nullptr);
};

// Native-weight criticality per node: the maximum weight of its unmasked
// nets, at least 1. GP timing spread uses the same response natively.
std::vector<double> node_weight_criticality(const DreamPlace::native::Design&);
// Clamp each movable node box to +-reach/sqrt(criticality) around its position.
void                limit_displacement(DreamPlace::native::Design&, const std::vector<double>& criticality, double reach);
}  // namespace livehd::placement
