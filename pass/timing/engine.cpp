// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "engine.hpp"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <string_view>
#include <unordered_map>
#include <array>
#include <cmath>
#include <format>
#include <functional>
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

Engine::Engine(Netlist& netlist, Constraints constraints, Options options)
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

template <class T>
void Engine::store(std::vector<T>& v, size_t i, T value) {
  if (journal_on_ && !(v[i] == value)) {
    Undo undo{};
    undo.index = i;
    if (static_cast<const void*>(&v) == static_cast<const void*>(&arrival_)) {
      undo.kind = Undo_kind::arrival;
    } else if (static_cast<const void*>(&v) == static_cast<const void*>(&slew_)) {
      undo.kind = Undo_kind::slew;
    } else if (static_cast<const void*>(&v) == static_cast<const void*>(&required_)) {
      undo.kind = Undo_kind::required;
    } else if (static_cast<const void*>(&v) == static_cast<const void*>(&depth_)) {
      undo.kind = Undo_kind::depth;
    } else if (static_cast<const void*>(&v) == static_cast<const void*>(&load_)) {
      undo.kind = Undo_kind::load;
    } else if (static_cast<const void*>(&v) == static_cast<const void*>(&wire_.net_cap_ff)) {
      undo.kind = Undo_kind::wire_cap;
    } else if (static_cast<const void*>(&v) == static_cast<const void*>(&wire_.sink_delay_ps)) {
      undo.kind = Undo_kind::wire_delay;
    } else if (static_cast<const void*>(&v) == static_cast<const void*>(&wire_.sink_slew_ps)) {
      undo.kind = Undo_kind::wire_slew;
    } else if (static_cast<const void*>(&v) == static_cast<const void*>(&endpoint_required_)) {
      undo.kind = Undo_kind::endpoint_required;
    } else if (static_cast<const void*>(&v) == static_cast<const void*>(&endpoint_slack_)) {
      undo.kind = Undo_kind::endpoint_slack;
    } else {
      undo.kind = Undo_kind::arc_delay;
    }
    if constexpr (std::is_same_v<T, float>) {
      std::memcpy(&undo.old_bits, &v[i], sizeof(float));
    } else {
      undo.old_bits = v[i];
    }
    journal_.push_back(undo);
  }
  v[i] = value;
}

void Engine::store_predecessor(size_t i, Predecessor value) {
  if (journal_on_ && !(predecessor_[i].pin == value.pin && predecessor_[i].transition == value.transition)) {
    journal_.push_back({Undo_kind::predecessor, predecessor_[i].transition, predecessor_[i].pin, i});
  }
  predecessor_[i] = value;
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
  // The constraint entries of each port, by one pass over the ports (a design has thousands of both).
  port_input_.assign(cells, no_index);
  {
    std::unordered_map<std::string_view, uint32_t> input_cell, output_cell;
    for (const auto& port : netlist_.ports()) {
      (port.input ? input_cell : output_cell)[port.name] = port.cell;
    }
    for (uint32_t i = 0; i < constraints_.inputs.size(); ++i) {
      if (const auto it = input_cell.find(constraints_.inputs[i].port); it != input_cell.end()) {
        port_input_[it->second] = i;
      }
    }
    for (const auto& out : constraints_.outputs) {
      if (const auto it = output_cell.find(out.port); it != output_cell.end()) {
        port_load_[it->second]  = out.load_ff;
        port_delay_[it->second] = out.delay_ps;
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
  load_.assign(netlist_.nets() * corners_, 0.f);
  early_load_.assign(netlist_.nets(), 0.f);
  for (uint32_t net = 0; net < netlist_.nets(); ++net) {
    compute_net_load(net);
  }
}

// The total load of one net in every corner: the pin capacitances of its sinks (the larger of rise and fall),
// the load of an output port, and the wire capacitance. Returns true when a stored value changed.
bool Engine::compute_net_load(uint32_t net) {
  const size_t         C = corners_;
  std::array<float, 32> sum{};
  float                early = 0;
  for (const auto sink : netlist_.sinks_of(net)) {
    const auto cell  = netlist_.cell_of(sink);
    const auto type  = netlist_.type_of(cell);
    const auto local = netlist_.local_of(sink);
    if (type == Cell_types::output_port_type) {
      for (size_t k = 0; k < C; ++k) {
        sum[k] += port_load_[cell];
      }
      early += port_load_[cell];
      continue;
    }
    for (size_t k = 0; k < C; ++k) {
      sum[k] += std::max(types_.pin_cap(type, local, k, 0), types_.pin_cap(type, local, k, 1));
    }
    early += std::min(types_.pin_cap(type, local, hold_corner_, 0), types_.pin_cap(type, local, hold_corner_, 1));
  }
  if (!wire_.net_cap_ff.empty()) {
    for (size_t k = 0; k < C; ++k) {
      sum[k] += wire_.net_cap_ff[net * C + k];
    }
    early += wire_.net_cap_ff[net * C + hold_corner_];
  }
  bool changed = false;
  for (size_t k = 0; k < C; ++k) {
    changed = changed || load_[net * C + k] != sum[k];
    store(load_, net * C + k, sum[k]);
  }
  early_load_[net] = early;  // the hold walk is not kept up to date by edits
  return changed;
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
    if (const auto entry = port_input_[port.cell]; entry != no_index) {
      delay = constraints_.inputs[entry].delay_ps;
      slew  = constraints_.inputs[entry].slew_ps;
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
  seed_late();
  for (const uint32_t p : netlist_.order()) {
    recompute_arrival(p);
  }
}

// The late arrival, slew, depth and worst predecessor of one pin from its sources, which must be current.
// A pin the engine does not time keeps its seed.
void Engine::recompute_arrival(uint32_t p) {
  const size_t C = corners_;
  if (netlist_.untimed(p) || clock_pin_[p] != 0 || cut_[p]) {
    return;
  }
  const auto net = netlist_.net_of(p);
  if (net != no_index && !netlist_.is_driver(p)) {
    const auto driver = netlist_.driver_of(net);
    for (size_t k = 0; k < C; ++k) {
      const float wd = wire_delay(p, k);
      const float ws = wire_.sink_slew_ps.empty() ? 0.f : wire_.sink_slew_ps[static_cast<size_t>(p) * C + k];
      for (unsigned t = 0; t < 2; ++t) {
        const auto from = index(driver, k, t), to = index(p, k, t);
        store(arrival_, to, arrival_[from] + wd);
        store(slew_, to, ws == 0.f ? slew_[from] : std::hypot(slew_[from], ws));
        store(depth_, to, depth_[from]);
        if (k == options_.path_corner) {
          store_predecessor(p * 2 + t, {driver, static_cast<uint8_t>(t)});
        }
      }
    }
    return;
  }
  const auto cell  = netlist_.cell_of(p);
  const auto local = netlist_.local_of(p);
  const auto type  = netlist_.type_of(cell);
  const auto arcs  = types_.arcs_into(type, local);
  if (arcs.empty()) {
    return;
  }
  const auto* type_arcs = types_.arcs(type).data();
  for (size_t k = 0; k < C; ++k) {
    // Start the pin from nothing: this is also the update of a pin that already has a value.
    for (unsigned t = 0; t < 2; ++t) {
      store(arrival_, index(p, k, t), -inf);
      store(slew_, index(p, k, t), 0.f);
      store(depth_, index(p, k, t), uint16_t{0});
      if (k == options_.path_corner) {
        store_predecessor(p * 2 + t, {});
      }
    }
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
      const auto offset = cell_base_[cell] + (arc_offset_[types_.type(type).first_arc + (&arc - type_arcs)]) * C + k * combos.size();
      for (size_t m = 0; m < combos.size(); ++m) {
        const auto in = static_cast<unsigned>(combos[m].in), out = static_cast<unsigned>(combos[m].out);
        const auto from = index(from_pin, k, in), to = index(p, k, out);
        if (arrival_[from] == -inf) {
          delay_changed_ = delay_changed_ || arc_delay_[offset + m] != 0.f;
          store(arc_delay_, offset + m, 0.f);
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
        delay_changed_ = delay_changed_ || arc_delay_[offset + m] != delay;
        store(arc_delay_, offset + m, delay);
        const float at = arrival_[from] + delay;
        if (at > arrival_[to]) {
          store(arrival_, to, at);
          store(depth_, to, static_cast<uint16_t>(std::min<uint32_t>(depth_[from] + 1, 65535)));
          if (k == options_.path_corner) {
            store_predecessor(p * 2 + out, {from_pin, static_cast<uint8_t>(in)});
          }
        }
        store(slew_, to, std::max(slew_[to], out_slew));
      }
    }
  }
}

// The requirement of one endpoint in a corner and transition: the clock period less the setup (or the output
// delay), the uncertainty, the depth margin, and, rebased, the clock's negative worst slack. +inf: not checked.
float Engine::endpoint_requirement(size_t e, size_t k, unsigned t, bool rebased) const {
  const auto& endpoint = endpoints_[e];
  const auto  pin      = endpoint.pin;
  const auto  at       = index(pin, k, t);
  if (arrival_[at] == -inf) {
    return inf;
  }
  const auto& clock = constraints_.clocks[endpoint.clock];
  const auto  cell  = netlist_.cell_of(pin);
  float       requirement = inf;
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
      return inf;
    }
    requirement = clock.period_ps - setup;
  }
  requirement        -= clock.uncertainty_ps;
  const float margin  = options_.depth_margin[k];
  if (margin > 0.f) {
    requirement -= margin * arrival_[at] / std::sqrt(static_cast<float>(std::max<uint16_t>(depth_[at], 1)));
  }
  const float ref = rebased ? std::min(0.f, clock_wns_[endpoint.clock * corners_ + k]) : 0.f;
  return requirement - ref;
}

void Engine::seed_endpoints(bool rebased) {
  rebased_ = rebased;
  std::fill(required_.begin(), required_.end(), inf);
  endpoint_required_.assign(endpoints_.size() * corners_ * 2, inf);
  for (size_t e = 0; e < endpoints_.size(); ++e) {
    for (size_t k = 0; k < corners_; ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        const float r = endpoint_requirement(e, k, t, rebased);
        endpoint_required_[(e * corners_ + k) * 2 + t] = r;
        if (r != inf) {
          required_[index(endpoints_[e].pin, k, t)] = std::min(required_[index(endpoints_[e].pin, k, t)], r);
        }
      }
    }
  }
}

void Engine::backward() {
  const auto order = netlist_.order();
  for (size_t i = order.size(); i-- > 0;) {
    recompute_required(order[i]);
  }
}

// The required time of one pin from what it feeds, which must be current: the endpoint requirement if it is
// one, the sinks of its net less the wire delay, the cell outputs it feeds less the arc delay.
void Engine::recompute_required(uint32_t p) {
  if (netlist_.untimed(p) || clock_pin_[p] != 0 || cut_[p]) {
    return;
  }
  const size_t C    = corners_;
  const auto   net  = netlist_.net_of(p);
  const auto   cell = netlist_.cell_of(p);
  const auto   local = netlist_.local_of(p);
  const auto   type  = netlist_.type_of(cell);
  // The endpoint of this pin, if any.
  const auto endpoint = std::lower_bound(endpoints_.begin(), endpoints_.end(), p, [](const Endpoint& e, uint32_t pin) { return e.pin < pin; });
  const size_t e      = endpoint != endpoints_.end() && endpoint->pin == p ? static_cast<size_t>(endpoint - endpoints_.begin()) : no_endpoint;
  const auto* type_arcs = types_.arcs(type).data();
  const auto  arcs      = types_.arcs(type);
  for (size_t k = 0; k < C; ++k) {
    for (unsigned t = 0; t < 2; ++t) {
      float requirement = e == no_endpoint ? inf : endpoint_required_[(e * C + k) * 2 + t];
      if (net != no_index && netlist_.is_driver(p)) {
        for (const auto sink : netlist_.sinks_of(net)) {
          if (netlist_.untimed(sink) || clock_pin_[sink] != 0) {
            continue;
          }
          requirement = std::min(requirement, required_[index(sink, k, t)] - wire_delay(sink, k));
        }
      }
      for (size_t j = 0; j < arcs.size(); ++j) {
        const auto& arc = arcs[j];
        if (arc.kind != Arc_kind::combinational || arc.from != local) {
          continue;
        }
        const auto combos = combos_of(arc);
        const auto offset
            = cell_base_[cell] + (arc_offset_[types_.type(type).first_arc + (&arc - type_arcs)]) * C + k * combos.size();
        const auto to_pin = netlist_.pin(cell, arc.to);
        for (size_t m = 0; m < combos.size(); ++m) {
          if (static_cast<unsigned>(combos[m].in) != t) {
            continue;
          }
          const auto out_required = required_[index(to_pin, k, static_cast<unsigned>(combos[m].out))];
          if (out_required != inf) {
            requirement = std::min(requirement, out_required - arc_delay_[offset + m]);
          }
        }
      }
      store(required_, index(p, k, t), requirement);
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
  if (options_.true_slack_in_rank) {
    // A caller that reports slack (a GUI estimate, a comparison across rounds) wants the real one: another
    // backward walk against the true requirements; the criticality above stays the rebased one.
    seed_endpoints(false);
    backward();
    required_rebased_ = false;
  }
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
  // The hold walk's state is allocated here, on first use: a caller that only ranks never pays for it.
  early_arrival_.assign(netlist_.pins() * 2, inf);
  early_slew_.assign(netlist_.pins() * 2, 0.f);
  for (const auto& port : netlist_.ports()) {
    const auto pin = netlist_.pin(port.cell, 0);
    if (!port.input || clock_pin_[pin] != 0 || cut_[pin]) {
      continue;
    }
    float delay = 0, slew = constraints_.default_input_slew_ps;
    if (const auto entry = port_input_[port.cell]; entry != no_index) {
      delay = constraints_.inputs[entry].delay_ps;
      slew  = constraints_.inputs[entry].slew_ps;
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

// The worst slew and capacitance excess ratios of one pin over the setup corners (0: within its limits).
void Engine::electrical_excess(uint32_t p, double& slew_excess, double& cap_excess) const {
  slew_excess = cap_excess = 0;
  if (netlist_.untimed(p) || clock_pin_[p] != 0) {
    return;
  }
  const auto cell  = netlist_.cell_of(p);
  const auto type  = netlist_.type_of(cell);
  const auto local = netlist_.local_of(p);
  for (size_t k = 0; k < corners_; ++k) {
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
}

Electrical_pin Engine::pin_electrical(uint32_t p) {
  settle();
  double slew_excess, cap_excess;
  electrical_excess(p, slew_excess, cap_excess);
  return {p, static_cast<uint8_t>((slew_excess > 0) + (cap_excess > 0)),
          static_cast<float>((slew_excess > 0 ? slew_excess : 0) + (cap_excess > 0 ? cap_excess : 0))};
}

void Engine::check_electrical() {
  report_.slew_violations        = 0;
  report_.capacitance_violations = 0;
  report_.electrical_excess      = 0;
  electrical_pins_.clear();
  for (uint32_t p = 0; p < netlist_.pins(); ++p) {
    double slew_excess, cap_excess;
    electrical_excess(p, slew_excess, cap_excess);
    if (slew_excess > 0) {
      ++report_.slew_violations;
      report_.electrical_excess += slew_excess;
    }
    if (cap_excess > 0) {
      ++report_.capacitance_violations;
      report_.electrical_excess += cap_excess;
    }
    if (slew_excess > 0 || cap_excess > 0) {
      electrical_pins_.push_back({p, static_cast<uint8_t>((slew_excess > 0) + (cap_excess > 0)),
                                  static_cast<float>(slew_excess + cap_excess)});
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
         + of(cell_base_) + of(port_input_) + of(electrical_pins_) + of(arc_offset_) + of(port_load_) + of(endpoints_) + of(cut_) + of(excluded_);
}

// ---- Edit sessions -------------------------------------------------------------------------------------

size_t Engine::endpoint_of(uint32_t pin) const {
  const auto it = std::lower_bound(endpoints_.begin(), endpoints_.end(), pin, [](const Endpoint& e, uint32_t p) { return e.pin < p; });
  return it != endpoints_.end() && it->pin == pin ? static_cast<size_t>(it - endpoints_.begin()) : no_endpoint;
}

void Engine::begin_edits() {
  if (journal_on_) {
    return;
  }
  if (rebased_ || endpoint_required_.empty() != endpoints_.empty()) {
    throw std::logic_error("Engine edits need a state left by evaluate()");
  }
  const auto order = netlist_.order();
  pos_.assign(netlist_.pins(), 0);
  for (size_t i = 0; i < order.size(); ++i) {
    pos_[order[i]] = static_cast<uint32_t>(i);
  }
  queued_.assign(netlist_.pins(), 0);
  endpoint_touched_.assign(endpoints_.size(), 0);
  if (wire_.net_cap_ff.empty()) {
    wire_.net_cap_ff.assign(netlist_.nets() * corners_, 0.f);
  }
  if (wire_.sink_delay_ps.empty()) {
    wire_.sink_delay_ps.assign(netlist_.pins() * corners_, 0.f);
  }
  if (wire_.sink_slew_ps.empty()) {
    wire_.sink_slew_ps.assign(netlist_.pins() * corners_, 0.f);
  }
  journal_on_ = true;
}

void Engine::push_forward(uint32_t pin) {
  if (!(queued_[pin] & 1u)) {
    queued_[pin] |= 1u;
    fwd_heap_.push_back(pos_[pin]);
    std::push_heap(fwd_heap_.begin(), fwd_heap_.end(), std::greater<>());
  }
}

void Engine::push_backward(uint32_t pin) {
  if (!(queued_[pin] & 2u)) {
    queued_[pin] |= 2u;
    bwd_heap_.push_back(pos_[pin]);
    std::push_heap(bwd_heap_.begin(), bwd_heap_.end());
  }
}

void Engine::touch_endpoint(uint32_t pin) {
  if (const auto e = endpoint_of(pin); e != no_endpoint && !endpoint_touched_[e]) {
    endpoint_touched_[e] = 1;
    touched_endpoints_.push_back(static_cast<uint32_t>(e));
  }
}

// The setup slack of an endpoint over the setup corners and both transitions; it is what the report calls the
// endpoint slack, kept up to date.
void Engine::refresh_endpoint(size_t e) {
  float worst = inf;
  for (size_t k = 0; k < corners_; ++k) {
    if (!(options_.setup_corners >> k & 1u)) {
      continue;
    }
    for (unsigned t = 0; t < 2; ++t) {
      const auto at = index(endpoints_[e].pin, k, t);
      if (arrival_[at] != -inf && required_[at] != inf) {
        worst = std::min(worst, required_[at] - arrival_[at]);
      }
    }
  }
  store(endpoint_slack_, e, worst);
}

void Engine::settle() {
  if (!journal_on_ || (fwd_heap_.empty() && bwd_heap_.empty())) {
    return;
  }
  const size_t         C     = corners_;
  const auto           order = netlist_.order();
  std::array<float, 192> before{};  // arrival, slew, depth of the 2 transitions of up to 31 corners: 186 values
  // Forward: lowest position first; a pin that changes wakes what it feeds.
  while (!fwd_heap_.empty()) {
    std::pop_heap(fwd_heap_.begin(), fwd_heap_.end(), std::greater<>());
    const auto position = fwd_heap_.back();
    fwd_heap_.pop_back();
    const auto p = order[position];
    queued_[p]  &= static_cast<uint8_t>(~1u);
    size_t n = 0;
    for (size_t k = 0; k < C; ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        before[n++] = arrival_[index(p, k, t)];
        before[n++] = slew_[index(p, k, t)];
        before[n++] = depth_[index(p, k, t)];
      }
    }
    delay_changed_ = false;
    recompute_arrival(p);
    n                = 0;
    bool changed     = false;
    for (size_t k = 0; k < C; ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        changed = changed || before[n] != arrival_[index(p, k, t)] || before[n + 1] != slew_[index(p, k, t)]
                  || before[n + 2] != depth_[index(p, k, t)];
        n += 3;
      }
    }
    const auto cell  = netlist_.cell_of(p);
    const auto local = netlist_.local_of(p);
    if (delay_changed_) {
      // The requirement of the pins that feed this one through an arc uses the arc delay.
      for (const auto& arc : types_.arcs_into(netlist_.type_of(cell), local)) {
        if (arc.kind == Arc_kind::combinational) {
          push_backward(netlist_.pin(cell, arc.from));
        }
      }
    }
    if (!changed) {
      continue;
    }
    push_backward(p);
    if (const auto e = endpoint_of(p); e != no_endpoint) {
      for (size_t k = 0; k < C; ++k) {
        for (unsigned t = 0; t < 2; ++t) {
          store(endpoint_required_, (e * C + k) * 2 + t, endpoint_requirement(e, k, t, false));
        }
      }
      touch_endpoint(p);
    }
    const auto wake = [&](uint32_t pin) {
      if (pos_[pin] > position) {
        push_forward(pin);
      }
    };
    if (const auto net = netlist_.net_of(p); net != no_index && netlist_.is_driver(p)) {
      for (const auto sink : netlist_.sinks_of(net)) {
        wake(sink);
      }
    } else {
      const auto type = netlist_.type_of(cell);
      for (const auto& arc : types_.arcs(type)) {
        if (arc.from == local && timed_arc(arc)) {
          wake(netlist_.pin(cell, arc.to));
        }
      }
    }
  }
  // Backward: highest position first; a pin whose requirement changes wakes what feeds it.
  while (!bwd_heap_.empty()) {
    std::pop_heap(bwd_heap_.begin(), bwd_heap_.end());
    const auto position = bwd_heap_.back();
    bwd_heap_.pop_back();
    const auto p = order[position];
    queued_[p]  &= static_cast<uint8_t>(~2u);
    size_t n = 0;
    for (size_t k = 0; k < C; ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        before[n++] = required_[index(p, k, t)];
      }
    }
    recompute_required(p);
    n            = 0;
    bool changed = false;
    for (size_t k = 0; k < C; ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        changed = changed || before[n++] != required_[index(p, k, t)];
      }
    }
    if (!changed) {
      continue;
    }
    touch_endpoint(p);
    const auto wake = [&](uint32_t pin) {
      if (pos_[pin] < position) {
        push_backward(pin);
      }
    };
    const auto cell  = netlist_.cell_of(p);
    const auto local = netlist_.local_of(p);
    if (const auto net = netlist_.net_of(p); net != no_index && !netlist_.is_driver(p)) {
      wake(netlist_.driver_of(net));
    }
    for (const auto& arc : types_.arcs_into(netlist_.type_of(cell), local)) {
      if (arc.kind == Arc_kind::combinational) {
        wake(netlist_.pin(cell, arc.from));
      }
    }
  }
  for (const auto e : touched_endpoints_) {
    endpoint_touched_[e] = 0;
    refresh_endpoint(e);
  }
  touched_endpoints_.clear();
}

size_t Engine::mark() {
  begin_edits();
  settle();
  return journal_.size();
}

void Engine::rollback(size_t to) {
  if (!journal_on_) {
    return;
  }
  for (const auto p : fwd_heap_) {
    queued_[netlist_.order()[p]] = 0;
  }
  for (const auto p : bwd_heap_) {
    queued_[netlist_.order()[p]] = 0;
  }
  fwd_heap_.clear();
  bwd_heap_.clear();
  for (const auto e : touched_endpoints_) {
    endpoint_touched_[e] = 0;
  }
  touched_endpoints_.clear();
  const auto bits_to_float = [](uint32_t bits) {
    float v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
  };
  while (journal_.size() > to) {
    const auto u = journal_.back();
    journal_.pop_back();
    switch (u.kind) {
      case Undo_kind::arrival          : arrival_[u.index] = bits_to_float(u.old_bits); break;
      case Undo_kind::slew             : slew_[u.index] = bits_to_float(u.old_bits); break;
      case Undo_kind::required         : required_[u.index] = bits_to_float(u.old_bits); break;
      case Undo_kind::depth            : depth_[u.index] = static_cast<uint16_t>(u.old_bits); break;
      case Undo_kind::predecessor      : predecessor_[u.index] = {u.old_bits, u.aux}; break;
      case Undo_kind::arc_delay        : arc_delay_[u.index] = bits_to_float(u.old_bits); break;
      case Undo_kind::load             : load_[u.index] = bits_to_float(u.old_bits); break;
      case Undo_kind::wire_cap         : wire_.net_cap_ff[u.index] = bits_to_float(u.old_bits); break;
      case Undo_kind::wire_delay       : wire_.sink_delay_ps[u.index] = bits_to_float(u.old_bits); break;
      case Undo_kind::wire_slew        : wire_.sink_slew_ps[u.index] = bits_to_float(u.old_bits); break;
      case Undo_kind::endpoint_required: endpoint_required_[u.index] = bits_to_float(u.old_bits); break;
      case Undo_kind::endpoint_slack   : endpoint_slack_[u.index] = bits_to_float(u.old_bits); break;
      case Undo_kind::cell_type        : netlist_.retype(static_cast<uint32_t>(u.index), u.old_bits); break;
    }
  }
}

void Engine::end_edits() {
  rollback(0);
  journal_on_ = false;
  journal_.clear();
  journal_.shrink_to_fit();
}

void Engine::resize_cell(uint32_t cell, uint32_t type) {
  begin_edits();
  settle();
  const auto old = netlist_.type_of(cell);
  if (old == type) {
    return;
  }
  netlist_.retype(cell, type);  // throws, unchanged, for another interface
  journal_.push_back({Undo_kind::cell_type, 0, old, cell});
  const auto first = netlist_.first_pin(cell);
  const auto count = types_.type(type).pin_count;
  for (uint32_t local = 0; local < count; ++local) {
    const auto pin = first + local;
    push_backward(pin);  // its arcs and checks are other tables now
    if (!types_.arcs_into(type, local).empty()) {
      push_forward(pin);
    }
    if (const auto e = endpoint_of(pin); e != no_endpoint) {
      for (size_t k = 0; k < corners_; ++k) {
        for (unsigned t = 0; t < 2; ++t) {
          store(endpoint_required_, (e * corners_ + k) * 2 + t, endpoint_requirement(e, k, t, false));
        }
      }
      touch_endpoint(pin);
    }
    // An input pin has another capacitance: the net that drives it has another load.
    if (const auto net = netlist_.net_of(pin); net != no_index && !netlist_.is_driver(pin)) {
      if (compute_net_load(net)) {
        push_forward(netlist_.driver_of(net));
      }
    }
  }
}

void Engine::set_net_wire(uint32_t net, std::span<const float> cap_ff, std::span<const uint32_t> sink_pins,
                          std::span<const float> delay_ps, std::span<const float> slew_ps) {
  begin_edits();
  settle();
  const size_t C = corners_;
  if (cap_ff.size() != C || delay_ps.size() != sink_pins.size() * C || slew_ps.size() != sink_pins.size() * C) {
    throw std::invalid_argument("Engine::set_net_wire: sizes do not match the corners and pins");
  }
  for (size_t k = 0; k < C; ++k) {
    store(wire_.net_cap_ff, net * C + k, cap_ff[k]);
  }
  compute_net_load(net);
  push_forward(netlist_.driver_of(net));
  push_backward(netlist_.driver_of(net));
  for (size_t i = 0; i < sink_pins.size(); ++i) {
    const auto pin = sink_pins[i];
    for (size_t k = 0; k < C; ++k) {
      store(wire_.sink_delay_ps, static_cast<size_t>(pin) * C + k, delay_ps[i * C + k]);
      store(wire_.sink_slew_ps, static_cast<size_t>(pin) * C + k, slew_ps[i * C + k]);
    }
    push_forward(pin);
  }
}

float Engine::wns() {
  settle();
  float worst = inf;
  for (const auto s : endpoint_slack_) {
    worst = std::min(worst, s);
  }
  return worst;
}

float Engine::tns() {
  settle();
  float sum = 0;
  for (const auto s : endpoint_slack_) {
    if (s < 0) {
      sum += s;
    }
  }
  return sum;
}

std::vector<std::pair<uint32_t, float>> Engine::failing_endpoints(float below_ps) {
  settle();
  std::vector<std::pair<uint32_t, float>> out;
  for (size_t e = 0; e < endpoints_.size(); ++e) {
    if (endpoint_slack_[e] < below_ps) {
      out.emplace_back(endpoints_[e].pin, endpoint_slack_[e]);
    }
  }
  std::ranges::sort(out, [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : a.first < b.first; });
  return out;
}

}  // namespace livehd::timing::fast
