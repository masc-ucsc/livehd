// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "pin_weights.hpp"

namespace livehd::placement::timeclosure {
struct Legalization_pin {
  std::string name;
  size_t      node = 0, net = 0;
};
struct Legalization_binding {
  size_t                        nodes = 0, movable = 0;
  std::vector<Legalization_pin> pins;
  // A physical pin index for each net's unique driver.
  std::vector<size_t>           drivers;
  // Empty denotes unit costs; zero strength preserves that empty representation.
  std::vector<double>           node_weights, net_weights;
};
struct Legalization_strength {
  double displacement = 0, net = 0;
  void   validate() const;
  bool   enabled() const { return displacement > 0 || net > 0; }
};
struct Legalization_priorities {
  std::vector<double> node_weights, net_weights;
  std::vector<double> node_criticality, net_criticality;
  size_t              weighted_pins = 0, protected_movable = 0, protected_nets = 0;
};
// Map freshly evaluated native LR/path RAW weights to bounded relative costs.
// Normalize by their common maximum; never reuse the GP smoothing history.
// Both ends of a weighted physical arc protect their movable owners. A net's
// priority is its largest pin priority, without fanout-dependent accumulation.
// Fixed nodes stay unchanged. Validate all identities and output costs before
// returning; callers install the complete result atomically at the actual GP
// frame and use a fresh final-legal timing guard. This never moves a cell.
Legalization_priorities legalization_priorities(const Legalization_binding&, const Pin_weights&, Legalization_strength);
}  // namespace livehd::placement::timeclosure
