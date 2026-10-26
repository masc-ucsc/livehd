// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "checkpoint.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <tuple>

#include "checks.hpp"

namespace livehd::placement::buffering {
namespace {
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1 && DREAMPLACE_NATIVE_PIN_TIMING_API >= 3
std::set<std::string> worst_drivers(const late::Result& timing, const timeclosure::Period_objective& objective) {
  const auto&           g        = *timing.feasible_graph;
  const auto            incoming = timeclosure::incoming_arcs(g);
  std::set<std::string> result;
  for (size_t v = 0; v < g.vertices.size(); ++v) {
    if (!g.vertices[v].endpoint_required_ps) {
      continue;
    }
    if (!timeclosure::times_match(timeclosure::endpoint_cost(g, objective, v), objective.achievable_period)) {
      continue;
    }
    for (const auto id : timeclosure::worst_path_arcs(timing, objective, incoming, v)) {
      const auto& arc = g.arcs[id];
      if (arc.wire) {
        result.insert(timing.pins[g.vertices[arc.from].pin].name);
      }
    }
  }
  return result;
}
// Native/physical lookups shared by demand discovery and context refresh.
struct Checkpoint_view {
  timeclosure::Period_objective                      objective;
  std::set<std::string>                              critical;
  std::map<std::string, double>                      window;  // driver -> fresh raw LR severity
  std::map<std::string, size_t>                      pins;
  std::map<std::string, const physical::Rc_net*>     wires;
  std::map<std::string, std::string>                 masters;
  std::map<std::pair<std::string, unsigned>, double> mass;  // sink/transition LR mass
};
Checkpoint_view checkpoint_view(const Checkpoint_input& input) {
  Checkpoint_view view;
  view.objective = timeclosure::proxy_objective(input.timing, input.weights.objective);
  view.critical  = worst_drivers(input.timing, view.objective);
  for (const auto& w : std::views::values(input.weights.pins)) {
    view.window[w.driver] = std::max(view.window[w.driver], w.raw);
  }
  for (size_t i = 0; i < input.timing.pins.size(); ++i) {
    require(view.pins.emplace(input.timing.pins[i].name, i).second, "duplicate native checkpoint pin");
  }
  for (const auto& net : input.parasitics.nets) {
    require(view.wires.emplace(net.driver, &net).second, "duplicate checkpoint RC driver");
  }
  for (const auto& c : input.parasitics.cells) {
    require(view.masters.emplace(c.name, c.master).second, "duplicate checkpoint physical instance");
  }
  const auto& graph = *input.timing.feasible_graph;
  for (size_t i = 0; i < graph.arcs.size(); ++i) {
    if (graph.arcs[i].wire) {
      const auto& sink                                                = graph.vertices[graph.arcs[i].to];
      view.mass[{input.timing.pins[sink.pin].name, sink.transition}] += view.objective.arc_mass[i];
    }
  }
  return view;
}
// Fresh class and LR severity of an RC owner, counted into demand.
void classify(Allocation_net& net, const Checkpoint_view& view, Checkpoint_demand& demand) {
  const auto severity = view.window.find(net.owner);
  net.classification  = view.critical.contains(net.owner) ? Net_class::critical
                        : severity != view.window.end()   ? Net_class::window
                                                          : Net_class::outside_window;
  net.severity        = severity != view.window.end() ? severity->second : 0;
  if (net.classification == Net_class::critical) {
    ++demand.critical_nets;
  } else if (net.classification == Net_class::window) {
    ++demand.window_nets;
  } else {
    ++demand.outside_nets;
  }
}
void refresh_context(Allocation_net& net, const Checkpoint_input& input, const Checkpoint_view& view, double units,
                     const late::Driver_options& driver) {
  if (!net.context) {
    return;
  }
  const auto&  pins                = view.pins;
  const auto&  masters             = view.masters;
  const double optimization_period = view.objective.optimization_period;
  const double reference_period    = *input.timing.feasible_graph->reference_period_ps;
  auto&        c                   = *net.context;
  const auto   output              = c.driver.name + ":" + c.driver.model.output;
  const auto   source              = c.driver.name + ":" + c.driver.model.input;
  require(output == net.owner, "checkpoint source identity differs from net owner");
  if (!pins.contains(source) || !pins.contains(output) || !view.wires.contains(output)) {
    net.context.reset();
    return;
  }
  require(masters.contains(c.driver.name) && masters.at(c.driver.name) == c.driver.model.cell,
          "checkpoint source master differs from exact netlist");
  const auto si = pins.at(source), so = pins.at(output);
  for (unsigned t = 0; t < 2; ++t) {
    const auto& in  = input.timing.pins[si].transitions[t];
    const auto& out = input.timing.pins[so].transitions[t];
    if (!in.arrival_ps || !in.slew_ps || !out.predecessor || out.predecessor->pin != si) {
      net.context.reset();
      return;
    }
    c.driver.input_arrival_ps[t] = *in.arrival_ps;
    c.driver.input_slew_ps[t]    = *in.slew_ps;
  }
  require(input.locations.contains(output), "checkpoint driver lacks physical pin location");
  const auto position = [&](const std::string& pin) {
    require(input.locations.contains(pin), "checkpoint sink lacks physical pin location");
    const auto p = input.locations.at(pin);
    return Point{p.first / units, p.second / units};
  };
  c.driver.output = position(output);
  std::set<std::string> expected, observed;
  for (const auto& pin : view.wires.at(output)->nodes) {
    if (!pin.pin.empty() && pin.pin != output) {
      require(expected.insert(pin.pin).second, "duplicate physical sink in current RC");
    }
  }
  for (auto& sink : c.sinks) {
    require(observed.insert(sink.name).second && pins.contains(sink.name), "duplicate/missing checkpoint sink");
    const auto split = sink.name.rfind(':');
    if (split == std::string::npos) {
      net.context.reset();
      return;
    }
    const auto owner = sink.name.substr(0, split);
    require(masters.contains(owner) && masters.at(owner) == sink.cell && sink.name.substr(split + 1) == sink.pin,
            "checkpoint sink master/pin differs from exact netlist");
    sink.position = position(sink.name);
    for (unsigned t = 0; t < 2; ++t) {
      const auto& state = input.timing.pins[pins.at(sink.name)].transitions[t];
      sink.required_ps[t]
          = state.required_ps ? std::optional<double>(*state.required_ps + optimization_period - reference_period) : std::nullopt;
      const auto found   = view.mass.find({sink.name, t});
      sink.multiplier[t] = found == view.mass.end() ? 0 : found->second;
    }
  }
  require(expected == observed, "checkpoint context omits/changes original sink membership");
  c.normalization_period_ps = *input.timing.achievable_period_ps;
  c.driver_options          = driver;
}
Checkpoint_demand refresh_nets(const Checkpoint_input& input, const Checkpoint_view& view, std::vector<Allocation_net> nets,
                               const late::Driver_options& driver, double units) {
  Checkpoint_demand result;
  require(input.timing.characterized_boundary_policy == driver.characterized_boundary
              && input.timing.bounded_boundary_policy == driver.bounded_boundary,
          "checkpoint driver policy differs from native timing");
  for (auto& net : nets) {
    classify(net, view, result);
    refresh_context(net, input, view, units, driver);
    if (!net.context) {
      ++result.missing_contexts;
    }
  }
  result.supported_api = true;
  result.nets          = std::move(nets);
  return result;
}
#endif
}  // namespace
void Checkpoint_settings::validate() const {
  require(start >= 0 && std::isfinite(native_units_per_um) && native_units_per_um > 0 && std::isfinite(site_dbu_per_um)
              && site_dbu_per_um > 0 && bool(generate),
          "invalid joint checkpoint settings/producer");
}
Checkpoint_demand discover_checkpoint_demand(const Checkpoint_input& input, const late::Driver_options& driver, double units,
                                             size_t max_nets) {
  require(std::isfinite(units) && units > 0 && max_nets > 0, "invalid checkpoint demand selection bounds/units");
  Checkpoint_demand result;
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1 && DREAMPLACE_NATIVE_PIN_TIMING_API >= 3
  const auto            view = checkpoint_view(input);
  std::set<std::string> managed;
  if (input.previous) {
    const auto owners = std::views::keys(input.previous->allocation.sites.reservations());
    managed.insert(owners.begin(), owners.end());
  }
  std::vector<Allocation_net> selected;
  for (const auto& wire : input.parasitics.nets) {
    Allocation_net net;
    net.owner = wire.driver;
    classify(net, view, result);
    if (net.classification != Net_class::outside_window || managed.contains(net.owner)) {
      selected.push_back(std::move(net));
    }
  }
  std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) {
    return std::tuple{a.classification, -a.severity, a.owner} < std::tuple{b.classification, -b.severity, b.owner};
  });
  result.omitted_nets = selected.size() > max_nets ? selected.size() - max_nets : 0;
  selected.resize(std::min(selected.size(), max_nets));
  const auto& pins    = view.pins;
  const auto& masters = view.masters;
  for (auto& net : selected) {
    net.input_search_complete = false;
    const auto split          = net.owner.rfind(':');
    if (split == std::string::npos || !pins.contains(net.owner)) {
      continue;
    }
    const auto instance = net.owner.substr(0, split);
    if (!masters.contains(instance)) {
      continue;
    }
    const auto& out  = input.timing.pins[pins.at(net.owner)];
    const auto  rise = out.transitions[0].predecessor, fall = out.transitions[1].predecessor;
    if (!rise || !fall || rise->pin != fall->pin || rise->pin >= input.timing.pins.size()) {
      continue;
    }
    const auto& source      = input.timing.pins[rise->pin].name;
    const auto  input_split = source.rfind(':');
    if (input_split == std::string::npos || source.substr(0, input_split) != instance) {
      continue;
    }
    Context     context;
    const auto& box      = input.design.layout;
    context.core         = {box.xl / units, box.yl / units, box.xh / units, box.yh / units};
    context.driver.name  = instance;
    context.driver.model = {masters.at(instance), source.substr(input_split + 1), net.owner.substr(split + 1)};
    bool supported       = true;
    for (const auto& node : view.wires.at(net.owner)->nodes) {
      if (node.pin.empty() || node.pin == net.owner) {
        continue;
      }
      const auto colon = node.pin.rfind(':');
      if (colon == std::string::npos || !masters.contains(node.pin.substr(0, colon))) {
        supported = false;
        break;
      }
      Sink sink;
      sink.name = node.pin;
      sink.cell = masters.at(node.pin.substr(0, colon));
      sink.pin  = node.pin.substr(colon + 1);
      context.sinks.push_back(std::move(sink));
    }
    if (supported && !context.sinks.empty()) {
      net.context = std::move(context);
    }
  }
  auto refreshed          = refresh_nets(input, view, std::move(selected), driver, units);
  result.nets             = std::move(refreshed.nets);
  result.missing_contexts = refreshed.missing_contexts;
  result.supported_api    = true;
#else
  (void)input;
  (void)driver;
#endif
  return result;
}
Checkpoint_result prepare_checkpoint(const Checkpoint_input& input, const Checkpoint_problem& problem,
                                     const std::optional<Checkpoint_state>& original, const liberty::timing::Library& library,
                                     const Gap_force_payload& base, const std::map<std::string, int>& drivers,
                                     const late::Driver_options& driver, const Checkpoint_settings& settings) {
  settings.validate();
  require(input.iteration >= settings.start, "joint checkpoint before configured activation");
  Checkpoint_result result;
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1 && DREAMPLACE_NATIVE_PIN_TIMING_API >= 3
  auto  demand            = refresh_nets(input, checkpoint_view(input), problem.nets, driver, settings.native_units_per_um);
  auto& nets              = demand.nets;
  result.critical_nets    = demand.critical_nets;
  result.window_nets      = demand.window_nets;
  result.outside_nets     = demand.outside_nets;
  result.missing_contexts = demand.missing_contexts;
  Checkpoint_state next{Site_reservations(problem.sites)};
  next.epoch     = timeclosure::timing_epoch(input.timing);
  next.iteration = input.iteration;
  if (original) {
    require(original->iteration < input.iteration && original->epoch == next.epoch,
            "joint checkpoint needs increasing iterations and an explicit full-graph epoch rebase");
    require(input.design.density_reservations == original->density.footprints, "native/host reservation checkpoint state diverged");
    next.allocation.resources = original->allocation.resources;
    next.allocation.history   = original->allocation.history;
    next.density              = original->density;
    next.pulls                = original->pulls;
    for (const auto& [owner, spans] : original->allocation.sites.reservations()) {
      if (!problem.retired.contains(owner)) {
        require(next.allocation.sites.replace(owner, spans).status == Reservation_status::reserved,
                "changed geometry invalidates protected reservation; replan required");
      }
    }
  } else {
    next.density.footprints = input.design.density_reservations;
  }
  auto config           = settings.allocation;
  config.geometry_epoch = problem.geometry_epoch;
  config.dbu_per_um     = settings.site_dbu_per_um;
  result.negotiation    = negotiate(library, next.allocation, nets, problem.capacities, problem.retired, config);
  std::set<std::string>            released = problem.retired;
  std::map<std::string, Net_class> classes;
  for (size_t i = 0; i < nets.size(); ++i) {
    classes[nets[i].owner] = nets[i].classification;
    if (result.negotiation->nets[i].candidate) {
      released.insert(nets[i].owner);
    }
  }
  std::erase_if(next.pulls, [&](const auto& pull) { return released.contains(pull.owner); });
  for (auto& pull : next.pulls) {
    if (classes.contains(pull.owner)) {
      pull.priority = classes.at(pull.owner);
    }
  }
  next.pulls.insert(next.pulls.end(), result.negotiation->pulls.begin(), result.negotiation->pulls.end());
  result.density = bind_reservation_density(input.design,
                                            next.density,
                                            nets,
                                            *result.negotiation,
                                            settings.native_units_per_um,
                                            settings.site_dbu_per_um,
                                            problem.materialized,
                                            problem.retired);
  result.forces
      = bind_gap_forces(input.design, base, drivers, next.pulls, settings.native_units_per_um, config.maximum_pull_fraction);
  require(result.density.supported_api && result.forces.supported_api, "joint checkpoint native API unavailable");
  next.allocation      = result.negotiation->proposal;
  next.density         = result.density.proposal;
  result.proposal      = std::move(next);
  result.supported_api = true;
#else
  (void)problem;
  (void)original;
  (void)library;
  (void)base;
  (void)drivers;
  (void)driver;
#endif
  return result;
}
}  // namespace livehd::placement::buffering
