// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "proxy_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace livehd::placement::timeclosure {
Objective_network objective_network(const late::Result& timing) {
  if (!timing.certified || !timing.unsupported.empty() || !timing.achievable_period_ps || !timing.feasible_graph
      || !timing.feasible_graph->reference_period_ps) {
    throw std::invalid_argument("period objective requires captured feasible graph and supported native period coverage");
  }
  const auto& graph = *timing.feasible_graph;
  if (graph.launch_domains.empty() || (!timing.domains.empty() && graph.launch_domains.size() != timing.domains.size())
      || (timing.domains.empty() && (graph.launch_domains.size() != 1 || graph.launch_domains.front()))) {
    throw std::invalid_argument("inconsistent captured launch domains");
  }
  Objective_network network;
  network.reference_period = *graph.reference_period_ps;
  network.vertices         = graph.vertices.size();
  for (size_t i = 0; i < graph.vertices.size(); ++i) {
    const auto& vertex = graph.vertices[i];
    if (vertex.pin >= timing.pins.size() || vertex.transition > 1 || vertex.domain >= graph.launch_domains.size()
        || (!timing.domains.empty() && graph.launch_domains[vertex.domain] != timing.domains[vertex.domain].launch_clock)) {
      throw std::invalid_argument("invalid captured timing vertex identity");
    }
    if (vertex.boundary_arrival_ps) {
      network.boundaries.push_back({i, *vertex.boundary_arrival_ps});
    }
    if (vertex.endpoint_required_ps) {
      if (!timing.pins[vertex.pin].endpoint) {
        throw std::invalid_argument("captured requirement on a non-endpoint");
      }
      network.endpoints.push_back({i, *vertex.endpoint_required_ps});
    }
  }
  for (const auto& arc : graph.arcs) {
    network.arcs.push_back({arc.from, arc.to, arc.delay_ps});
  }
  return network;
}

Period_objective proxy_objective(const late::Result& timing, Objective_settings settings) {
  const auto objective = period_objective(objective_network(timing), settings);
  const auto close     = [](double a, double b) {
    return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= std::max(1e-9, std::max(std::abs(a), std::abs(b)) * 1e-12);
  };
  if (!close(objective.achievable_period, *timing.achievable_period_ps)) {
    throw std::invalid_argument("captured objective period disagrees with native timing");
  }
  const auto& graph = *timing.feasible_graph;
  for (size_t i = 0; i < graph.vertices.size(); ++i) {
    const auto&           vertex = graph.vertices[i];
    std::optional<double> arrival;
    if (timing.domains.empty()) {
      arrival = timing.pins[vertex.pin].transitions[vertex.transition].arrival_ps;
    } else {
      const auto& domain = timing.domains[vertex.domain];
      if (vertex.pin >= domain.pins.size()) {
        throw std::invalid_argument("captured domain pin outside observation");
      }
      arrival = domain.pins[vertex.pin][vertex.transition].arrival_ps;
    }
    if (!arrival || !close(objective.arrivals[i], *arrival)) {
      throw std::invalid_argument("captured objective arrival disagrees with native timing");
    }
  }
  return objective;
}
}  // namespace livehd::placement::timeclosure
