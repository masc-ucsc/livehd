// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement::detail {
// Snapshot existing signal wiring and test conservative cell body/pin/OBS clearance.
// Returns an empty function when no signal wires exist. Unsupported rules throw.
// Technology must outlive the filter; cached master geometry must remain unchanged.
std::function<bool(const physical::Component&)> repeater_wire_filter(const physical::Technology&,
                                                                   const physical::Floorplan&);
}  // namespace livehd::placement::detail
