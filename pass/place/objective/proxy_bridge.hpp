// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass/place/late_timing.hpp"
#include "period.hpp"

namespace livehd::placement::timeclosure {
// Requires explicit graph capture, complete native timing coverage and one
// supported reference period. Independent fidelity/GP readiness gates still
// apply. Vertices/arcs retain Result::feasible_graph ordering and physical-pin
// identities; no worst-predecessor approximation or missing-data zero is used.
Objective_network                objective_network(const late::Result& timing);
// Also verifies every reconstructed hard arrival and T* against the native
// observation. Endpoint requirements are captured before backward propagation.
Period_objective                 proxy_objective(const late::Result& timing, Objective_settings settings = {});
// Captured feasible-graph arc indices entering each vertex, in capture order.
std::vector<std::vector<size_t>> incoming_arcs(const late::Feasible_timing_graph& graph);
// The captured arc realizing a vertex's native worst predecessor: same pin,
// transition and launch domain, consistent with the objective's hard arrivals,
// largest delay among equal-arrival arcs. Nullopt at a launch boundary. A
// missing boundary or matching arc is an explicit error, never a guess.
std::optional<size_t>            worst_predecessor_arc(const late::Result& timing, const Period_objective& objective,
                                                       const std::vector<std::vector<size_t>>& incoming, size_t vertex);
// Topology/model generations and driver policy of one native observation.
Objective_epoch                  timing_epoch(const late::Result& timing);
}  // namespace livehd::placement::timeclosure
