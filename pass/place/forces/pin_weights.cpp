// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pin_weights.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "pass/place/require.hpp"

namespace livehd::placement::timeclosure {
namespace {
constexpr size_t absent = std::numeric_limits<size_t>::max();
bool             same_cell(const hhds::Node_class& a, const hhds::Node_class& b) {
  // Node_class equality compares numeric node ids only. Include the graph and
  // occurrence context so another graph/instance can never satisfy ownership.
  return a.is_valid() && b.is_valid() && a.get_graph() == b.get_graph() && a == b && a.get_context() == b.get_context()
         && (a.is_class() || a.get_root_gid() == b.get_root_gid()) && (!a.is_hier() || a.get_hier_pos() == b.get_hier_pos());
}
double blend_factor(double raw, double old, const Pin_weight_settings& s) {
  if (s.blend_base == 0 || s.rising_base == 1) {
    return s.blend_base;
  }
  const double change  = std::max(raw - old, 0.);
  const double log_eta = std::log(s.blend_base) + std::pow(change, s.rising_exponent) * std::log(s.rising_base);
  return log_eta >= 0 ? 1 : std::exp(log_eta);
}
}  // namespace
void Pin_weight_settings::validate() const {
  objective.validate();
  for (double value : {graph_fraction, blend_base, rising_base, rising_exponent}) {
    require(std::isfinite(value), "nonfinite pin weight setting");
  }
  require(
      graph_fraction >= 0 && graph_fraction <= 1 && blend_base >= 0 && blend_base <= 1 && rising_base >= 1 && rising_exponent > 0,
      "invalid pin weight blend/normalization policy");
}
Pin_weight_bank::Pin_weight_bank(Pin_weight_settings config) : settings(config) { settings.validate(); }
void Pin_weight_bank::set_temperature_fraction(double value) {
  auto next                           = settings;
  next.objective.temperature_fraction = value;
  next.validate();
  settings = next;
}
void Pin_weight_bank::clear() {
  previous.clear();
  epoch.reset();
}
Pin_weights Pin_weight_bank::update(const late::Result& timing) {
  const auto          objective = proxy_objective(timing, settings.objective);
  const auto&         g         = *timing.feasible_graph;
  std::vector<double> graph(timing.pins.size()), path(timing.pins.size());
  std::vector<size_t> driver(timing.pins.size(), absent);
  std::vector<char>   declaration_direction(timing.pins.size(), 3);
  const auto          incoming                 = incoming_arcs(g);
  size_t              related_output_path_arcs = 0;
  for (size_t i = 0; i < g.arcs.size(); ++i) {
    const auto& arc  = g.arcs[i];
    const auto& from = g.vertices[arc.from];
    const auto& to   = g.vertices[arc.to];
    require(from.domain == to.domain, "timing arc crosses launch domains");
    if (arc.wire) {
      require(driver[to.pin] == absent || driver[to.pin] == from.pin, "multiple physical timing drivers");
      driver[to.pin]  = from.pin;
      graph[to.pin]  += objective.arc_mass[i];
    }
  }
  const auto has_declaration = [&](size_t id, bool output) {
    auto& known = declaration_direction[id];
    if (known == 3) {
      known           = 0;
      const auto& pin = timing.pins[id];
      if (pin.graph_node.is_valid() && !pin.clock) {
        if (const auto io = pin.graph_node.get_subnode_io()) {
          const auto matches = [&](const auto& port) {
            // Input and output declaration numbers can overlap. Require the
            // exact instance, direction, port number and physical name.
            return port.port_id == pin.declaration_port
                   && pin.name == std::string(pin.graph_node.get_name()) + ":" + std::string(port.name);
          };
          if (std::any_of(io->get_input_pin_decls().begin(), io->get_input_pin_decls().end(), matches)) {
            known = 1;
          }
          if (std::any_of(io->get_output_pin_decls().begin(), io->get_output_pin_decls().end(), matches)) {
            known = 2;
          }
        }
      }
    }
    return known == (output ? 2 : 1);
  };
  std::vector<size_t> endpoints;
  size_t              endpoint = 0;
  for (size_t i = 0; i < g.vertices.size(); ++i) {
    if (g.vertices[i].endpoint_required_ps) {
      if (objective.in_window[endpoint++]) {
        endpoints.push_back(i);
      }
    }
  }
  const auto cost = [&](size_t v) { return endpoint_cost(g, objective, v); };
  std::sort(endpoints.begin(), endpoints.end(), [&](size_t a, size_t b) {
    if (cost(a) != cost(b)) {
      return cost(a) > cost(b);
    }
    const auto& x = g.vertices[a];
    const auto& y = g.vertices[b];
    return std::tie(timing.pins[x.pin].name, x.transition, x.domain) < std::tie(timing.pins[y.pin].name, y.transition, y.domain);
  });
  for (size_t rank = 0; rank < endpoints.size(); ++rank) {
    const double                    reciprocal         = 1. / double(rank + 1);
    double                          pending_cell_delay = 0;
    std::optional<hhds::Node_class> pending_owner;
    for (const auto id : worst_path_arcs(timing, objective, incoming, endpoints[rank])) {
      const auto&  arc          = g.arcs[id];
      const auto&  v            = g.vertices[arc.to];
      const auto   source       = g.vertices[arc.from].pin;
      const double contribution = std::max(arc.delay_ps, 0.) * reciprocal * reciprocal;
      if (arc.wire) {
        require(!pending_owner, "related-output path contribution crossed a wire before reaching its physical input");
        path[v.pin] += contribution;
      } else {
        if (pending_owner) {
          require(
              same_cell(timing.pins[source].graph_node, *pending_owner) && same_cell(timing.pins[v.pin].graph_node, *pending_owner),
              "related-output path contribution crossed cell ownership");
        }
        if (driver[source] != absent) {
          require(!pending_owner || has_declaration(source, false),
                  "related-output path contribution did not reach a declared input");
          path[source]       += contribution + pending_cell_delay;
          pending_cell_delay  = 0;
          pending_owner.reset();
        } else if (contribution > 0 || pending_owner) {
          // Liberty may characterize output-to-output dependencies, including
          // outputs with no materialized external graph pin. Follow this same
          // ranked worst path through the cell and charge the entire positive
          // cell-delay chain to its influencing physical input. Never invent
          // an incoming net for an output or silently discard its contribution.
          require(has_declaration(source, true) && same_cell(timing.pins[source].graph_node, timing.pins[v.pin].graph_node),
                  "unbound cell-arc source is not a declared same-cell output");
          pending_owner       = timing.pins[source].graph_node;
          pending_cell_delay += contribution;
          if (contribution > 0) {
            ++related_output_path_arcs;
          }
          require(std::isfinite(pending_cell_delay), "nonfinite related-output path contribution");
        }
      }
    }
    require(!pending_owner, "related-output path contribution has no influencing physical input before launch boundary");
  }
  const double graph_max     = *std::max_element(graph.begin(), graph.end());
  const double path_max      = *std::max_element(path.begin(), path.end());
  const auto   current_epoch = timing_epoch(timing);
  const bool   reset         = epoch && *epoch != current_epoch;
  Pin_weights  result;
  result.objective                = settings.objective;
  result.critical_endpoints       = endpoints.size();
  result.epoch_reset              = reset;
  result.related_output_path_arcs = related_output_path_arcs;
  for (size_t i = 0; i < timing.pins.size(); ++i) {
    const double graph_part = graph_max > 0 ? graph[i] / graph_max : 0;
    const double path_part  = path_max > 0 ? path[i] / path_max : 0;
    const double raw        = settings.graph_fraction * graph_part + path_part;
    require(std::isfinite(raw) && raw >= 0, "nonfinite normalized pin weight");
    if (raw == 0) {
      continue;
    }
    if (timing.pins[i].clock || driver[i] == absent) {
      throw std::invalid_argument("weighted pin lacks a physical data-net driver: " + timing.pins[i].name);
    }
    const auto&  name         = timing.pins[i].name;
    const auto&  source       = timing.pins[driver[i]].name;
    auto         previous_pin = previous.find(name);
    const double old
        = !reset && previous_pin != previous.end() && previous_pin->second.driver == source ? previous_pin->second.weight : 0;
    const double eta    = blend_factor(raw, old, settings);
    const double weight = eta * raw + (1 - eta) * old;
    require(std::isfinite(eta) && eta >= 0 && eta <= 1 && std::isfinite(weight) && weight >= 0, "nonfinite blended pin weight");
    require(result.pins.emplace(name, Pin_weight{source, graph_part, path_part, raw, eta, weight}).second,
            "duplicate physical pin weight identity");
  }
  for (const auto& [name, value] : previous) {
    const auto found = result.pins.find(name);
    if (reset || found == result.pins.end() || found->second.driver != value.driver) {
      ++result.removed;
    }
  }
  previous = result.pins;
  epoch    = current_epoch;
  return result;
}
}  // namespace livehd::placement::timeclosure
