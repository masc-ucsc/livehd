// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "period.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>

namespace livehd::placement::timeclosure {
namespace {
void finite(double value) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument("nonfinite period objective value");
  }
}
double sum(double a, double b) {
  const double value = a + b;
  finite(value);
  return value;
}
double smooth_max(const std::vector<double>& values, double tau) {
  const double peak = *std::max_element(values.begin(), values.end());
  double       mass = 0;
  for (double value : values) {
    mass += std::exp((value - peak) / tau);
  }
  return sum(peak, tau * std::log(mass));
}
std::vector<double> probabilities(const std::vector<double>& values, double tau) {
  const double        peak = *std::max_element(values.begin(), values.end());
  std::vector<double> weights;
  double              mass = 0;
  for (double value : values) {
    weights.push_back(std::exp((value - peak) / tau));
    mass += weights.back();
  }
  for (double& weight : weights) {
    weight /= mass;
  }
  return weights;
}
}  // namespace

void Objective_settings::validate() const {
  finite(window_fraction);
  finite(temperature_fraction);
  if (window_fraction < 0 || window_fraction >= 1 || temperature_fraction <= 0 || temperature_fraction > .5) {
    throw std::invalid_argument("invalid period objective window/temperature");
  }
}

Period_objective period_objective(const Objective_network& network, Objective_settings settings) {
  settings.validate();
  finite(network.reference_period);
  if (!network.vertices || network.endpoints.empty() || network.reference_period <= 0) {
    throw std::invalid_argument("period objective requires vertices, constrained endpoints and a positive reference period");
  }
  const size_t                       n = network.vertices;
  std::vector<std::vector<size_t>>   incoming(n), outgoing(n);
  std::vector<size_t>                indegree(n);
  std::vector<std::optional<size_t>> boundary(n);
  const auto                         vertex = [&](size_t index) {
    if (index >= n) {
      throw std::invalid_argument("period objective vertex outside DAG");
    }
  };
  for (size_t i = 0; i < network.arcs.size(); ++i) {
    const auto& arc = network.arcs[i];
    vertex(arc.from);
    vertex(arc.to);
    finite(arc.delay);
    incoming[arc.to].push_back(i);
    outgoing[arc.from].push_back(i);
    ++indegree[arc.to];
  }
  for (size_t i = 0; i < network.boundaries.size(); ++i) {
    const auto& input = network.boundaries[i];
    vertex(input.vertex);
    finite(input.arrival);
    if (boundary[input.vertex]) {
      throw std::invalid_argument("duplicate objective launch boundary");
    }
    boundary[input.vertex] = i;
  }
  std::vector<bool> endpoint(n);
  for (const auto& output : network.endpoints) {
    vertex(output.vertex);
    finite(output.required);
    if (endpoint[output.vertex]) {
      throw std::invalid_argument("duplicate objective endpoint state");
    }
    endpoint[output.vertex] = true;
  }
  std::priority_queue<size_t, std::vector<size_t>, std::greater<>> ready;
  for (size_t i = 0; i < n; ++i) {
    if (!indegree[i]) {
      ready.push(i);
    }
  }
  std::vector<size_t> order;
  while (!ready.empty()) {
    const auto from = ready.top();
    ready.pop();
    order.push_back(from);
    for (size_t index : outgoing[from]) {
      if (--indegree[network.arcs[index].to] == 0) {
        ready.push(network.arcs[index].to);
      }
    }
  }
  if (order.size() != n) {
    throw std::invalid_argument("cycle in period objective DAG");
  }
  Period_objective result;
  result.arrivals.resize(n);
  for (size_t to : order) {
    std::optional<double> arrival;
    if (boundary[to]) {
      arrival = network.boundaries[*boundary[to]].arrival;
    }
    for (size_t index : incoming[to]) {
      const auto&  arc       = network.arcs[index];
      const double candidate = sum(result.arrivals[arc.from], arc.delay);
      arrival                = arrival ? std::max(*arrival, candidate) : candidate;
    }
    if (!arrival) {
      throw std::invalid_argument("unconstrained source in period objective DAG");
    }
    result.arrivals[to] = *arrival;
  }
  std::vector<double> cost;
  for (const auto& output : network.endpoints) {
    cost.push_back(sum(result.arrivals[output.vertex], sum(network.reference_period, -output.required)));
  }
  const double worst         = *std::max_element(cost.begin(), cost.end());
  result.achievable_period   = std::max(0., worst);
  result.fixed_wns           = sum(network.reference_period, -worst);
  result.optimization_period = (1 - settings.window_fraction) * result.achievable_period;
  result.temperature         = settings.temperature_fraction * std::max(network.reference_period, result.achievable_period);
  finite(result.temperature);
  if (result.temperature <= 0) {
    throw std::invalid_argument("underflow in period objective temperature");
  }
  result.soft_arrivals.resize(n);
  std::vector<double> arc_probability(network.arcs.size()), boundary_probability(network.boundaries.size());
  for (size_t to : order) {
    std::vector<double> candidates;
    if (boundary[to]) {
      candidates.push_back(network.boundaries[*boundary[to]].arrival);
    }
    for (size_t index : incoming[to]) {
      const auto& arc = network.arcs[index];
      candidates.push_back(sum(result.soft_arrivals[arc.from], arc.delay));
    }
    result.soft_arrivals[to] = smooth_max(candidates, result.temperature);
    const auto weights       = probabilities(candidates, result.temperature);
    size_t     candidate     = 0;
    if (boundary[to]) {
      boundary_probability[*boundary[to]] = weights[candidate++];
    }
    for (size_t index : incoming[to]) {
      arc_probability[index] = weights[candidate++];
    }
  }
  result.in_window.resize(cost.size());
  std::vector<double> soft_cost;
  const double        cutoff = worst - settings.window_fraction * result.achievable_period;
  for (size_t i = 0; i < cost.size(); ++i) {
    result.in_window[i] = cost[i] >= cutoff;
    if (result.in_window[i]) {
      const auto& output = network.endpoints[i];
      soft_cost.push_back(sum(result.soft_arrivals[output.vertex], sum(network.reference_period, -output.required)));
    }
  }
  result.soft_period = smooth_max(soft_cost, result.temperature);
  result.vertex_mass.resize(n);
  result.endpoint_mass.resize(cost.size());
  const auto endpoint_probability = probabilities(soft_cost, result.temperature);
  size_t     active               = 0;
  for (size_t i = 0; i < cost.size(); ++i) {
    if (result.in_window[i]) {
      const double mass                                = endpoint_probability[active++];
      result.endpoint_mass[i]                          = mass;
      result.vertex_mass[network.endpoints[i].vertex] += mass;
    }
  }
  result.arc_mass.resize(network.arcs.size());
  result.boundary_mass.resize(network.boundaries.size());
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    const size_t to = *it;
    if (boundary[to]) {
      const auto i            = *boundary[to];
      result.boundary_mass[i] = result.vertex_mass[to] * boundary_probability[i];
    }
    for (size_t index : incoming[to]) {
      const auto&  arc              = network.arcs[index];
      const double mass             = result.vertex_mass[to] * arc_probability[index];
      result.arc_mass[index]        = mass;
      result.vertex_mass[arc.from] += mass;
    }
  }
  return result;
}

void Period_guard_settings::validate() const {
  for (double v : {repeatability_noise,
                   regression_tolerance,
                   temperature_fraction,
                   minimum_temperature_fraction,
                   maximum_temperature_fraction,
                   rejection_scale}) {
    finite(v);
  }
  if (repeatability_noise < 0 || regression_tolerance <= repeatability_noise || minimum_temperature_fraction <= 0
      || maximum_temperature_fraction > .5 || temperature_fraction < minimum_temperature_fraction
      || temperature_fraction > maximum_temperature_fraction || rejection_scale <= 0 || rejection_scale >= 1) {
    throw std::invalid_argument("invalid period guard noise/temperature bounds");
  }
}
Period_guard::Period_guard(Period_guard_settings config) : settings(config) { settings.validate(); }
void Period_guard::rebase(double retained_period, Objective_epoch current, bool supported) {
  finite(retained_period);
  if (!supported || retained_period < 0) {
    throw std::invalid_argument("unsupported retained period");
  }
  best  = retained_period;
  epoch = current;
}
Period_decision Period_guard::consider(double candidate_period, Objective_epoch current, bool supported) {
  finite(candidate_period);
  if (!supported) {
    return Period_decision::unsupported;
  }
  if (candidate_period < 0) {
    throw std::invalid_argument("negative candidate period");
  }
  if (!best || current != epoch) {
    return Period_decision::incomparable;
  }
  if (candidate_period < *best) {
    best = candidate_period;
    return Period_decision::improve;
  }
  if (candidate_period - *best <= settings.regression_tolerance) {
    return Period_decision::within_noise;
  }
  settings.temperature_fraction
      = std::max(settings.minimum_temperature_fraction, settings.temperature_fraction * settings.rejection_scale);
  return Period_decision::regression;
}
}  // namespace livehd::placement::timeclosure
