// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "startup.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
namespace livehd::placement::timeclosure {
namespace {
constexpr double contact_resistance_ohm = 1e-9;
}
double startup_mean_branch_length(const physical::Parasitics& original, const std::array<double, 4>& rc) {
  for (double v : rc) {
    if (!std::isfinite(v) || v < 0) {
      throw std::invalid_argument("invalid startup technology RC");
    }
  }
  const double cap_per_um = (rc[1] + rc[3]) * .5, res_per_um = (rc[0] + rc[2]) * .5;
  double       total_length = 0;
  size_t       total_sinks  = 0;
  for (const auto& net : original.nets) {
    double cap = 0, res = 0;
    size_t sinks = 0;
    for (const auto& node : net.nodes) {
      if (!std::isfinite(node.capacitance_ff) || node.capacitance_ff < 0) {
        throw std::invalid_argument("invalid startup wire capacitance");
      }
      cap   += node.capacitance_ff;
      sinks += !node.pin.empty() && node.pin != net.driver;
    }
    for (const auto& edge : net.edges) {
      if (!std::isfinite(edge.resistance_ohm) || edge.resistance_ohm < 0) {
        throw std::invalid_argument("invalid startup wire resistance");
      }
      if (edge.resistance_ohm > contact_resistance_ohm) {
        res += edge.resistance_ohm;
      }
    }
    if ((!cap && !res) || !sinks) {
      continue;
    }
    if (cap > 0 && cap_per_um > 0) {
      total_length += cap / cap_per_um;
    } else if (res > 0 && res_per_um > 0) {
      total_length += res / res_per_um;
    } else {
      throw std::invalid_argument("startup wire prior lacks compatible technology RC");
    }
    total_sinks += sinks;
  }
  if (!std::isfinite(total_length)) {
    throw std::invalid_argument("startup wire mean overflow");
  }
  return total_sinks ? total_length / total_sinks : 0;
}
physical::Parasitics startup_average_parasitics(const physical::Parasitics& original, const std::array<double, 4>& rc,
                                                double length) {
  if (!std::isfinite(length) || length < 0) {
    throw std::invalid_argument("invalid startup mean wire length");
  }
  for (double v : rc) {
    if (!std::isfinite(v) || v < 0) {
      throw std::invalid_argument("invalid startup technology RC");
    }
  }
  physical::Parasitics result;
  result.identity         = original.identity + ":startup-mean-wire-v1";
  result.approximate      = true;
  result.cells            = original.cells;
  result.terminals        = original.terminals;
  const double resistance = (rc[0] + rc[2]) * .5 * length, capacitance = (rc[1] + rc[3]) * .5 * length;
  if (!std::isfinite(resistance) || !std::isfinite(capacitance)) {
    throw std::invalid_argument("startup mean wire RC overflow");
  }
  for (const auto& net : original.nets) {
    if (net.nodes.size() > std::numeric_limits<unsigned>::max()) {
      throw std::invalid_argument("startup prior exceeds RC index capacity");
    }
    const bool ideal
        = std::all_of(net.edges.begin(), net.edges.end(), [](const auto& e) { return e.resistance_ohm <= contact_resistance_ohm; })
          && std::all_of(net.nodes.begin(), net.nodes.end(), [](const auto& n) { return n.capacitance_ff == 0; });
    if (ideal) {
      result.nets.push_back(net);
      continue;
    }
    physical::Rc_net prior;
    prior.driver = net.driver;
    prior.nodes.push_back({net.driver, 0});
    std::set<std::string> pins;
    bool                  driver = false;
    for (const auto& node : net.nodes) {
      if (node.pin.empty()) {
        continue;
      }
      if (!pins.insert(node.pin).second) {
        throw std::invalid_argument("duplicate startup prior pin");
      }
      if (node.pin == net.driver) {
        driver = true;
        continue;
      }
      prior.nodes.push_back({node.pin, capacitance * .5});
      prior.nodes[0].capacitance_ff += capacitance * .5;
      prior.edges.push_back({0, unsigned(prior.nodes.size() - 1), resistance});
      prior.wire_length_um += length;
    }
    if (!driver || net.driver.empty() || !std::isfinite(prior.nodes[0].capacitance_ff) || !std::isfinite(prior.wire_length_um)) {
      throw std::invalid_argument("invalid startup prior membership/capacitance");
    }
    result.nets.push_back(std::move(prior));
  }
  return result;
}
Startup_weights startup_path_weights(const late::Result& timing, double fraction) {
  if (!std::isfinite(fraction) || fraction <= 0 || fraction > 1) {
    throw std::invalid_argument("startup path fraction must be in (0,1]");
  }
  const auto                             objective = proxy_objective(timing);
  const auto&                            g         = *timing.feasible_graph;
  const auto                             incoming  = incoming_arcs(g);
  std::vector<std::pair<double, size_t>> endpoints;
  for (size_t i = 0; i < g.vertices.size(); ++i) {
    if (g.vertices[i].endpoint_required_ps) {
      endpoints.emplace_back(endpoint_cost(g, objective, i), i);
    }
  }
  std::sort(endpoints.begin(), endpoints.end(), [](const auto& a, const auto& b) {
    return a.first != b.first ? a.first > b.first : a.second < b.second;
  });
  Startup_weights result;
  result.supported = true;
  result.endpoints = endpoints.size();
  result.selected  = std::min(endpoints.size(), size_t(std::ceil(fraction * endpoints.size())));
  for (size_t i = 0; i < result.selected; ++i) {
    std::vector<size_t> wires;
    for (const auto id : worst_path_arcs(timing, objective, incoming, endpoints[i].second)) {
      const auto& arc = g.arcs[id];
      if (arc.wire && !timing.pins[g.vertices[arc.from].pin].clock && !timing.pins[g.vertices[arc.to].pin].clock) {
        wires.push_back(id);
      }
    }
    for (auto id : wires) {
      const auto& arc    = g.arcs[id];
      const auto& name   = timing.pins[g.vertices[arc.to].pin].name;
      const auto& driver = timing.pins[g.vertices[arc.from].pin].name;
      auto&       weight = result.weights.pins[name];
      if (!weight.driver.empty() && weight.driver != driver) {
        throw std::invalid_argument("startup physical pin has inconsistent drivers");
      }
      weight.driver  = driver;
      weight.raw    += 1. / wires.size();
    }
  }
  double largest = 0;
  for (const auto& [pin, w] : result.weights.pins) {
    (void)pin;
    largest = std::max(largest, w.raw);
  }
  for (auto& [pin, w] : result.weights.pins) {
    (void)pin;
    w.graph = w.weight = w.raw / largest;
    w.blend            = 1;
  }
  return result;
}
}  // namespace livehd::placement::timeclosure
