// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <stdexcept>

namespace livehd::timing::fast {
namespace {
constexpr float inf = std::numeric_limits<float>::infinity();

struct Combo {
  int8_t in, out;
};
constexpr Combo positive[2] = {
    {0, 0},
    {1, 1}
};
constexpr Combo negative[2] = {
    {0, 1},
    {1, 0}
};
constexpr Combo any[4] = {
    {0, 0},
    {0, 1},
    {1, 0},
    {1, 1}
};
constexpr Combo launch[2] = {
    {0, 0},
    {0, 1}
};  // the rising clock edge launches both output transitions

// The (input, output) transition pairs an arc can carry, in the order its delays are stored.
std::span<const Combo> combos_of(const Arc& arc) {
  if (arc.kind == Arc_kind::launch_rise) {
    return launch;
  }
  if (arc.kind != Arc_kind::combinational) {
    return {};
  }
  switch (arc.sense) {
    case Sense::positive : return positive;
    case Sense::negative : return negative;
    case Sense::non_unate: return any;
  }
  return any;
}

bool timed_arc(const Arc& arc) { return arc.kind == Arc_kind::combinational || arc.kind == Arc_kind::launch_rise; }
}  // namespace

Engine::Engine(const Netlist& netlist, Constraints constraints, Options options)
    : netlist_(netlist), types_(netlist.types()), constraints_(std::move(constraints)), options_(std::move(options)) {
  corners_ = types_.corners();
  if (corners_ > 31) {
    throw std::invalid_argument("at most 31 corners");
  }
  hold_corner_ = options_.hold_corner == no_index ? corners_ - 1 : options_.hold_corner;
  if (hold_corner_ >= corners_ || options_.path_corner >= corners_) {
    throw std::invalid_argument("hold or path corner out of range");
  }
  options_.depth_margin.resize(corners_, 0.f);
  prepare();
}

void Engine::set_wire(Wire_model wire) {
  wire_ = std::move(wire);
  compute_loads();
}

float Engine::wire_delay(uint32_t pin, size_t corner) const {
  return wire_.sink_delay_ps.empty() ? 0.f : wire_.sink_delay_ps[static_cast<size_t>(pin) * corners_ + corner];
}

float Engine::net_load(uint32_t net, size_t corner, bool early) const {
  return early ? early_load_[net] : load_[static_cast<size_t>(net) * corners_ + corner];
}

void Engine::prepare() {
  const size_t pins = netlist_.pins(), cells = netlist_.cells();
  const size_t C = corners_;

  // Layout of the arc delays kept for the backward walk and for incremental updates.
  arc_offset_.assign(types_.arc_total(), 0);
  type_block_.assign(types_.types(), 0);
  for (uint32_t t = 0; t < types_.types(); ++t) {
    uint32_t   off  = 0;
    const auto arcs = types_.arcs(t);
    for (size_t j = 0; j < arcs.size(); ++j) {
      arc_offset_[types_.type(t).first_arc + j]  = off;
      off                                       += static_cast<uint32_t>(combos_of(arcs[j]).size());
    }
    type_block_[t] = off;
  }
  cell_base_.assign(cells + 1, 0);
  for (uint32_t c = 0; c < cells; ++c) {
    cell_base_[c + 1] = cell_base_[c] + type_block_[netlist_.type_of(c)] * static_cast<uint32_t>(C);
  }
  arc_delay_.assign(cell_base_[cells], 0.f);

  arrival_.assign(pins * C * 2, -inf);
  slew_.assign(pins * C * 2, 0.f);
  required_.assign(pins * C * 2, inf);
  depth_.assign(pins * C * 2, 0);
  early_arrival_.assign(pins * 2, inf);
  early_slew_.assign(pins * 2, 0.f);
  criticality_.assign(pins, 0.f);
  predecessor_.assign(pins * 2, {});
  clock_pin_.assign(pins, 0);
  pin_clock_.assign(pins, 0);
  cut_.assign(pins, 0);
  excluded_.assign(pins, 0);
  port_load_.assign(cells, 0.f);
  port_delay_.assign(cells, 0.f);

  for (const auto pin : constraints_.cut_pins) {
    if (pin >= pins) {
      throw std::invalid_argument("cut pin out of range");
    }
    cut_[pin] = 1;
  }
  for (const auto pin : constraints_.excluded_endpoints) {
    if (pin >= pins) {
      throw std::invalid_argument("excluded endpoint out of range");
    }
    excluded_[pin] = 1;
  }
  for (const auto& out : constraints_.outputs) {
    for (const auto& port : netlist_.ports()) {
      if (!port.input && port.name == out.port) {
        port_load_[port.cell]  = out.load_ff;
        port_delay_[port.cell] = out.delay_ps;
      }
    }
  }
  mark_clock_network();
  compute_loads();

  // Endpoints: the data pins of clocked flip-flops, and the primary outputs that have an output delay.
  const auto clock_index = [&](const std::string& name) -> uint32_t {
    if (name.empty()) {
      return 0;
    }
    for (size_t i = 0; i < constraints_.clocks.size(); ++i) {
      if (constraints_.clocks[i].name == name) {
        return static_cast<uint32_t>(i);
      }
    }
    throw std::invalid_argument("unknown clock: " + name);
  };
  endpoints_.clear();
  report_.unclocked_endpoints = 0;
  report_.unclocked_flops     = 0;
  for (uint32_t c = 0; c < cells; ++c) {
    const auto  type_id = netlist_.type_of(c);
    const auto& type    = types_.type(type_id);
    if (type.latch || type.memory || type.clock_gate) {
      continue;
    }
    if (type_id == Cell_types::output_port_type) {
      continue;  // below
    }
    bool clocked = true, any_check = false;
    for (uint32_t local = 0; local < type.pin_count; ++local) {
      bool     has_setup = false, has_clock = true;
      uint32_t clock_pin = no_index;
      for (const auto& arc : types_.arcs_into(type_id, local)) {
        if (arc.kind == Arc_kind::setup_rise || arc.kind == Arc_kind::hold_rise) {
          has_setup = has_setup || arc.kind == Arc_kind::setup_rise;
          clock_pin = netlist_.pin(c, arc.from);
        }
      }
      if (clock_pin == no_index) {
        continue;
      }
      any_check = true;
      has_clock = clock_pin_[clock_pin] == 2;
      clocked   = clocked && has_clock;
      if (has_setup && !excluded_[netlist_.pin(c, local)]) {
        if (has_clock) {
          endpoints_.push_back({netlist_.pin(c, local), pin_clock_[clock_pin], false});
        } else {
          ++report_.unclocked_endpoints;
        }
      }
    }
    if (any_check && !clocked) {
      ++report_.unclocked_flops;
    }
  }
  for (const auto& out : constraints_.outputs) {
    for (const auto& port : netlist_.ports()) {
      if (!port.input && port.name == out.port) {
        const auto pin = netlist_.pin(port.cell, 0);
        if (!excluded_[pin] && !netlist_.untimed(pin)) {
          endpoints_.push_back({pin, clock_index(out.clock), true});
        }
      }
    }
  }
  std::sort(endpoints_.begin(), endpoints_.end(), [](const Endpoint& a, const Endpoint& b) { return a.pin < b.pin; });
  endpoint_slack_.assign(endpoints_.size(), inf);
  endpoint_path_slack_.assign(endpoints_.size(), inf);
  endpoint_path_transition_.assign(endpoints_.size(), 0);
  clock_wns_.assign(std::max<size_t>(1, constraints_.clocks.size()) * C, 0.f);

  report_.unsupported = constraints_.unsupported;
  for (const auto& line : netlist_.diagnostics()) {
    report_.unsupported.push_back(line);
  }
  if (types_.ignored_arcs() != 0) {
    report_.unsupported.push_back(
        std::format("{} Liberty arcs are not timed (see Cell_types::diagnostics)", types_.ignored_arcs()));
  }
  if (report_.unclocked_flops != 0) {
    report_.unsupported.push_back(
        std::format("{} flip-flops have no ideal clock from a clock port: not timed", report_.unclocked_flops));
  }
  if (constraints_.clocks.empty() && !endpoints_.empty()) {
    throw std::invalid_argument("endpoints without a clock");
  }
}

void Engine::mark_clock_network() {
  // The ideal clock: from each clock port through buffers and gates to the clock pins of flip-flops. Nothing
  // inside it is timed as data; a flip-flop's clock pin is launched at time 0 with the clock's transition.
  std::vector<uint32_t> work;
  const auto            reach = [&](uint32_t pin, uint32_t clock) {
    if (clock_pin_[pin] != 0) {
      return;
    }
    const auto  cell   = netlist_.cell_of(pin);
    const auto& type   = types_.type(netlist_.type_of(cell));
    const auto& pin_ty = netlist_.pin_type(pin);
    pin_clock_[pin]    = static_cast<uint8_t>(clock);
    if (pin_ty.clock && type.sequential && !type.clock_gate) {
      clock_pin_[pin] = 2;
      return;
    }
    clock_pin_[pin] = 1;
    work.push_back(pin);
  };
  for (size_t c = 0; c < constraints_.clocks.size(); ++c) {
    bool found = false;
    for (const auto& port : netlist_.ports()) {
      if (port.input && port.name == constraints_.clocks[c].port) {
        found = true;
        reach(netlist_.pin(port.cell, 0), static_cast<uint32_t>(c));
      }
    }
    if (!found) {
      throw std::invalid_argument("clock port not found: " + constraints_.clocks[c].port);
    }
    while (!work.empty()) {
      const auto pin = work.back();
      work.pop_back();
      const auto clock = pin_clock_[pin];
      if (netlist_.net_of(pin) != no_index && netlist_.is_driver(pin)) {
        for (const auto sink : netlist_.sinks_of(netlist_.net_of(pin))) {
          reach(sink, clock);
        }
      }
      const auto cell  = netlist_.cell_of(pin);
      const auto local = netlist_.local_of(pin);
      for (const auto& arc : types_.arcs(netlist_.type_of(cell))) {
        if (arc.kind == Arc_kind::combinational && arc.from == local) {
          reach(netlist_.pin(cell, arc.to), clock);
        }
      }
    }
  }
}

void Engine::compute_loads() {
  const size_t C = corners_, nets = netlist_.nets();
  load_.assign(nets * C, 0.f);
  early_load_.assign(nets, 0.f);
  for (uint32_t net = 0; net < nets; ++net) {
    for (const auto sink : netlist_.sinks_of(net)) {
      const auto cell  = netlist_.cell_of(sink);
      const auto type  = netlist_.type_of(cell);
      const auto local = netlist_.local_of(sink);
      if (type == Cell_types::output_port_type) {
        for (size_t k = 0; k < C; ++k) {
          load_[net * C + k] += port_load_[cell];
        }
        early_load_[net] += port_load_[cell];
        continue;
      }
      for (size_t k = 0; k < C; ++k) {
        load_[net * C + k] += std::max(types_.pin_cap(type, local, k, 0), types_.pin_cap(type, local, k, 1));
      }
      early_load_[net] += std::min(types_.pin_cap(type, local, hold_corner_, 0), types_.pin_cap(type, local, hold_corner_, 1));
    }
    if (!wire_.net_cap_ff.empty()) {
      for (size_t k = 0; k < C; ++k) {
        load_[net * C + k] += wire_.net_cap_ff[net * C + k];
      }
      early_load_[net] += wire_.net_cap_ff[net * C + hold_corner_];
    }
  }
}

void Engine::seed_late() {
  std::fill(arrival_.begin(), arrival_.end(), -inf);
  std::fill(slew_.begin(), slew_.end(), 0.f);
  std::fill(depth_.begin(), depth_.end(), 0);
  std::fill(predecessor_.begin(), predecessor_.end(), Predecessor{});
  const size_t C = corners_;
  for (const auto& port : netlist_.ports()) {
    const auto pin = netlist_.pin(port.cell, 0);
    if (!port.input || clock_pin_[pin] != 0 || cut_[pin]) {
      continue;
    }
    float delay = 0, slew = constraints_.default_input_slew_ps;
    for (const auto& in : constraints_.inputs) {
      if (in.port == port.name) {
        delay = in.delay_ps;
        slew  = in.slew_ps;
      }
    }
    for (size_t k = 0; k < C; ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        arrival_[index(pin, k, t)] = delay;
        slew_[index(pin, k, t)]    = slew;
      }
    }
  }
  for (uint32_t pin = 0; pin < netlist_.pins(); ++pin) {
    if (clock_pin_[pin] == 2 && !cut_[pin]) {
      const float clock_slew = constraints_.clocks[pin_clock_[pin]].slew_ps;
      for (size_t k = 0; k < C; ++k) {
        arrival_[index(pin, k, 0)] = 0.f;  // the rising edge; the falling edge launches nothing
        slew_[index(pin, k, 0)]    = clock_slew;
        slew_[index(pin, k, 1)]    = clock_slew;
      }
    }
  }
}

void Engine::forward_late() {
  const size_t C = corners_;
  seed_late();
  for (const uint32_t p : netlist_.order()) {
    if (netlist_.untimed(p) || clock_pin_[p] != 0 || cut_[p]) {
      continue;
    }
    const auto net = netlist_.net_of(p);
    if (net != no_index && !netlist_.is_driver(p)) {
      const auto driver = netlist_.driver_of(net);
      for (size_t k = 0; k < C; ++k) {
        const float wd = wire_delay(p, k);
        const float ws = wire_.sink_slew_ps.empty() ? 0.f : wire_.sink_slew_ps[static_cast<size_t>(p) * C + k];
        for (unsigned t = 0; t < 2; ++t) {
          const auto from = index(driver, k, t), to = index(p, k, t);
          arrival_[to] = arrival_[from] + wd;
          slew_[to]    = ws == 0.f ? slew_[from] : std::hypot(slew_[from], ws);
          depth_[to]   = depth_[from];
          if (k == options_.path_corner) {
            predecessor_[p * 2 + t] = {driver, static_cast<uint8_t>(t)};
          }
        }
      }
      continue;
    }
    const auto cell  = netlist_.cell_of(p);
    const auto local = netlist_.local_of(p);
    const auto type  = netlist_.type_of(cell);
    const auto arcs  = types_.arcs_into(type, local);
    if (arcs.empty()) {
      continue;
    }
    const auto* type_arcs = types_.arcs(type).data();
    for (size_t k = 0; k < C; ++k) {
      const float load = net == no_index ? 0.f : net_load(net, k, false);
      for (const auto& arc : arcs) {
        if (!timed_arc(arc)) {
          continue;
        }
        const auto from_pin = netlist_.pin(cell, arc.from);
        if (cut_[from_pin]) {
          continue;
        }
        const auto combos = combos_of(arc);
        const auto offset
            = cell_base_[cell] + (arc_offset_[types_.type(type).first_arc + (&arc - type_arcs)]) * C + k * combos.size();
        for (size_t m = 0; m < combos.size(); ++m) {
          const auto in = static_cast<unsigned>(combos[m].in), out = static_cast<unsigned>(combos[m].out);
          const auto from = index(from_pin, k, in), to = index(p, k, out);
          if (arrival_[from] == -inf) {
            continue;
          }
          const auto delay_table = types_.table(arc, k, 0, out);
          if (delay_table == no_index) {
            continue;
          }
          const Query query{slew_[from], load, 0.f, 0.f};
          const float delay      = types_.lookup(delay_table, query);
          const auto  slew_table = types_.table(arc, k, 1, out);
          const float out_slew   = slew_table == no_index ? 0.f : std::max(0.f, types_.lookup(slew_table, query));
          arc_delay_[offset + m] = delay;
          const float at         = arrival_[from] + delay;
          if (at > arrival_[to]) {
            arrival_[to] = at;
            depth_[to]   = static_cast<uint16_t>(std::min<uint32_t>(depth_[from] + 1, 65535));
            if (k == options_.path_corner) {
              predecessor_[p * 2 + out] = {from_pin, static_cast<uint8_t>(in)};
            }
          }
          slew_[to] = std::max(slew_[to], out_slew);
        }
      }
    }
  }
}

void Engine::seed_endpoints(bool rebased) {
  std::fill(required_.begin(), required_.end(), inf);
  const size_t C = corners_;
  for (const auto& endpoint : endpoints_) {
    const auto  pin   = endpoint.pin;
    const auto& clock = constraints_.clocks[endpoint.clock];
    const auto  cell  = netlist_.cell_of(pin);
    for (size_t k = 0; k < C; ++k) {
      const float ref = rebased ? std::min(0.f, clock_wns_[endpoint.clock * C + k]) : 0.f;
      for (unsigned t = 0; t < 2; ++t) {
        const auto at = index(pin, k, t);
        if (arrival_[at] == -inf) {
          continue;
        }
        float requirement = inf;
        if (endpoint.port) {
          requirement = clock.period_ps - port_delay_[cell];
        } else {
          float      setup = -inf;
          const auto type  = netlist_.type_of(cell);
          for (const auto& arc : types_.arcs_into(type, netlist_.local_of(pin))) {
            if (arc.kind != Arc_kind::setup_rise) {
              continue;
            }
            const auto table = types_.table(arc, k, 2, t);
            if (table == no_index) {
              continue;
            }
            const auto  clock_pin = netlist_.pin(cell, arc.from);
            const Query query{0.f, 0.f, slew_[index(clock_pin, k, 0)], slew_[at]};
            setup = std::max(setup, types_.lookup(table, query));
          }
          if (setup == -inf) {
            continue;
          }
          requirement = clock.period_ps - setup;
        }
        requirement        -= clock.uncertainty_ps;
        const float margin  = options_.depth_margin[k];
        if (margin > 0.f) {
          requirement -= margin * arrival_[at] / std::sqrt(static_cast<float>(std::max<uint16_t>(depth_[at], 1)));
        }
        required_[at] = std::min(required_[at], requirement - ref);
      }
    }
  }
}

void Engine::backward() {
  const size_t C     = corners_;
  const auto   order = netlist_.order();
  for (size_t i = order.size(); i-- > 0;) {
    const uint32_t p = order[i];
    if (netlist_.untimed(p) || clock_pin_[p] != 0 || cut_[p]) {
      continue;
    }
    const auto net = netlist_.net_of(p);
    if (net != no_index && netlist_.is_driver(p)) {
      for (size_t k = 0; k < C; ++k) {
        for (unsigned t = 0; t < 2; ++t) {
          float requirement = required_[index(p, k, t)];
          for (const auto sink : netlist_.sinks_of(net)) {
            if (netlist_.untimed(sink) || clock_pin_[sink] != 0) {
              continue;
            }
            requirement = std::min(requirement, required_[index(sink, k, t)] - wire_delay(sink, k));
          }
          required_[index(p, k, t)] = requirement;
        }
      }
    }
    const auto cell  = netlist_.cell_of(p);
    const auto local = netlist_.local_of(p);
    const auto type  = netlist_.type_of(cell);
    const auto arcs  = types_.arcs_into(type, local);
    if (arcs.empty()) {
      continue;
    }
    const auto* type_arcs = types_.arcs(type).data();
    for (size_t k = 0; k < C; ++k) {
      for (const auto& arc : arcs) {
        if (arc.kind != Arc_kind::combinational) {
          continue;  // a launch arc starts at a clock pin, which has no required time
        }
        const auto from_pin = netlist_.pin(cell, arc.from);
        const auto combos   = combos_of(arc);
        const auto offset
            = cell_base_[cell] + (arc_offset_[types_.type(type).first_arc + (&arc - type_arcs)]) * C + k * combos.size();
        for (size_t m = 0; m < combos.size(); ++m) {
          const auto out_required = required_[index(p, k, static_cast<unsigned>(combos[m].out))];
          if (out_required == inf) {
            continue;
          }
          auto& in_required = required_[index(from_pin, k, static_cast<unsigned>(combos[m].in))];
          in_required       = std::min(in_required, out_required - arc_delay_[offset + m]);
        }
      }
    }
  }
}

void Engine::measure_endpoints() {
  const size_t C = corners_, clocks = std::max<size_t>(1, constraints_.clocks.size());
  std::fill(clock_wns_.begin(), clock_wns_.end(), inf);
  std::vector<float>  corner_wns(C, inf), corner_tns(C, 0.f), clock_tns(clocks, 0.f), clock_wns(clocks, inf);
  std::vector<size_t> clock_endpoints(clocks, 0);
  for (size_t e = 0; e < endpoints_.size(); ++e) {
    const auto& endpoint    = endpoints_[e];
    float       worst       = inf;
    endpoint_path_slack_[e] = inf;
    for (size_t k = 0; k < C; ++k) {
      float slack_k = inf;
      for (unsigned t = 0; t < 2; ++t) {
        const auto at = index(endpoint.pin, k, t);
        if (arrival_[at] == -inf || required_[at] == inf) {
          continue;
        }
        const float s = required_[at] - arrival_[at];
        slack_k       = std::min(slack_k, s);
        if (k == options_.path_corner && s < endpoint_path_slack_[e]) {
          endpoint_path_slack_[e]      = s;
          endpoint_path_transition_[e] = static_cast<uint8_t>(t);
        }
      }
      if (slack_k == inf) {
        continue;
      }
      clock_wns_[endpoint.clock * C + k] = std::min(clock_wns_[endpoint.clock * C + k], slack_k);
      if (options_.setup_corners >> k & 1u) {
        worst          = std::min(worst, slack_k);
        corner_wns[k]  = std::min(corner_wns[k], slack_k);
        corner_tns[k] += std::min(0.f, slack_k);
      }
    }
    endpoint_slack_[e] = worst;
    if (worst != inf) {
      ++clock_endpoints[endpoint.clock];
      clock_wns[endpoint.clock]  = std::min(clock_wns[endpoint.clock], worst);
      clock_tns[endpoint.clock] += std::min(0.f, worst);
    }
  }
  // A clock-corner without a timed endpoint rebases nothing.
  for (auto& wns : clock_wns_) {
    if (wns == inf) {
      wns = 0.f;
    }
  }
  report_.clocks.assign(constraints_.clocks.size(), {});
  report_.endpoints = 0;
  report_.wns_ps    = inf;
  report_.tns_ps    = 0.f;
  for (size_t c = 0; c < constraints_.clocks.size(); ++c) {
    auto& out                 = report_.clocks[c];
    out.name                  = constraints_.clocks[c].name;
    out.period_ps             = constraints_.clocks[c].period_ps;
    out.endpoints             = clock_endpoints[c];
    out.wns_ps                = clock_endpoints[c] == 0 ? 0.f : clock_wns[c];
    out.tns_ps                = clock_tns[c];
    out.achievable_period_ps  = out.period_ps - out.wns_ps;
    report_.endpoints        += clock_endpoints[c];
    if (clock_endpoints[c] != 0) {
      report_.wns_ps = std::min(report_.wns_ps, out.wns_ps);
    }
    report_.tns_ps += out.tns_ps;
  }
  if (report_.wns_ps == inf) {
    report_.wns_ps = 0.f;
  }
  report_.corners.assign(C, {});
  for (size_t k = 0; k < C; ++k) {
    report_.corners[k] = {types_.corner_name(k), corner_wns[k] == inf ? 0.f : corner_wns[k], corner_tns[k]};
  }
}

void Engine::criticality_pass() {
  const size_t C       = corners_;
  float        longest = 0.f;
  for (const auto& clock : report_.clocks) {
    longest = std::max(longest, clock.achievable_period_ps);
  }
  const float window = std::max(options_.window_fraction * longest, 1e-3f);
  for (uint32_t p = 0; p < netlist_.pins(); ++p) {
    float best = 0.f;
    for (size_t k = 0; k < C; ++k) {
      if (!(options_.setup_corners >> k & 1u)) {
        continue;
      }
      for (unsigned t = 0; t < 2; ++t) {
        const auto at = index(p, k, t);
        if (arrival_[at] == -inf || required_[at] == inf) {
          continue;
        }
        best = std::max(best, std::clamp(1.f - (required_[at] - arrival_[at]) / window, 0.f, 1.f));
      }
    }
    criticality_[p] = best;
  }
}

void Engine::rank() {
  forward_late();
  seed_endpoints(false);
  measure_endpoints();
  seed_endpoints(true);
  backward();
  required_rebased_ = true;
  criticality_pass();
  report_.hold_computed = false;
}

void Engine::evaluate() {
  rank();
  seed_endpoints(false);
  backward();
  required_rebased_ = false;
  forward_early();
  check_hold();
  check_electrical();
  report_.hold_computed = true;
}

void Engine::forward_early() {
  const size_t h = hold_corner_;
  std::fill(early_arrival_.begin(), early_arrival_.end(), inf);
  std::fill(early_slew_.begin(), early_slew_.end(), 0.f);
  for (const auto& port : netlist_.ports()) {
    const auto pin = netlist_.pin(port.cell, 0);
    if (!port.input || clock_pin_[pin] != 0 || cut_[pin]) {
      continue;
    }
    float delay = 0, slew = constraints_.default_input_slew_ps;
    for (const auto& in : constraints_.inputs) {
      if (in.port == port.name) {
        delay = in.delay_ps;
        slew  = in.slew_ps;
      }
    }
    for (unsigned t = 0; t < 2; ++t) {
      early_arrival_[pin * 2 + t] = delay;
      early_slew_[pin * 2 + t]    = slew;
    }
  }
  for (uint32_t pin = 0; pin < netlist_.pins(); ++pin) {
    if (clock_pin_[pin] == 2 && !cut_[pin]) {
      const float clock_slew   = constraints_.clocks[pin_clock_[pin]].slew_ps;
      early_arrival_[pin * 2]  = 0.f;
      early_slew_[pin * 2]     = clock_slew;
      early_slew_[pin * 2 + 1] = clock_slew;
    }
  }
  for (const uint32_t p : netlist_.order()) {
    if (netlist_.untimed(p) || clock_pin_[p] != 0 || cut_[p]) {
      continue;
    }
    const auto net = netlist_.net_of(p);
    if (net != no_index && !netlist_.is_driver(p)) {
      const auto  driver = netlist_.driver_of(net);
      const float wd     = wire_delay(p, h);
      const float ws     = wire_.sink_slew_ps.empty() ? 0.f : wire_.sink_slew_ps[static_cast<size_t>(p) * corners_ + h];
      for (unsigned t = 0; t < 2; ++t) {
        early_arrival_[p * 2 + t] = early_arrival_[driver * 2 + t] + wd;
        early_slew_[p * 2 + t]    = ws == 0.f ? early_slew_[driver * 2 + t] : std::hypot(early_slew_[driver * 2 + t], ws);
      }
      continue;
    }
    const auto           cell = netlist_.cell_of(p);
    const auto           type = netlist_.type_of(cell);
    const auto           arcs = types_.arcs_into(type, netlist_.local_of(p));
    const float          load = net == no_index ? 0.f : net_load(net, h, true);
    std::array<float, 2> slew_min{inf, inf};
    for (const auto& arc : arcs) {
      if (!timed_arc(arc)) {
        continue;
      }
      const auto from_pin = netlist_.pin(cell, arc.from);
      if (cut_[from_pin]) {
        continue;
      }
      for (const auto& combo : combos_of(arc)) {
        const auto in = static_cast<unsigned>(combo.in), out = static_cast<unsigned>(combo.out);
        if (early_arrival_[from_pin * 2 + in] == inf) {
          continue;
        }
        const auto delay_table = types_.table(arc, h, 0, out);
        if (delay_table == no_index) {
          continue;
        }
        const Query query{early_slew_[from_pin * 2 + in], load, 0.f, 0.f};
        const float delay           = types_.lookup(delay_table, query);
        const auto  slew_table      = types_.table(arc, h, 1, out);
        const float out_slew        = slew_table == no_index ? 0.f : std::max(0.f, types_.lookup(slew_table, query));
        early_arrival_[p * 2 + out] = std::min(early_arrival_[p * 2 + out], early_arrival_[from_pin * 2 + in] + delay);
        slew_min[out]               = std::min(slew_min[out], out_slew);
      }
    }
    for (unsigned t = 0; t < 2; ++t) {
      if (slew_min[t] != inf) {
        early_slew_[p * 2 + t] = slew_min[t];
      }
    }
  }
}

void Engine::check_hold() {
  const size_t h = hold_corner_;
  hold_slack_.assign(netlist_.pins(), inf);
  std::vector<float> wns(std::max<size_t>(1, constraints_.clocks.size()), inf), tns(wns.size(), 0.f);
  for (const auto& endpoint : endpoints_) {
    if (endpoint.port) {
      continue;
    }
    const auto pin   = endpoint.pin;
    const auto cell  = netlist_.cell_of(pin);
    const auto type  = netlist_.type_of(cell);
    float      worst = inf;
    for (unsigned t = 0; t < 2; ++t) {
      if (early_arrival_[pin * 2 + t] == inf) {
        continue;
      }
      float hold = -inf;
      for (const auto& arc : types_.arcs_into(type, netlist_.local_of(pin))) {
        if (arc.kind != Arc_kind::hold_rise) {
          continue;
        }
        const auto table = types_.table(arc, h, 2, t);
        if (table == no_index) {
          continue;
        }
        const auto  clock_pin = netlist_.pin(cell, arc.from);
        const Query query{0.f, 0.f, early_slew_[clock_pin * 2], early_slew_[pin * 2 + t]};
        hold = std::max(hold, types_.lookup(table, query));
      }
      if (hold == -inf) {
        continue;
      }
      worst = std::min(worst, early_arrival_[pin * 2 + t] - hold);
    }
    hold_slack_[pin] = worst;
    if (worst != inf) {
      wns[endpoint.clock]  = std::min(wns[endpoint.clock], worst);
      tns[endpoint.clock] += std::min(0.f, worst);
    }
  }
  for (size_t c = 0; c < report_.clocks.size(); ++c) {
    report_.clocks[c].hold_wns_ps = wns[c] == inf ? 0.f : wns[c];
    report_.clocks[c].hold_tns_ps = tns[c];
  }
}

void Engine::check_electrical() {
  const size_t C                 = corners_;
  report_.slew_violations        = 0;
  report_.capacitance_violations = 0;
  report_.electrical_excess      = 0;
  for (uint32_t p = 0; p < netlist_.pins(); ++p) {
    if (netlist_.untimed(p) || clock_pin_[p] != 0) {
      continue;
    }
    const auto cell        = netlist_.cell_of(p);
    const auto type        = netlist_.type_of(cell);
    const auto local       = netlist_.local_of(p);
    double     slew_excess = 0, cap_excess = 0;
    for (size_t k = 0; k < C; ++k) {
      if (!(options_.setup_corners >> k & 1u)) {
        continue;
      }
      const float limit = types_.max_transition(type, local, k);
      if (limit != inf && limit > 0) {
        for (unsigned t = 0; t < 2; ++t) {
          if (arrival_[index(p, k, t)] != -inf) {
            slew_excess = std::max<double>(slew_excess, (slew_[index(p, k, t)] - limit) / limit);
          }
        }
      }
      const auto net = netlist_.net_of(p);
      if (net != no_index && netlist_.is_driver(p)) {
        const float cap_limit = types_.max_capacitance(type, local, k);
        if (cap_limit != inf && cap_limit > 0) {
          cap_excess = std::max<double>(cap_excess, (net_load(net, k, false) - cap_limit) / cap_limit);
        }
      }
    }
    if (slew_excess > 0) {
      ++report_.slew_violations;
      report_.electrical_excess += slew_excess;
    }
    if (cap_excess > 0) {
      ++report_.capacitance_violations;
      report_.electrical_excess += cap_excess;
    }
  }
}

float Engine::slack(uint32_t pin) const {
  float worst = inf;
  for (size_t k = 0; k < corners_; ++k) {
    if (!(options_.setup_corners >> k & 1u)) {
      continue;
    }
    for (unsigned t = 0; t < 2; ++t) {
      const auto at = index(pin, k, t);
      if (arrival_[at] != -inf && required_[at] != inf) {
        worst = std::min(worst, required_[at] - arrival_[at]);
      }
    }
  }
  return worst;
}

std::vector<uint32_t> Engine::endpoint_pins() const {
  std::vector<uint32_t> pins;
  pins.reserve(endpoints_.size());
  for (const auto& endpoint : endpoints_) {
    pins.push_back(endpoint.pin);
  }
  return pins;
}

float Engine::arc_delay(uint32_t cell, uint32_t arc, size_t corner, unsigned in, unsigned out) const {
  const auto  type   = netlist_.type_of(cell);
  const auto& a      = types_.arcs(type)[arc];
  const auto  combos = combos_of(a);
  for (size_t m = 0; m < combos.size(); ++m) {
    if (combos[m].in == static_cast<int8_t>(in) && combos[m].out == static_cast<int8_t>(out)) {
      return arc_delay_[cell_base_[cell] + arc_offset_[types_.type(type).first_arc + arc] * corners_ + corner * combos.size() + m];
    }
  }
  return std::numeric_limits<float>::quiet_NaN();
}

std::vector<Path_point> Engine::worst_path(uint32_t endpoint) const {
  std::vector<Path_point> path;
  unsigned                transition = 0;
  if (endpoint == no_index) {
    float worst = inf;
    for (size_t e = 0; e < endpoints_.size(); ++e) {
      if (endpoint_path_slack_[e] < worst) {
        worst      = endpoint_path_slack_[e];
        endpoint   = endpoints_[e].pin;
        transition = endpoint_path_transition_[e];
      }
    }
    if (endpoint == no_index) {
      return path;
    }
  } else {
    float worst = inf;
    for (unsigned t = 0; t < 2; ++t) {
      const auto at = index(endpoint, options_.path_corner, t);
      if (arrival_[at] != -inf && (worst == inf || -arrival_[at] < worst)) {
        worst      = -arrival_[at];
        transition = t;
      }
    }
  }
  uint32_t pin = endpoint;
  while (pin != no_index && path.size() < netlist_.pins() + 1) {
    path.push_back({pin, transition, arrival_[index(pin, options_.path_corner, transition)]});
    const auto& before = predecessor_[pin * 2 + transition];
    pin                = before.pin;
    transition         = before.transition;
  }
  std::reverse(path.begin(), path.end());
  return path;
}

size_t Engine::bytes() const {
  const auto of
      = [](const auto& vector) { return vector.capacity() * sizeof(typename std::decay_t<decltype(vector)>::value_type); };
  return of(arrival_) + of(slew_) + of(required_) + of(early_arrival_) + of(early_slew_) + of(hold_slack_) + of(criticality_)
         + of(depth_) + of(predecessor_) + of(clock_pin_) + of(pin_clock_) + of(load_) + of(early_load_) + of(arc_delay_)
         + of(cell_base_) + of(arc_offset_) + of(port_load_) + of(endpoints_) + of(cut_) + of(excluded_);
}

}  // namespace livehd::timing::fast
