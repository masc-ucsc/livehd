// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "preroute_repair.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

#include "inou/lefdef/routing_geometry.hpp"
#include "repeater_wire_filter.hpp"
#include "inou/lefdef/routing_rules.hpp"

namespace livehd::placement::detail {
static Repeater_snapshot make_repeater_snapshot(const Result& original, const Repeater_graph& proposal,
                                                  const Preroute_binding& binding, const physical::Technology& tech,
                                                  Layer_rc route_rc,
                                                  const std::function<bool(const physical::Component&)>* cached_filter) {
  if (!original.parasitics || !original.preroute_design || !proposal.graph || !proposal.library || proposal.cells.empty()
      || proposal.cells.size() != proposal.tree.buffers.size() || proposal.cells.size() != proposal.ports.size() || binding.clock
      || binding.reset || binding.tie || binding.dont_touch || !std::isfinite(route_rc.r) || route_rc.r <= 0
      || !std::isfinite(route_rc.c) || route_rc.c <= 0 || original.floorplan.dbu != tech.dbu) {
    throw std::invalid_argument("invalid pre-route repeater snapshot proposal");
  }
  std::function<bool(const physical::Component&)> local_filter;
  if (!cached_filter) {
    local_filter = repeater_wire_filter(tech, original.floorplan);
    cached_filter = &local_filter;
  }
  if (*cached_filter) {
    for (const auto& cell : proposal.cells) {
      if (!(*cached_filter)(cell)) {
        throw std::invalid_argument("repeater site conflicts with existing signal wiring: " + cell.name);
      }
    }
  }
  const auto& old_rc     = original.parasitics->nets.at(binding.rc_net);
  const auto& old_net    = original.floorplan.nets.at(binding.physical_net);
  const auto& old_design = original.preroute_design->design;
  old_design.validate();
  if (original.preroute_design->protects(old_net)) {
    throw std::invalid_argument("cannot split a protected pre-route net");
  }
  if (!physical::unrouted_signal(old_net) || !old_design.net_mask.at(binding.demand_net)) {
    throw std::invalid_argument("cannot split an already routed, non-signal or excluded net");
  }
  const auto label = [](const physical::Connection& connection) {
    if (connection.instance == "PIN") {
      throw std::invalid_argument("pre-route repeaters require cell endpoints");
    }
    return connection.instance + ":" + connection.pin;
  };
  const auto connection = [](const std::string& pin) {
    const auto colon = pin.rfind(':');
    if (colon == std::string::npos || !colon || colon + 1 == pin.size()) {
      throw std::invalid_argument("invalid repeater pin label");
    }
    return physical::Connection{pin.substr(0, colon), pin.substr(colon + 1)};
  };
  std::set<std::string> physical_pins, rc_pins, demand_pins;
  for (const auto& pin : old_net.connections) {
    if (!physical_pins.insert(label(pin)).second) {
      throw std::invalid_argument("duplicate repeater physical endpoint");
    }
  }
  for (const auto& node : old_rc.nodes) {
    if (!node.pin.empty()) {
      rc_pins.insert(node.pin);
    }
  }
  for (int i = old_design.flat_net2pin_start.at(binding.demand_net); i < old_design.flat_net2pin_start.at(binding.demand_net + 1);
       ++i) {
    demand_pins.insert(original.preroute_design->pin_labels.at(old_design.flat_net2pin.at(i)));
  }
  std::set<std::string> leaves(proposal.sinks.begin(), proposal.sinks.end());
  if (leaves.size() != proposal.sinks.size() || leaves.contains(old_rc.driver)) {
    throw std::invalid_argument("duplicate repeater leaves");
  }
  leaves.insert(old_rc.driver);
  if (physical_pins != rc_pins || physical_pins != demand_pins || physical_pins != leaves) {
    throw std::invalid_argument("repeater source DEF/RC/demand/graph endpoints differ");
  }
  Repeater_snapshot result;
  result.state            = original;
  result.state.fp_library = proposal.library;
  result.state.graph      = proposal.graph;
  result.state.parasitics = std::make_shared<physical::Parasitics>(*original.parasitics);
  auto& rc                = *result.state.parasitics;
  rc.identity.clear();
  rc.approximate                 = true;
  auto& frame                    = result.state.floorplan;
  auto  snapshot                 = std::make_shared<Preroute_design>();
  snapshot->tie_pins             = original.preroute_design->tie_pins;
  snapshot->dont_touch_pins      = original.preroute_design->dont_touch_pins;
  snapshot->pin_labels           = original.preroute_design->pin_labels;
  auto& design                   = snapshot->design;
  // This view is consumed only by routing demand, not another GP solve. Retain
  // physical macro/capacity identities; omit irrelevant GP groups and anchors.
  design.layout                  = old_design.layout;
  design.num_movable             = old_design.num_movable;
  design.node_x                  = old_design.node_x;
  design.node_y                  = old_design.node_y;
  design.node_size_x             = old_design.node_size_x;
  design.node_size_y             = old_design.node_size_y;
  design.node_macro              = old_design.node_macro;
  design.node_blocked_capacity_h = old_design.node_blocked_capacity_h;
  design.node_blocked_capacity_v = old_design.node_blocked_capacity_v;
  design.pin2node                = old_design.pin2node;
  design.pin_offset_x            = old_design.pin_offset_x;
  design.pin_offset_y            = old_design.pin_offset_y;
  design.net_mask                = old_design.net_mask;
  design.net_weights             = old_design.net_weights;
  std::map<std::string, int> pin_ids;
  for (size_t i = 0; i < snapshot->pin_labels.size(); ++i) {
    if (!pin_ids.emplace(snapshot->pin_labels[i], int(i)).second) {
      throw std::invalid_argument("duplicate demand pin label");
    }
  }
  std::set<std::string> names, net_names;
  for (const auto& cell : frame.components) {
    names.insert(cell.name);
  }
  for (const auto& net : frame.nets) {
    net_names.insert(net.name);
  }
  std::vector<std::string> inputs, outputs;
  for (size_t i = 0; i < proposal.cells.size(); ++i) {
    const auto& cell = proposal.cells[i];
    if (!names.insert(cell.name).second || cell.place.status != physical::Status::FIXED) {
      throw std::invalid_argument("repeater reservation is not a unique fixed component");
    }
    const auto& master = tech.macros.at(cell.macro);
    if (design.node_x.size() >= size_t(std::numeric_limits<int>::max())
        || design.pin2node.size() > size_t(std::numeric_limits<int>::max()) - 2) {
      throw std::invalid_argument("repeater demand view exceeds index range");
    }
    const int node = int(design.node_x.size());
    design.node_x.push_back(cell.place.x);
    design.node_y.push_back(cell.place.y);
    design.node_size_x.push_back(cell.place.width);
    design.node_size_y.push_back(cell.place.height);
    if (!design.node_macro.empty()) {
      design.node_macro.push_back(0);
    }
    if (!design.node_blocked_capacity_h.empty()) {
      design.node_blocked_capacity_h.push_back(0);
    }
    if (!design.node_blocked_capacity_v.empty()) {
      design.node_blocked_capacity_v.push_back(0);
    }
    for (const auto& port : {proposal.ports[i].first, proposal.ports[i].second}) {
      const auto& pin = master.pins.at(port);
      if (pin.shapes.empty()) {
        throw std::invalid_argument("repeater lacks physical pin geometry");
      }
      const auto [x, y] = physical::oriented_center(master, pin.shapes.front().rect, cell.place.orientation);
      const auto name   = cell.name + ":" + port;
      if (!pin_ids.emplace(name, int(design.pin2node.size())).second) {
        throw std::invalid_argument("duplicate repeater pin");
      }
      design.pin2node.push_back(node);
      design.pin_offset_x.push_back(x * frame.dbu);
      design.pin_offset_y.push_back(y * frame.dbu);
      snapshot->pin_labels.push_back(name);
      result.state.pin_locations[name] = {cell.place.x + x * frame.dbu, cell.place.y + y * frame.dbu};
    }
    inputs.push_back(cell.name + ":" + proposal.ports[i].first);
    outputs.push_back(cell.name + ":" + proposal.ports[i].second);
    result.repeaters.push_back({cell.name, cell.macro, proposal.ports[i].first, proposal.ports[i].second});
    if (!frame.special_nets.empty()) {
      for (const auto& [name, pin] : master.pins) {
        if (pin.use != "POWER" && pin.use != "GROUND") {
          continue;
        }
        physical::Net* supply = nullptr;
        for (auto& net : frame.special_nets) {
          if (net.use != pin.use) {
            continue;
          }
          if (supply) {
            throw std::invalid_argument("ambiguous repeater power domain");
          }
          supply = &net;
        }
        if (!supply) {
          throw std::invalid_argument("missing repeater supply net");
        }
        supply->connections.push_back({cell.name, name});
      }
    }
    frame.components.push_back(cell);
    rc.cells.push_back(
        {cell.name, cell.place.x, cell.place.y, unsigned(cell.place.orientation), cell.macro, cell.place.width, cell.place.height});
  }
  std::vector<std::vector<int>> nets;
  for (size_t n = 0; n < old_design.net_weights.size(); ++n) {
    nets.emplace_back(old_design.flat_net2pin.begin() + old_design.flat_net2pin_start[n],
                      old_design.flat_net2pin.begin() + old_design.flat_net2pin_start[n + 1]);
  }
  const Wire_rc wire{route_rc.r, route_rc.c, route_rc.r, route_rc.c, "pre-route repeater proposal"};
  const auto    branch = [&](const std::string&                    driver,
                          const std::vector<Buffer_tree::Load>& loads,
                          size_t                                physical_index,
                          size_t                                rc_index,
                          size_t                                demand_index,
                          std::string                           name) {
    physical::Net                              net{std::move(name), {connection(driver)}, {}, "SIGNAL"};
    std::vector<std::pair<std::string, Point>> points{{driver, result.state.pin_locations.at(driver)}};
    std::vector<int>                           pins{pin_ids.at(driver)};
    for (const auto& load : loads) {
      const auto& pin = load.buffer ? inputs.at(load.index) : proposal.sinks.at(load.index);
      net.connections.push_back(connection(pin));
      points.emplace_back(pin, result.state.pin_locations.at(pin));
      pins.push_back(pin_ids.at(pin));
    }
    auto estimated = steiner_rc(driver, points, wire, frame.dbu);
    if (physical_index == frame.nets.size()) {
      frame.nets.push_back(std::move(net));
    } else {
      frame.nets.at(physical_index) = std::move(net);
    }
    if (rc_index == rc.nets.size()) {
      rc.nets.push_back(std::move(estimated));
    } else {
      rc.nets.at(rc_index) = std::move(estimated);
    }
    if (demand_index == nets.size()) {
      nets.push_back(std::move(pins));
      design.net_weights.push_back(old_design.net_weights.at(binding.demand_net));
      design.net_mask.push_back(1);
    } else {
      nets.at(demand_index) = std::move(pins);
    }
    result.branches.push_back({physical_index, demand_index, rc_index});
  };
  branch(old_rc.driver, proposal.tree.root, binding.physical_net, binding.rc_net, binding.demand_net, old_net.name);
  for (size_t i = 0; i < proposal.tree.buffers.size(); ++i) {
    const auto base   = "__preroute_" + proposal.cells[i].name;
    auto       name   = base;
    size_t     serial = 0;
    while (!net_names.insert(name).second) {
      name = base + "_" + std::to_string(++serial);
    }
    branch(outputs[i], proposal.tree.buffers[i].loads, frame.nets.size(), rc.nets.size(), nets.size(), name);
  }
  for (const auto& net : nets) {
    design.flat_net2pin.insert(design.flat_net2pin.end(), net.begin(), net.end());
    if (design.flat_net2pin.size() > size_t(std::numeric_limits<int>::max())) {
      throw std::invalid_argument("repeater CSR exceeds index range");
    }
    design.flat_net2pin_start.push_back(int(design.flat_net2pin.size()));
  }
  design.build_maps();
  design.validate();
  inherit_buffer_weights(result.state.net_weights, old_rc.driver, outputs);
  result.state.preroute_design = std::move(snapshot);
  return result;
}
Repeater_snapshot repeater_snapshot(const Result& original, const Repeater_graph& proposal, const Preroute_binding& binding,
                                    const physical::Technology& tech, Layer_rc route_rc) {
  return make_repeater_snapshot(original, proposal, binding, tech, route_rc, nullptr);
}
Preroute_stage_result preroute_stage(const Result& original, const physical::Technology& tech, const Route_technology_rc& wire,
                                     const std::vector<Preroute_binding>& bindings, timing::Whatif& timer,
                                     const Options& placement_options, const Preroute_options& options,
                                     const Timing_feedback& initial_timing) {
  if (!original.parasitics || !original.preroute_design || options.per_net_budget.count() <= 0
      || options.timing.deadline == std::chrono::steady_clock::time_point::max() || !std::isfinite(initial_timing.time_unit_ps)
      || initial_timing.time_unit_ps <= 0) {
    throw std::invalid_argument("pre-route stage requires physical snapshots, timing units and finite budgets");
  }
  if (options.straight_only) {
    auto routed
        = preroute(tech, original.floorplan, *original.parasitics, *original.preroute_design, bindings, wire, timer, options);
    Preroute_stage_result result;
    result.placed            = original;
    result.placed.floorplan  = std::move(routed.frame);
    result.placed.parasitics = std::make_shared<physical::Parasitics>(std::move(routed.parasitics));
    result.records           = std::move(routed.records);
    result.timings_ms        = std::move(routed.timings_ms);
    return result;
  }
  const auto            start = std::chrono::steady_clock::now();
  Preroute_stage_result out;
  out.placed            = original;
  const auto expired    = [&] { return std::chrono::steady_clock::now() >= options.timing.deadline; };
  // Records always describe the stage input, also after accepted trees.
  const auto record_for = [&](const Preroute_binding& b, std::string reason) {
    return preroute_record(original.floorplan, *original.parasitics, b, std::move(reason));
  };
  const auto                         mapping = bind_preroute(original.floorplan, *original.parasitics, *original.preroute_design);
  std::map<size_t, Preroute_binding> matched;
  for (const auto& b : mapping) {
    matched.emplace(b.physical_net, b);
  }
  std::set<size_t> unique;
  for (const auto& b : bindings) {
    const auto found = matched.find(b.physical_net);
    if (found == matched.end() || found->second.rc_net != b.rc_net || found->second.demand_net != b.demand_net
        || !unique.insert(b.physical_net).second) {
      throw std::invalid_argument("invalid pre-route stage binding");
    }
  }
  if (expired()) {
    for (const auto& b : bindings) {
      out.records.push_back(record_for(b, "stage deadline"));
    }
    return out;
  }
  Layer_rc planning_rc{0, 0};
  for (const auto& name : options.capacity.layers) {
    const auto layer = wire.layers.find(name);
    if (layer != wire.layers.end()) {
      planning_rc.r = std::max(planning_rc.r, layer->second.r);
      planning_rc.c = std::max(planning_rc.c, layer->second.c);
    }
  }
  const auto        library_start = std::chrono::steady_clock::now();
  Repeater_proposer proposer(placement_options, tech);
  const auto        length_limit = proposer.length_limit_um(planning_rc);
  if (std::isfinite(length_limit)) {
    out.repeater_length_limit_um = length_limit;
  }
  out.timings_ms["repeater_library"] = elapsed_ms(library_start);
  // The host just evaluated this physical snapshot. Ranking can reuse that
  // feedback; repeated Whatif::pin queries needlessly revisit the timer. Pending
  // caller edits require live queries instead. Every trial still rechecks timing
  // against the current journal before attempting any geometry.
  const bool                    use_feedback = timer.mark() == 0;
  std::vector<Preroute_binding> short_nets;
  struct Long_net {
    Preroute_binding binding;
    double           length, margin;
  };
  std::vector<Long_net>                         long_nets;
  std::map<std::string, const physical::Macro*> endpoint_masters;
  for (const auto& cell : original.floorplan.components) {
    endpoint_masters.emplace(cell.name, &tech.macros.at(cell.macro));
  }

  for (const auto& b : bindings) {
    const auto& net = original.parasitics->nets.at(b.rc_net);
    if (!std::isfinite(net.wire_length_um) || net.wire_length_um < 0) {
      throw std::invalid_argument("invalid source wire length");
    }
    const auto& physical_net  = original.floorplan.nets.at(b.physical_net);
    const bool cell_endpoints = std::all_of(physical_net.connections.begin(), physical_net.connections.end(), [&](const auto& pin) {
      const auto master = endpoint_masters.find(pin.instance);
      return master != endpoint_masters.end() && master->second->klass == "CORE" && !master->second->virtual_proxy;
    });
    // Let the ordinary stage record the same eligibility rejection for these
    // nets. In particular, splitting must never relabel a non-signal net SIGNAL.
    if (b.clock || b.reset || b.tie || b.dont_touch || original.preroute_design->protects(physical_net)
        || !original.preroute_design->design.net_mask.at(b.demand_net)
        || net.wire_length_um <= length_limit || !physical::unrouted_signal(physical_net) || !cell_endpoints
        || !preroute_span_exclusion(original.preroute_design->design, b.demand_net, options).empty()) {
      short_nets.push_back(b);
      continue;
    }
    double margin = std::numeric_limits<double>::infinity();
    for (const auto& node : net.nodes) {
      if (node.pin.empty()) {
        continue;
      }
      if (expired()) {
        margin = -std::numeric_limits<double>::infinity();
        break;
      }
      const auto            cached = initial_timing.pins.find(node.pin);
      std::optional<double> setup, hold;
      if (use_feedback && cached != initial_timing.pins.end()) {
        if (cached->second.slack) {
          setup = *cached->second.slack * initial_timing.time_unit_ps;
        }
        if (cached->second.hold_slack) {
          hold = *cached->second.hold_slack * initial_timing.time_unit_ps;
        }
      } else {
        const auto pin = timer.pin(node.pin);
        setup          = pin.slack;
        hold           = pin.hold_slack;
      }
      if (!setup || !hold || !std::isfinite(*setup) || !std::isfinite(*hold)) {
        margin = -std::numeric_limits<double>::infinity();
        break;
      }
      margin = std::min({margin, *setup - options.timing.setup_margin_ps, *hold - options.timing.hold_margin_ps});
    }
    long_nets.push_back({b, net.wire_length_um, margin});
  }
  std::stable_sort(long_nets.begin(), long_nets.end(), [](const auto& a, const auto& b) {
    return a.length != b.length ? a.length < b.length : a.margin > b.margin;
  });
  out.repeater_candidates = long_nets.size();
  out.repeater_margin_eligible = std::count_if(long_nets.begin(), long_nets.end(), [](const auto& candidate) {
    return candidate.margin >= 0;
  });
  out.repeater_classification_complete = !expired();
  out.timings_ms["repeater_classification"] = elapsed_ms(start);
  auto prepared_options = options;
  if (!options.deferred_pg && !prepared_options.power_grid_occupancy && !original.floorplan.special_nets.empty()
      && out.repeater_margin_eligible != 0 && !expired()) {
    struct Preparation_deadline {};
    const auto setup_start = std::chrono::steady_clock::now();
    try {
      prepared_options.power_grid_occupancy = std::make_shared<const physical::Routing_power_grid>(
          tech, original.floorplan, original.floorplan.dbu, [&] {
            if (expired()) {
              throw Preparation_deadline{};
            }
          });
    } catch (const Preparation_deadline&) {
      for (const auto& b : bindings) {
        out.records.push_back(record_for(b, "stage deadline"));
      }
      out.timings_ms["power_grid_occupancy"] = elapsed_ms(setup_start);
      return out;
    }
    out.timings_ms["power_grid_occupancy"] = elapsed_ms(setup_start);
  }
  const auto mark = timer.mark();
  try {
    if (!short_nets.empty() && !expired()) {
      auto                          routed = preroute(tech,
                             out.placed.floorplan,
                             *out.placed.parasitics,
                             *out.placed.preroute_design,
                             short_nets,
                             wire,
                             timer,
                             prepared_options);
      std::map<std::string, size_t> rc_by_name;
      for (const auto& b : short_nets) {
        rc_by_name.emplace(original.floorplan.nets.at(b.physical_net).name, b.rc_net);
      }
      for (const auto& record : routed.records) {
        if (record.accepted) {
          timer.set_net(routed.parasitics.nets.at(rc_by_name.at(record.net)));
        }
      }
      out.placed.floorplan  = std::move(routed.frame);
      out.placed.parasitics = std::make_shared<physical::Parasitics>(std::move(routed.parasitics));
      out.records           = std::move(routed.records);
      for (const auto& [name, ms] : routed.timings_ms) {
        out.timings_ms[name] += ms;
      }
    } else {
      for (const auto& b : short_nets) {
        out.records.push_back(record_for(b, "stage deadline"));
      }
    }
    if (!expired()
        && std::any_of(long_nets.begin(), long_nets.end(), [](const auto& candidate) { return candidate.margin >= 0; })) {
      const auto prepare_start = std::chrono::steady_clock::now();
      proposer.prepare();
      out.timings_ms["repeater_trial_library"] = elapsed_ms(prepare_start);
    }
    // Rejected trials preserve out.placed. Decode its fixed wires once for
    // snapshot validation, and invalidate only after an accepted tree changes
    // that exact frame. This cache never escapes the stage.
    std::optional<std::function<bool(const physical::Component&)>> snapshot_wire_filter;
    for (const auto& candidate : long_nets) {
      if (!snapshot_wire_filter && candidate.margin >= 0 && !expired()) {
        const auto prepare_start = std::chrono::steady_clock::now();
        snapshot_wire_filter.emplace(repeater_wire_filter(tech, out.placed.floorplan));
        out.timings_ms["repeater_snapshot_wire_filter"] += elapsed_ms(prepare_start);
      }
      const auto& b           = candidate.binding;
      auto        record      = record_for(b, {});
      const auto  trial_start = std::chrono::steady_clock::now();
      if (expired()) {
        record.reason = "stage deadline";
        out.records.push_back(std::move(record));
        continue;
      }
      if (candidate.margin < 0) {
        record.reason = "initial setup/hold margin";
        out.records.push_back(std::move(record));
        continue;
      }
      auto trial_options            = prepared_options;
      trial_options.timing.deadline = std::min(options.timing.deadline, trial_start + options.per_net_budget);
      std::vector<std::string> locked;
      for (const auto& net : out.placed.floorplan.nets) {
        if (net.wires.empty()) {
          continue;
        }
        for (const auto& pin : net.connections) {
          locked.push_back(physical::connection_label(pin));
        }
      }
      // The pre-route proposer retains existing buffers and reads timing only
      // at this original net's endpoints. Do not copy the full-design pin map
      // for every trial: its size is unrelated to the candidate's budget.
      Timing_feedback feedback;
      feedback.time_unit_ps = initial_timing.time_unit_ps;
      const auto source     = out.placed.parasitics->nets.at(b.rc_net);
      for (const auto& node : source.nodes) {
        if (node.pin.empty()) {
          continue;
        }
        const auto pin   = timer.pin(node.pin);
        auto&      value = feedback.pins[node.pin];
        if (const auto prior = initial_timing.pins.find(node.pin); prior != initial_timing.pins.end()) {
          value = prior->second;
        }
        if (pin.required) {
          value.required = *pin.required / feedback.time_unit_ps;
        } else {
          value.required.reset();
        }
      }
      const auto initial = check_preroute_timing(timer, source, source, locked, trial_options.timing);
      if (!initial.legal) {
        record.reason = initial.reason;
        out.records.push_back(std::move(record));
        continue;
      }
      record.attempted = true;
      ++out.repeater_attempted;
      std::optional<Repeater_snapshot> snapshot;
      Preroute_tree_trial              routed;
      const auto record_phase = [&](const char* name, const auto phase_begin) {
        const auto ms = elapsed_ms(phase_begin);
        record.timings_ms[name] += ms;
        out.timings_ms[name] += ms;
      };
      record_phase("repeater_initial", trial_start);
      const auto measured = [&](const char* name, auto&& operation) {
        const auto phase_begin = std::chrono::steady_clock::now();
        try {
          auto value = operation();
          record_phase(name, phase_begin);
          return value;
        } catch (...) {
          record_phase(name, phase_begin);
          throw;
        }
      };
      try {
        auto proposal = measured("repeater_proposal", [&] {
          return proposer.propose(out.placed, feedback, source.driver, planning_rc, trial_options.timing.deadline);
        });
        if (!proposal) {
          record.reason = std::chrono::steady_clock::now() >= trial_options.timing.deadline ? "repeater deadline"
                                                                                            : "no legal repeater proposal";
        } else if (std::chrono::steady_clock::now() >= trial_options.timing.deadline) {
          record.reason = "repeater deadline";
        } else {
          if (!prepared_options.capacity_seed && !options.deferred_pg) {
            prepared_options.capacity_seed = measured("repeater_capacity_seed", [&] {
              return std::make_shared<const Route_capacity_seed>(
                  tech, out.placed.floorplan, options.capacity, prepared_options.power_grid_occupancy.get(), [&] {
                    if (std::chrono::steady_clock::now() >= trial_options.timing.deadline) {
                      throw std::runtime_error("repeater capacity deadline");
                    }
                  });
            });
          }
          trial_options.capacity_seed = prepared_options.capacity_seed;
          snapshot = measured("repeater_snapshot", [&] {
            return make_repeater_snapshot(out.placed, *proposal, b, tech, planning_rc, &*snapshot_wire_filter);
          });
          if (std::chrono::steady_clock::now() >= trial_options.timing.deadline) {
            record.reason = "repeater deadline";
          } else {
            routed = measured("repeater_tree", [&] {
              return preroute_repeated_tree(tech,
                                            snapshot->state.floorplan,
                                            *snapshot->state.parasitics,
                                            *snapshot->state.preroute_design,
                                            snapshot->branches,
                                            wire,
                                            timer,
                                            trial_options,
                                            source,
                                            snapshot->repeaters,
                                            locked);
            });
            if (!routed.result) {
              record.reason          = routed.reason;
              record.capacity_detail = routed.capacity_detail;
            }
          }
        }
      } catch (const std::invalid_argument& error) {
        record.reason = std::string("repeater proposal: ") + error.what();
      } catch (const std::runtime_error& error) {
        record.reason = std::string("repeater proposal: ") + error.what();
      }
      if (routed.result) {
        // Commit failures must escape to the whole-stage rollback. Treating a
        // partially installed tree as an ordinary rejected proposal would leave
        // subsequent trials observing cells or RC absent from out.placed.
        for (const auto& cell : snapshot->repeaters) {
          timer.add_cell(cell.instance, cell.master);
        }
        for (const auto& branch : snapshot->branches) {
          timer.set_net(routed.result->parasitics.nets.at(branch.rc_net));
        }
        snapshot->state.floorplan  = std::move(routed.result->frame);
        snapshot->state.parasitics = std::make_shared<physical::Parasitics>(std::move(routed.result->parasitics));
        const auto previous_owner  = out.placed.fp_library;
        out.placed                 = std::move(snapshot->state);
        snapshot_wire_filter.reset();
        prepared_options.capacity_seed.reset();
        out.repeater_trees++;
        out.repeater_cells += snapshot->repeaters.size();
        for (auto& accepted : routed.result->records) {
          out.records.push_back(std::move(accepted));
        }
        record.accepted = true;  // branch records above replace the original net record
      }
      // Rejected trees still consumed setup and search work. Keep these as
      // subsets of repeater_trials, separate from the ordinary short-net pass.
      for (const auto& [name, phase_ms] : routed.timings_ms) {
        out.timings_ms["repeater_route_" + name] += phase_ms;
        record.timings_ms["route_" + name] = phase_ms;
      }
      const auto ms = elapsed_ms(trial_start);
      out.timings_ms["repeater_trials"] += ms;
      if (!record.accepted) {
        record.timings_ms["repeater_trial"] = ms;
        out.records.push_back(std::move(record));
      }
    }
  } catch (...) {
    timer.rollback(mark);
    throw;
  }
  timer.rollback(mark);
  return out;
}
}  // namespace livehd::placement::detail
