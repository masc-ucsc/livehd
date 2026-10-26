// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass/place/late_timing.hpp"
#include "period.hpp"

namespace livehd::placement::timeclosure {
// Requires explicit graph capture, complete native timing coverage and one
// supported reference period. Independent fidelity/GP readiness gates still
// apply. Vertices/arcs retain Result::feasible_graph ordering and physical-pin
// identities; no worst-predecessor approximation or missing-data zero is used.
Objective_network objective_network(const late::Result& timing);
// Also verifies every reconstructed hard arrival and T* against the native
// observation. Endpoint requirements are captured before backward propagation.
Period_objective  proxy_objective(const late::Result& timing, Objective_settings settings = {});
}  // namespace livehd::placement::timeclosure
