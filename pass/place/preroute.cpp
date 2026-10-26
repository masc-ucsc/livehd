// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "preroute.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

#include "inou/lefdef/routing_geometry.hpp"
#include "pin_escape_guard.hpp"

namespace livehd::placement {
std::vector<Preroute_binding> bind_preroute(const physical::Floorplan& frame, const physical::Parasitics& rc,
                                            const Preroute_design& snapshot, const std::set<std::string>& dont_touch_pins) {
  const auto& d = snapshot.design;
  d.validate();
  if (snapshot.pin_labels.size() != d.pin2node.size()) {
    throw std::invalid_argument("pre-route demand pin labels missing");
  }
  const auto validate_protected = [&](const std::set<std::string>& protected_pins) {
    if (protected_pins.empty()) {
      return;
    }
    auto unmatched = protected_pins;
    if (unmatched.contains("")) {
      throw std::invalid_argument("empty pre-route protected endpoint identity");
    }
    for (const auto& label : snapshot.pin_labels) {
      unmatched.erase(label);
    }
    if (!unmatched.empty()) {
      throw std::invalid_argument("pre-route protected endpoint missing from final demand: " + *unmatched.begin());
    }
  };
  validate_protected(snapshot.dont_touch_pins);
  validate_protected(dont_touch_pins);
  // Endpoint sets, rather than a presumed first pin or generated DEF net name,
  // establish correspondence. This also catches a stale snapshot after repair.
  using Pins              = std::vector<std::string>;
  const auto canonicalize = [](Pins& pins) {
    std::sort(pins.begin(), pins.end());
    return std::adjacent_find(pins.begin(), pins.end()) == pins.end();
  };
  std::map<Pins, size_t> demand_nets, rc_nets;
  for (size_t n = 0; n < d.net_mask.size(); ++n) {
    Pins pins;
    pins.reserve(d.flat_net2pin_start[n + 1] - d.flat_net2pin_start[n]);
    for (int p = d.flat_net2pin_start[n]; p < d.flat_net2pin_start[n + 1]; ++p) {
      const auto& label = snapshot.pin_labels.at(d.flat_net2pin[p]);
      if (label.empty()) {
        throw std::invalid_argument("invalid pre-route demand endpoint identity");
      }
      pins.push_back(label);
    }
    if (!canonicalize(pins)) {
      throw std::invalid_argument("invalid pre-route demand endpoint identity");
    }
    if (pins.size() >= 2 && !demand_nets.emplace(std::move(pins), n).second) {
      throw std::invalid_argument("ambiguous pre-route demand net");
    }
  }
  for (size_t n = 0; n < rc.nets.size(); ++n) {
    Pins pins;
    pins.reserve(rc.nets[n].nodes.size());
    for (const auto& pin : rc.nets[n].nodes) {
      if (!pin.pin.empty()) {
        pins.push_back(pin.pin);
      }
    }
    if (!canonicalize(pins)) {
      throw std::invalid_argument("duplicate pre-route RC endpoint");
    }
    if (!std::binary_search(pins.begin(), pins.end(), rc.nets[n].driver) || pins.size() < 2
        || !rc_nets.emplace(std::move(pins), n).second) {
      throw std::invalid_argument("invalid or ambiguous pre-route RC net");
    }
  }
  std::vector<Preroute_binding> result;
  std::set<size_t>              used_demand, used_rc;
  for (size_t n = 0; n < frame.nets.size(); ++n) {
    const auto& net = frame.nets[n];
    if (net.connections.size() < 2) {
      continue;
    }
    Pins pins;
    pins.reserve(net.connections.size());
    bool tie = false, protected_net = false;
    for (const auto& pin : net.connections) {
      const auto label = physical::connection_label(pin);
      if (label.empty()) {
        throw std::invalid_argument("invalid pre-route physical endpoint identity");
      }
      pins.push_back(label);
      tie           |= snapshot.tie_pins.contains(label);
      protected_net |= snapshot.dont_touch_pins.contains(label) || dont_touch_pins.contains(label);
    }
    if (!canonicalize(pins)) {
      throw std::invalid_argument("duplicate pre-route physical endpoint");
    }
    const auto demand = demand_nets.find(pins), timing = rc_nets.find(pins);
    if (demand == demand_nets.end() || timing == rc_nets.end() || !used_demand.insert(demand->second).second
        || !used_rc.insert(timing->second).second) {
      throw std::invalid_argument("pre-route physical, timing and demand nets do not correspond");
    }
    result.push_back({n, demand->second, timing->second, false, false, tie, protected_net});
  }
  return result;
}

std::string preroute_span_exclusion(const DreamPlace::native::Design& design, size_t net, const Preroute_options& options) {
  if (options.max_rows < 0 || options.max_sites < 0) {
    throw std::invalid_argument("negative pre-route span cap");
  }
  if (!options.max_rows && !options.max_sites) {
    return {};
  }
  double xl = std::numeric_limits<double>::infinity(), yl = xl, xh = -xl, yh = -xl;
  for (int i = design.flat_net2pin_start.at(net); i < design.flat_net2pin_start.at(net + 1); ++i) {
    const auto   pin = design.flat_net2pin.at(i), node = design.pin2node.at(pin);
    const double x = design.node_x.at(node) + design.pin_offset_x.at(pin);
    const double y = design.node_y.at(node) + design.pin_offset_y.at(pin);
    if (!std::isfinite(x) || !std::isfinite(y)) {
      throw std::invalid_argument("nonfinite pre-route pin span");
    }
    xl = std::min(xl, x);
    xh = std::max(xh, x);
    yl = std::min(yl, y);
    yh = std::max(yh, y);
  }
  if (options.max_rows && yh - yl > options.max_rows * design.layout.row_height) {
    return "maximum row span";
  }
  if (options.max_sites && xh - xl > options.max_sites * design.layout.site_width) {
    return "maximum site span";
  }
  return {};
}

Preroute_record preroute_record(const physical::Floorplan& frame, const physical::Parasitics& rc, const Preroute_binding& b,
                                std::string reason) {
  const auto&     net = frame.nets.at(b.physical_net);
  Preroute_record record{net.name, std::move(reason)};
  record.pin_count           = net.connections.size();
  record.estimated_length_um = rc.nets.at(b.rc_net).wire_length_um;
  return record;
}
static Preroute_result preroute_impl(const physical::Technology& tech, const physical::Floorplan& frame,
                                     const physical::Parasitics& rc, const Preroute_design& snapshot,
                                     const std::vector<Preroute_binding>& bindings, const Route_technology_rc& wire,
                                     timing::Whatif& timer, const Preroute_options& options, bool require_all) {
  const auto& design = snapshot.design;
  design.validate();
  if (snapshot.pin_labels.size() != design.pin2node.size()) {
    throw std::invalid_argument("pre-route demand pin labels missing");
  }
  if (options.per_net_budget.count() <= 0 || !options.access_candidates || !options.patterns || options.max_rows < 0
      || options.max_sites < 0) {
    throw std::invalid_argument("invalid pre-route search budgets");
  }
  if (!std::isfinite(options.timing.hold_margin_ps) || options.timing.hold_margin_ps < 0) {
    throw std::invalid_argument("pre-routing requires characterized hold margin");
  }
  if (!std::isfinite(options.timing.setup_margin_ps) || options.timing.setup_margin_ps < 0
      || options.timing.deadline == std::chrono::steady_clock::time_point::max()) {
    throw std::invalid_argument("pre-routing requires setup margin and a finite stage deadline");
  }
  if (!std::isfinite(options.critical_fraction) || options.critical_fraction < 0 || options.critical_fraction > 1) {
    throw std::invalid_argument("invalid aligned critical fraction");
  }
  std::set<size_t> net_indices, demand_indices, rc_indices;
  for (const auto& b : bindings) {
    if (b.physical_net >= frame.nets.size() || b.demand_net >= design.net_mask.size() || b.rc_net >= rc.nets.size()
        || !net_indices.insert(b.physical_net).second || !demand_indices.insert(b.demand_net).second
        || !rc_indices.insert(b.rc_net).second) {
      throw std::invalid_argument("invalid or duplicate pre-route net mapping");
    }
  }
  const auto      route_profile = signal_route_profile(physical::resolve_tech_profile(tech, options.tech_profile), tech);
  auto            phase_start   = std::chrono::steady_clock::now();
  Preroute_result result{frame, rc, {}, {}};
  const auto      phase = [&](const char* name) {
    const auto now = std::chrono::steady_clock::now();
    result.timings_ms.emplace(name, std::chrono::duration<double, std::milli>(now - phase_start).count());
    phase_start = now;
  };
  phase("snapshot");
  if (options.deferred_pg && options.power_grid_occupancy) {
    throw std::invalid_argument("deferred PG cannot reuse a fixed power-grid occupancy snapshot");
  }
  std::optional<physical::Floorplan> completed_grid;
  // Deferred PG publishes the last grid that admitted every accepted route.
  const auto publish_grid = [&] {
    if (completed_grid) {
      result.frame.special_nets = std::move(completed_grid->special_nets);
      result.frame.vias         = std::move(completed_grid->vias);
      result.frame.terminals    = std::move(completed_grid->terminals);
    }
  };
  if (options.deferred_pg) {
    if (!options.straight_only || !frame.special_nets.empty()) {
      throw std::invalid_argument("deferred PG requires straight SAT routing and an unwired PG frame");
    }
    completed_grid = make_power_grid(tech, frame, reserve_power_grid_channels(tech, frame, *options.deferred_pg), nullptr, true);
    phase("power_grid_setup");
  }
  struct Setup_deadline {};
  const auto checkpoint = [&] {
    if (std::chrono::steady_clock::now() >= options.timing.deadline) {
      throw Setup_deadline{};
    }
  };
  std::optional<physical::Routing_occupancy> occupancy_storage;
  std::unique_ptr<Route_capacity>            capacity;
  std::unique_ptr<Pin_escape_guard>          pin_escapes;
  const bool has_signal_wires = std::ranges::any_of(frame.nets, [](const auto& net) { return !net.wires.empty(); });
  try {
    checkpoint();
    std::optional<physical::Routing_occupancy> static_occupancy;
    std::vector<physical::Shape>               existing_shapes;
    if (has_signal_wires) {
      for (const auto& net : frame.nets) {
        checkpoint();
        if (std::any_of(net.wires.begin(), net.wires.end(), [](const auto& path_group) { return path_group.type != "FIXED"; })) {
          throw std::invalid_argument("pre-routing requires existing signal wiring to be FIXED");
        }
      }
      // Capacity excludes signal wires from static obstructions, then reserves
      // their tracks separately. Decode the much larger cell/PG geometry once;
      // the working copy receives signal shapes for net-aware route checking.
      static_occupancy.emplace(physical::routing_occupancy(tech,
                                                           frame,
                                                           frame.dbu,
                                                           checkpoint,
                                                           options.power_grid_occupancy.get(),
                                                           physical::Signal_wires::exclude));
      occupancy_storage.emplace(*static_occupancy);
      checkpoint();
      for (const auto& net : frame.nets) {
        checkpoint();
        physical::visit_route_geometry(tech, frame, net, [&](const physical::Shape& shape) {
          checkpoint();
          occupancy_storage->insert({shape, net.name, true});
          existing_shapes.push_back(shape);
        });
      }
    } else {
      occupancy_storage.emplace(
          physical::routing_occupancy(tech, result.frame, frame.dbu, checkpoint, options.power_grid_occupancy.get()));
    }
    auto& occupancy = *occupancy_storage;
    if (options.deferred_pg) {
      for (const auto& metal : plan_power_grid(tech, frame, *options.deferred_pg).metal) {
        if (metal.followpin) {
          occupancy.insert({
              {metal.layer, metal.rect},
              metal.net
          });
        }
      }
    }
    phase("occupancy");
    if (!options.deferred_pg) {
      if (options.capacity_seed) {
        capacity = options.capacity_seed->instantiate(tech, result.frame, options.capacity, checkpoint);
      }
      if (!capacity) {
        capacity = std::make_unique<Route_capacity>(tech,
                                                    result.frame,
                                                    static_occupancy ? *static_occupancy : occupancy,
                                                    options.capacity,
                                                    checkpoint);
      }
      capacity->reserve_existing(existing_shapes);
    }
    phase("capacity");
    checkpoint();
  } catch (const Setup_deadline&) {
    // Construction is stage-private and timing has not been edited yet.
    // Preserve the original routes and return a diagnostic for every net.
    phase("setup_deadline");
    for (const auto& b : bindings) {
      const bool unaligned = options.straight_only && !options.straight_routes.contains(frame.nets[b.physical_net].name);
      result.records.push_back(preroute_record(frame, rc, b, unaligned ? "no retained two-pin SAT alignment" : "stage deadline"));
    }
    publish_grid();
    return result;
  }
  auto&               occupancy = *occupancy_storage;
  std::vector<size_t> realized_nets;
  if (has_signal_wires) {
    std::map<std::string, size_t> pin_net;
    for (size_t pin = 0; pin < snapshot.pin_labels.size(); ++pin) {
      if (!pin_net.emplace(snapshot.pin_labels[pin], design.pin2net.at(pin)).second) {
        throw std::invalid_argument("duplicate demand endpoint while matching existing wires");
      }
    }
    for (const auto& net : frame.nets) {
      if (net.wires.empty()) {
        continue;
      }
      std::set<std::string> pins;
      std::optional<size_t> demand_net;
      for (const auto& pin : net.connections) {
        const auto label = physical::connection_label(pin);
        const auto found = pin_net.find(label);
        if (!pins.insert(label).second || found == pin_net.end() || (demand_net && *demand_net != found->second)) {
          throw std::invalid_argument("existing fixed wire endpoints differ from routing demand");
        }
        demand_net = found->second;
      }
      if (!demand_net
          || pins.size() != size_t(design.flat_net2pin_start.at(*demand_net + 1) - design.flat_net2pin_start.at(*demand_net))) {
        throw std::invalid_argument("existing fixed wire does not cover its complete demand net");
      }
      realized_nets.push_back(*demand_net);
    }
  }
  Preroute_demand demand(design, options.demand, options.capacity, realized_nets);
  (void)demand.prepare();
  physical::Route_components route_components(result.frame);
  phase("search_setup");
  std::map<std::string, size_t> cells;
  for (size_t i = 0; i < frame.components.size(); ++i) {
    cells.emplace(frame.components[i].name, i);
  }

  // Route commitment locks entire endpoint instances. Include their other
  // connected data pins so a safe pin cannot strand an unrelated critical path.
  const auto            expired = [&] { return std::chrono::steady_clock::now() >= options.timing.deadline; };
  std::set<std::string> excluded_timing_pins;
  for (size_t n = 0; n < design.net_mask.size(); ++n) {
    if (expired()) {
      break;
    }
    if (!design.net_mask[n]) {
      for (int i = design.flat_net2pin_start[n]; i < design.flat_net2pin_start[n + 1]; ++i) {
        excluded_timing_pins.insert(snapshot.pin_labels.at(design.flat_net2pin[i]));
      }
    }
  }
  for (const auto& binding : bindings) {
    if (expired()) {
      break;
    }
    if (binding.clock || binding.reset) {
      for (const auto& pin : rc.nets[binding.rc_net].nodes) {
        excluded_timing_pins.insert(pin.pin);
      }
    }
  }
  std::map<std::string, std::set<std::string>> cell_timing_pins;
  for (const auto& net : rc.nets) {
    if (expired()) {
      break;
    }
    for (const auto& pin : net.nodes) {
      if (expired()) {
        break;
      }
      const auto separator = pin.pin.rfind(':');
      if (separator == std::string::npos || excluded_timing_pins.contains(pin.pin)) {
        continue;
      }
      const auto instance = pin.pin.substr(0, separator);
      if (cells.contains(instance)) {
        cell_timing_pins[instance].insert(pin.pin);
      }
    }
  }
  struct Candidate {
    Preroute_binding      binding;
    double                length, slack, setup_slack;
    std::set<std::string> lock_pins;
    bool                  critical = false;
  };
  std::vector<Candidate> candidates;
  for (const auto& b : bindings) {
    const auto& net    = frame.nets[b.physical_net];
    const auto  reject = [&](std::string reason) { result.records.push_back(preroute_record(frame, rc, b, std::move(reason))); };
    if (options.straight_only && !options.straight_routes.contains(net.name)) {
      reject("no retained two-pin SAT alignment");
      continue;
    }
    if (expired()) {
      reject("stage deadline");
      continue;
    }
    if (b.clock || b.reset || b.tie || b.dont_touch || snapshot.protects(net) || !design.net_mask[b.demand_net]) {
      reject("logical exclusion");
      continue;
    }
    if (!physical::unrouted_signal(net)) {
      reject("not an unwired signal net");
      continue;
    }
    bool                  eligible = true;
    std::set<std::string> physical_pins;
    for (const auto& p : net.connections) {
      const auto cell = cells.find(p.instance);
      if (cell == cells.end()) {
        eligible = false;
        break;
      }
      const auto& master = tech.macros.at(frame.components[cell->second].macro);
      if (master.klass != "CORE" || master.virtual_proxy) {
        eligible = false;
        break;
      }
      if (!physical_pins.insert(p.instance + ":" + p.pin).second) {
        throw std::invalid_argument("duplicate pre-route endpoint");
      }
    }
    if (!eligible) {
      reject("non-standard-cell endpoint");
      continue;
    }
    std::set<std::string> rc_pins;
    for (const auto& p : rc.nets[b.rc_net].nodes) {
      if (!p.pin.empty()) {
        rc_pins.insert(p.pin);
      }
    }
    if (physical_pins != rc_pins || !physical_pins.contains(rc.nets[b.rc_net].driver)) {
      throw std::invalid_argument("physical and timing pre-route endpoints differ");
    }
    std::set<std::string> demand_pins;
    for (int p = design.flat_net2pin_start[b.demand_net]; p < design.flat_net2pin_start[b.demand_net + 1]; ++p) {
      if (!demand_pins.insert(snapshot.pin_labels.at(design.flat_net2pin[p])).second) {
        throw std::invalid_argument("duplicate pre-route demand endpoint");
      }
    }
    if (physical_pins != demand_pins) {
      throw std::invalid_argument("physical and demand pre-route endpoints differ");
    }
    if (const auto reason = preroute_span_exclusion(design, b.demand_net, options); !reason.empty()) {
      reject(reason);
      continue;
    }
    const auto length = rc.nets[b.rc_net].wire_length_um;
    if (!std::isfinite(length) || length < 0) {
      throw std::invalid_argument("invalid pre-route estimated length");
    }
    auto lock_pins = physical_pins;
    for (const auto& pin : net.connections) {
      const auto& related = cell_timing_pins[pin.instance];
      lock_pins.insert(related.begin(), related.end());
    }
    double slack       = std::numeric_limits<double>::infinity();
    double setup_slack = std::numeric_limits<double>::infinity();
    for (const auto& p : lock_pins) {
      const auto t = timer.pin(p);
      if (!t.slack || !t.hold_slack || !std::isfinite(*t.slack) || !std::isfinite(*t.hold_slack)) {
        eligible = false;
        break;
      }
      setup_slack = std::min(setup_slack, *t.slack);
      slack       = std::min({slack, *t.slack - options.timing.setup_margin_ps, *t.hold_slack - options.timing.hold_margin_ps});
    }
    if (!eligible || slack < 0) {
      reject("initial setup/hold margin");
      continue;
    }
    candidates.push_back({b, length, slack, setup_slack, std::move(lock_pins)});
  }
  phase("candidate_selection");
  // A repeater tree is atomic. An ineligible branch already makes publication
  // impossible, so do not build the whole-frame escape guard or route siblings.
  if (require_all && candidates.size() != bindings.size()) {
    for (const auto& candidate : candidates) {
      result.records.push_back(preroute_record(frame, rc, candidate.binding, "tree rollback: ineligible sibling"));
    }
    candidates.clear();
  }
  if (!candidates.empty()) {
    try {
      checkpoint();
      if (completed_grid) {
        // Deferred channels remove supply shapes. Reserving escapes against the
        // complete grid is conservative when those channels are subsequently cut.
        auto full_grid_occupancy = physical::routing_occupancy(tech, *completed_grid, frame.dbu, checkpoint);
        pin_escapes              = std::make_unique<Pin_escape_guard>(tech,
                                                         *completed_grid,
                                                         full_grid_occupancy,
                                                         options.access_candidates,
                                                         checkpoint,
                                                         route_profile.access);
      } else {
        pin_escapes = std::make_unique<Pin_escape_guard>(tech,
                                                         result.frame,
                                                         occupancy,
                                                         options.access_candidates,
                                                         checkpoint,
                                                         route_profile.access);
      }
      phase("pin_escape_setup");
      checkpoint();
    } catch (const Setup_deadline&) {
      phase("setup_deadline");
      for (const auto& candidate : candidates) {
        result.records.push_back(preroute_record(frame, rc, candidate.binding, "stage deadline"));
      }
      candidates.clear();
    }
  }
  Signal_router router(tech, result.frame, occupancy, route_profile, pin_escapes.get());
  if (options.straight_only && options.critical_fraction > 0) {
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
      return a.setup_slack < b.setup_slack;
    });
    const auto count = size_t(std::ceil(options.critical_fraction * candidates.size()));
    for (size_t i = 0; i < count; ++i) {
      candidates[i].critical = true;
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
    return a.length != b.length ? a.length < b.length : a.slack > b.slack;
  });
  // Setup already required all existing signal wiring to be FIXED.
  std::set<std::string> locked;
  for (const auto& net : frame.nets) {
    if (net.wires.empty()) {
      continue;
    }
    for (const auto& pin : net.connections) {
      const auto cell = cells.find(pin.instance);
      if (cell == cells.end() || frame.components[cell->second].place.status != physical::Status::FIXED) {
        throw std::invalid_argument("existing fixed signal route has an unlocked endpoint");
      }
      locked.insert(pin.instance + ":" + pin.pin);
      const auto& related = cell_timing_pins[pin.instance];
      locked.insert(related.begin(), related.end());
    }
  }
  phase("eligibility");
  const auto mark = timer.mark();
  try {
    for (const auto& candidate : candidates) {
      const auto& b       = candidate.binding;
      auto&       net     = result.frame.nets[b.physical_net];
      auto        record  = preroute_record(frame, rc, b, {});
      const auto  attempt = [&] {
        if (expired()) {
          record.reason = "stage deadline";
          return;
        }
        const auto deadline    = std::min(options.timing.deadline, std::chrono::steady_clock::now() + options.per_net_budget);
        const auto net_expired = [&] {
          if (std::chrono::steady_clock::now() < deadline) {
            return false;
          }
          record.reason = "net deadline";
          return true;
        };
        // Physical access points of a proposal, in connection order.
        const auto rc_pins = [&](const Signal_route_geometry& geometry) {
          std::vector<Route_rc_pin> pins;
          for (size_t i = 0; i < net.connections.size(); ++i) {
            const auto& point = geometry.endpoints[i];
            pins.push_back({net.connections[i].instance + ":" + net.connections[i].pin, point.pin_layer, point.x, point.y});
          }
          return pins;
        };
        record.attempted           = true;
        auto       operation_start = std::chrono::steady_clock::now();
        const auto measured        = [&](const char* name) {
          const auto now           = std::chrono::steady_clock::now();
          record.timings_ms[name] += std::chrono::duration<double, std::milli>(now - operation_start).count();
          operation_start          = now;
        };
        const auto remaining = demand.without(b.demand_net);
        measured("remaining_demand");
        if (net_expired()) {
          return;
        }
        if (capacity) {
          const auto demand_check = capacity->precheck_demand(remaining.remaining);
          measured("demand_precheck");
          if (!demand_check.legal) {
            record.reason          = demand_check.reason;
            record.capacity_detail = demand_check.detail;
            return;
          }
          if (net_expired()) {
            return;
          }
        }
        Signal_route_request request{
                    {options.access_candidates, options.patterns, deadline},
                    {},
            candidate.critical
        };
        if (options.straight_only) {
          request.corridor = options.straight_routes.at(net.name);
        }
        auto proposals = router.try_route(net, request);
        measured("geometry");
        record.patterns = proposals.patterns_examined;
        if (proposals.candidates.empty()) {
          record.reason = proposals.reason;
          return;
        }
        if (net_expired()) {
          return;
        }
        struct Layer_candidate {
          Signal_route_geometry geometry;
          double                score;
        };
        std::vector<Layer_candidate> alternatives;
        for (auto& proposed : proposals.candidates) {
          alternatives.push_back({std::move(proposed), -std::numeric_limits<double>::infinity()});
        }
        Signal_route_geometry geometry;
        record.critical_layer_search = candidate.critical;
        if (candidate.critical) {
          // Compare actual layer/via RC in the timer, including driver loading.
          // A locked net's lower slack must not mask improvements on this net.
          for (auto& alternative : alternatives) {
            if (net_expired()) {
              return;
            }
            auto trial = net;
            trial.wires.push_back({"FIXED", alternative.geometry.paths});
            try {
              const auto pins   = rc_pins(alternative.geometry);
              const auto actual = routed_rc(tech, result.frame, trial, rc.nets[b.rc_net].driver, pins, wire, &route_components);
              auto       limits = options.timing;
              limits.deadline   = deadline;
              const auto timing = check_preroute_timing(timer, result.parasitics.nets[b.rc_net], actual, {}, limits);
              if (timing.legal && timing.net_setup_slack_ps) {
                alternative.score = *timing.net_setup_slack_ps;
              }
            } catch (const std::invalid_argument&) {
              // Unsupported characterization never makes an upper layer win.
            }
          }
          std::stable_sort(alternatives.begin(), alternatives.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.score > rhs.score;
          });
          measured("layer_selection");
        }
        record.layer_candidates = alternatives.size();
        for (auto& alternative : alternatives) {
          geometry            = std::move(alternative.geometry);
          const auto evaluate = [&] {
            std::optional<physical::Floorplan> rule_frame;
            if (!geometry.nondefault_rules.empty()) {
              rule_frame = result.frame;
              apply_signal_route_rules(*rule_frame, geometry);
            }
            const auto& evaluation_frame = rule_frame ? *rule_frame : result.frame;
            auto        trial_net        = net;
            trial_net.wires.push_back({"FIXED", geometry.paths});
            const auto shapes       = physical::route_geometry(tech, evaluation_frame, trial_net);
            const auto escape_trial = pin_escapes->check(shapes, net.name, net_expired);
            measured("pin_escape_check");
            if (!escape_trial.legal) {
              record.reason = escape_trial.reason;
              return;
            }
            std::optional<Route_capacity_trial> preliminary;
            if (capacity) {
              preliminary          = capacity->precheck(shapes);
              const auto& precheck = *preliminary;
              measured("capacity_precheck");
              if (!precheck.legal) {
                record.reason          = precheck.reason;
                record.capacity_detail = precheck.detail;
                return;
              }
            }
            if (net_expired()) {
              return;
            }
            physical::Rc_net actual;
            try {
              actual = routed_rc(tech,
                                 evaluation_frame,
                                 trial_net,
                                 rc.nets[b.rc_net].driver,
                                 rc_pins(geometry),
                                 wire,
                                 rule_frame ? nullptr : &route_components);
            } catch (const std::invalid_argument& e) {
              record.reason = e.what();
              return;
            }
            measured("rc");
            if (net_expired()) {
              return;
            }
            std::optional<physical::Floorplan> trial_grid;
            std::unique_ptr<Route_capacity>    trial_capacity;
            auto*                              active_capacity = capacity.get();
            if (options.deferred_pg) {
              auto pg_input                 = evaluation_frame;
              pg_input.nets[b.physical_net] = trial_net;
              try {
                const auto profile                        = reserve_power_grid_channels(tech, pg_input, *options.deferred_pg);
                trial_grid                                = make_power_grid(tech, pg_input, profile, nullptr, true);
                auto                         static_frame = *trial_grid;
                std::vector<physical::Shape> realized;
                for (size_t n = 0; n < static_frame.nets.size(); ++n) {
                  auto& wired = static_frame.nets[n];
                  if (n != b.physical_net) {
                    auto prior = physical::route_geometry(tech, *trial_grid, wired);
                    realized.insert(realized.end(), prior.begin(), prior.end());
                  }
                  wired.wires.clear();
                }
                const auto blocked = physical::routing_occupancy(tech, static_frame, frame.dbu);
                trial_capacity     = std::make_unique<Route_capacity>(tech, static_frame, blocked, options.capacity);
                trial_capacity->reserve_existing(realized);
              } catch (const std::invalid_argument& error) {
                record.reason = std::string("deferred PG: ") + error.what();
                measured("power_grid");
                return;
              }
              active_capacity = trial_capacity.get();
              measured("power_grid");
            }
            if (net_expired()) {
              return;
            }
            const auto reservation = preliminary && active_capacity == capacity.get()
                                                 ? active_capacity->check_remaining(*preliminary, remaining.remaining)
                                                 : active_capacity->check(shapes, remaining.remaining);
            measured("capacity");
            if (!reservation.legal) {
              record.reason          = reservation.reason;
              record.capacity_detail = reservation.detail;
              return;
            }
            auto limits     = options.timing;
            limits.deadline = deadline;
            std::vector<std::string> protected_pins(locked.begin(), locked.end());
            protected_pins.insert(protected_pins.end(), candidate.lock_pins.begin(), candidate.lock_pins.end());
            const auto timing = check_preroute_timing(timer, result.parasitics.nets[b.rc_net], actual, protected_pins, limits);
            measured("timing");
            if (!timing.legal) {
              record.reason = timing.reason;
              return;
            }
            if (net_expired()) {
              return;
            }
            // All rejectable gates passed. These objects are stage-private; any
            // exception during commit aborts the whole stage and restores timing.
            apply_signal_route_rules(result.frame, geometry);
            timer.set_net(actual);
            std::vector<physical::Occupied_shape> additions;
            for (const auto& shape : shapes) {
              additions.push_back({shape, net.name, true});
            }
            occupancy.insert_many(additions);
            pin_escapes->commit(escape_trial);
            active_capacity->commit(reservation);
            if (trial_grid) {
              completed_grid = std::move(trial_grid);
            }
            demand.commit(remaining);
            net                              = std::move(trial_net);
            result.parasitics.nets[b.rc_net] = std::move(actual);
            for (const auto& p : net.connections) {
              result.frame.components[cells.at(p.instance)].place.status = physical::Status::FIXED;
            }
            locked.insert(candidate.lock_pins.begin(), candidate.lock_pins.end());
            result.parasitics.identity.clear();    // Host assigns the combined final-placement/cache identity.
            result.parasitics.approximate = true;  // Characterized ground-C model, no coupling extraction.
            record.reason.clear();
            record.capacity_detail.clear();
            record.timing_at_acceptance = timing;
            record.accepted             = true;
            record.vias                 = geometry.vias;
            record.wire_length_um       = geometry.wire_length_um;
            record.layer_wire_length_um = geometry.layer_wire_length_um;
          };
          evaluate();
          if (record.accepted || net_expired()) {
            break;
          }
        }
      };
      attempt();
      result.records.push_back(std::move(record));
    }
  } catch (...) {
    timer.rollback(mark);
    throw;
  }
  timer.rollback(mark);
  phase("search_and_timing");
  publish_grid();
  return result;
}
Preroute_result preroute(const physical::Technology& tech, const physical::Floorplan& frame, const physical::Parasitics& rc,
                         const Preroute_design& snapshot, const std::vector<Preroute_binding>& bindings,
                         const Route_technology_rc& wire, timing::Whatif& timer, const Preroute_options& options) {
  return preroute_impl(tech, frame, rc, snapshot, bindings, wire, timer, options, false);
}
Preroute_tree_trial preroute_tree(const physical::Technology& tech, const physical::Floorplan& frame,
                                  const physical::Parasitics& rc, const Preroute_design& snapshot,
                                  const std::vector<Preroute_binding>& branches, const Route_technology_rc& wire,
                                  timing::Whatif& timer, const Preroute_options& options) {
  if (branches.empty()) {
    throw std::invalid_argument("pre-route tree requires every branch binding");
  }
  auto routed = preroute_impl(tech, frame, rc, snapshot, branches, wire, timer, options, true);
  if (routed.records.size() != branches.size()) {
    throw std::runtime_error("pre-route tree branch accounting mismatch");
  }
  Preroute_tree_trial trial;
  trial.timings_ms = routed.timings_ms;
  for (const auto& record : routed.records) {
    if (!record.accepted) {
      trial.reason = "tree branch " + record.net + ": " + record.reason;
      if (!record.capacity_detail.empty()) {
        trial.capacity_detail = "tree branch " + record.net + ": " + record.capacity_detail;
      }
      trial.rejected_records = std::move(routed.records);
      for (auto& rejected : trial.rejected_records) {
        if (rejected.accepted) {
          rejected.accepted = false;
          rejected.reason   = "tree rollback";
        }
      }
      return trial;
    }
  }
  trial.result = std::move(routed);
  return trial;
}
Preroute_tree_trial preroute_repeated_tree(const physical::Technology& tech, const physical::Floorplan& frame,
                                           const physical::Parasitics& rc, const Preroute_design& snapshot,
                                           const std::vector<Preroute_binding>& branches, const Route_technology_rc& wire,
                                           timing::Whatif& timer, const Preroute_options& options, const physical::Rc_net& original,
                                           const std::vector<Preroute_repeater>& repeaters,
                                           const std::vector<std::string>&       locked) {
  const auto branch_rc = [&](const physical::Parasitics& parasitics) {
    std::vector<physical::Rc_net> nets;
    for (const auto& binding : branches) {
      nets.push_back(parasitics.nets.at(binding.rc_net));
    }
    return nets;
  };
  for (const auto& repeater : repeaters) {
    const auto cell = std::find_if(frame.components.begin(), frame.components.end(), [&](const auto& component) {
      return component.name == repeater.instance;
    });
    if (cell == frame.components.end() || cell->macro != repeater.master || cell->place.status != physical::Status::FIXED) {
      throw std::invalid_argument("pre-route repeater requires its reserved FIXED physical component");
    }
  }
  auto       trial_rc = branch_rc(rc);
  const auto initial  = check_preroute_repeater_timing(timer, original, trial_rc, repeaters, locked, options.timing);
  if (!initial.legal) {
    return {{}, {}, "repeater proposal: " + initial.reason};
  }
  Preroute_tree_trial result;
  const auto          mark = timer.mark();
  try {
    for (const auto& repeater : repeaters) {
      timer.add_cell(repeater.instance, repeater.master);
    }
    for (const auto& net : trial_rc) {
      timer.set_net(net);
    }
    result = preroute_tree(tech, frame, rc, snapshot, branches, wire, timer, options);
  } catch (...) {
    timer.rollback(mark);
    throw;
  }
  timer.rollback(mark);
  if (!result.result) {
    return result;
  }
  const auto final
      = check_preroute_repeater_timing(timer, original, branch_rc(result.result->parasitics), repeaters, locked, options.timing);
  if (!final.legal) {
    result.reason           = "repeater routed timing: " + final.reason;
    result.rejected_records = std::move(result.result->records);
    for (auto& record : result.rejected_records) {
      record.accepted = false;
      record.reason   = "tree rollback: " + final.reason;
    }
    result.result.reset();
  }
  return result;
}
}  // namespace livehd::placement
