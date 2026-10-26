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

namespace {
bool close(double a, double b) {
  return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= std::max(1e-9, std::max(std::abs(a), std::abs(b)) * 1e-12);
}
}  // namespace

Period_objective proxy_objective(const late::Result& timing, Objective_settings settings) {
  const auto objective = period_objective(objective_network(timing), settings);
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

std::vector<std::vector<size_t>> incoming_arcs(const late::Feasible_timing_graph& graph) {
  std::vector<std::vector<size_t>> incoming(graph.vertices.size());
  for (size_t i = 0; i < graph.arcs.size(); ++i) {
    incoming[graph.arcs[i].to].push_back(i);
  }
  return incoming;
}

std::optional<size_t> worst_predecessor_arc(const late::Result& timing, const Period_objective& objective,
                                            const std::vector<std::vector<size_t>>& incoming, size_t vertex) {
  const auto& g    = *timing.feasible_graph;
  const auto& v    = g.vertices[vertex];
  const auto  pred = timing.domains.empty() ? timing.pins[v.pin].transitions[v.transition].predecessor
                                            : timing.domains.at(v.domain).pins.at(v.pin)[v.transition].predecessor;
  if (!pred) {
    if (!v.boundary_arrival_ps) {
      throw std::invalid_argument("native worst path has no launch boundary");
    }
    return std::nullopt;
  }
  std::optional<size_t> chosen;
  for (size_t id : incoming[vertex]) {
    const auto& arc  = g.arcs[id];
    const auto& from = g.vertices[arc.from];
    if (from.pin == pred->pin && from.transition == pred->transition && from.domain == v.domain
        && close(objective.arrivals[arc.from] + arc.delay_ps, objective.arrivals[vertex])
        && (!chosen || arc.delay_ps > g.arcs[*chosen].delay_ps)) {
      chosen = id;
    }
  }
  if (!chosen) {
    throw std::invalid_argument("native worst predecessor has no matching feasible arc");
  }
  return chosen;
}

Objective_epoch timing_epoch(const late::Result& timing) {
  return {.topology                      = timing.topology_generation,
          .timing_model                  = timing.model_generation,
          .characterized_boundary_policy = timing.characterized_boundary_policy,
          .bounded_boundary_policy       = timing.bounded_boundary_policy};
}
}  // namespace livehd::placement::timeclosure
