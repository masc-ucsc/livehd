// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "dreamplace/native/placement.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Final physical positions, pin offsets and clock/reset masks. Labels use the
// same pin index as design.pin2node, not the CSR traversal index. Retain only
// when requested; ordinary placement must not copy its whole design for this.
struct Preroute_design {
  DreamPlace::native::Design                       design;
  std::vector<std::string>                         pin_labels;
  // Exact physical pin identities of characterized constant-source instances.
  std::set<std::string>                            tie_pins;
  // Host-resolved exclusions belong to this exact final netlist snapshot.
  // Preserve them when constructing speculative repeater/demand views.
  std::set<std::string>                            dont_touch_pins;
  // Retained SAT witnesses keyed by complete endpoint identities, not mutable net names.
  std::map<std::set<std::string>, physical::Shape> sat_routes;

  bool protects(const physical::Net& net) const {
    if (dont_touch_pins.empty()) {
      return false;
    }
    return std::ranges::any_of(net.connections,
                               [&](const auto& pin) { return dont_touch_pins.contains(physical::connection_label(pin)); });
  }
};
}  // namespace livehd::placement
