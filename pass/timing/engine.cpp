// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "engine.hpp"

#include <map>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <queue>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace livehd::timing::fast {
namespace {

// The delay and slew a sink sees through its wire, the way OpenSTA's dmp delay calculator reads them
// (DmpAlg::loadDelaySlew): the driver output is a saturating ramp whose `slew` is measured between the library's
// lower and upper thresholds, and the sink is that ramp through one pole whose time constant is the sink's Elmore
// delay `tau`. Delay is the sink's middle-threshold crossing less the ramp's own, slew the sink's lower->upper span.
// A long heavily loaded net then reports the multi-ns slew OpenSTA reports at its far sinks; a short wire leaves
// the driver slew unchanged (delay = tau when tau is under 0.1% of the slew, as OpenSTA does).
std::pair<float, float> ramp_through_pole(float slew, float tau, const Cell_types::Slew_measure& m) {
  if (!(tau > 0)) {
    return {0.f, slew};
  }
  if (!(slew > 0) || tau < slew * 1e-3f) {
    return {tau, slew};
  }
  const double T   = double(slew) * m.derate / (m.upper - m.lower);  // the full 0->1 ramp duration
  const double tc  = tau;
  const auto   y   = [&](double t) {
    if (t <= 0) {
      return 0.0;
    }
    if (t <= T) {
      return (t - tc * -std::expm1(-t / tc)) / T;
    }
    return 1 - tc / T * (std::exp(-(t - T) / tc) - std::exp(-t / tc));
  };
  const auto cross = [&](double v) {
    double lo = 0, hi = T + 30 * tc;
    for (int i = 0; i < 40 && hi - lo > 1e-4; ++i) {
      const double mid = .5 * (lo + hi);
      (y(mid) < v ? lo : hi) = mid;
    }
    return .5 * (lo + hi);
  };
  const double delay = cross(m.middle) - m.middle * T;
  const double out   = (cross(m.upper) - cross(m.lower)) / m.derate;
  return {static_cast<float>(std::max(0.0, delay)), static_cast<float>(std::max<double>(slew, out))};
}
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
constexpr Combo rising_launch_combos[2] = {
    {0, 0},
    {0, 1}
};  // the rising clock edge launches both output transitions

// The (input, output) transition pairs an arc can carry, in the order its delays are stored.
std::span<const Combo> combos_of(const Arc& arc) {
  if (arc.kind == Arc_kind::launch_rise) {
    return rising_launch_combos;
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
  groups_ = std::max<uint32_t>(1, constraints_.groups);
  if (groups_ > 32) {
    throw std::invalid_argument("at most 32 launch groups");
  }
  slots_ = corners_ * groups_;
  const auto relations = groups_ * std::max<size_t>(1, constraints_.clocks.size());
  if ((!constraints_.setup_relation_ps.empty() && constraints_.setup_relation_ps.size() != relations)
      || (!constraints_.hold_relation_ps.empty() && constraints_.hold_relation_ps.size() != relations)) {
    throw std::invalid_argument("clock relations do not match groups x clocks");
  }
  if (constraints_.default_input_group >= groups_) {
    throw std::invalid_argument("default input group out of range");
  }
  for (auto factors : {constraints_.clock_derates.cell, constraints_.clock_derates.net}) {
    for (float factor : factors) {
      if (!(factor > 0) || !std::isfinite(factor)) {
        throw std::invalid_argument("invalid clock delay derate");
      }
    }
  }
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
    } else if (static_cast<const void*>(&v) == static_cast<const void*>(&early_load_)) {
      undo.kind = Undo_kind::early_load;
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

float Engine::setup_relation(size_t group, size_t clock) const {
  return constraints_.setup_relation_ps.empty() ? constraints_.clocks[clock].period_ps
                                                : constraints_.setup_relation_ps[group * constraints_.clocks.size() + clock];
}

float Engine::hold_relation(size_t group, size_t clock) const {
  return constraints_.hold_relation_ps.empty() ? 0.f : constraints_.hold_relation_ps[group * constraints_.clocks.size() + clock];
}

uint32_t Engine::input_group(uint32_t port_cell) const {
  const auto entry = port_input_[port_cell];
  return entry == no_index ? constraints_.default_input_group : constraints_.inputs[entry].group;
}

// On a physical clock tree, compare its launch and capture paths with the actual rise and fall loads.
// The pre-CTS and virtual-tree models retain their conservative setup/hold load bounds.
std::array<float, 2> Engine::transition_loads(uint32_t net, size_t corner, bool early) const {
  if (net == no_index) {
    return {};
  }
  if (clock_sources_.empty() || virtual_net(net)) {
    const float load = net_load(net, corner, early);
    return {load, load};
  }
  const float          wire_cap = wire_.net_cap_ff.empty() ? 0.f : wire_.net_cap_ff[net * corners_ + corner];
  std::array<float, 2> loads{wire_cap, wire_cap};
  for (const auto pin : netlist_.sinks_of(net)) {
    const auto cell = netlist_.cell_of(pin), type = netlist_.type_of(cell);
    for (unsigned t = 0; t < 2; ++t) {
      loads[t] += type == Cell_types::output_port_type ? port_load_[cell]
                                                       : types_.pin_cap(type, netlist_.local_of(pin), corner, t, early);
    }
  }
  return loads;
}

// The arrival and slew an input port launches with: its input delay and transition, or, behind a driving cell, the
// delay that cell adds for the load of the port's net and its output slew there.
Engine::Seed Engine::input_seed(uint32_t port_cell, size_t k, unsigned t, bool early, bool clock_source) const {
  Seed       seed{0.f, constraints_.default_input_slew_ps};
  const auto entry = port_input_[port_cell];
  if (entry == no_index) {
    return seed;
  }
  const auto& in = constraints_.inputs[entry];
  if (!clock_source) {
    seed.arrival = early && in.early_set ? in.early_delay_ps : in.delay_ps;
  }
  seed.slew      = in.slew_ps;
  if (in.drive_type == no_index) {
    return seed;
  }
  const auto  net  = netlist_.net_of(netlist_.pin(port_cell, 0u));
  const float load = transition_loads(net, k, early)[t];
  float       add = early ? inf : -inf, slew = early ? inf : -inf;
  for (const auto& arc : types_.arcs_into(in.drive_type, in.drive_pin)) {
    if (!timed_arc(arc)) {
      continue;
    }
    for (const auto& combo : combos_of(arc)) {
      if (static_cast<unsigned>(combo.out) != t) {
        continue;
      }
      const auto delay_table = types_.table(arc, k, 0, t);
      if (delay_table == no_index) {
        continue;
      }
      const Query loaded{in.slew_ps, load, 0.f, 0.f}, unloaded{in.slew_ps, 0.f, 0.f, 0.f};
      const float extra      = types_.lookup(delay_table, loaded) - types_.lookup(delay_table, unloaded);
      const auto  slew_table = types_.table(arc, k, 1, t);
      const float out_slew   = slew_table == no_index ? in.slew_ps : std::max(0.f, types_.lookup(slew_table, loaded));
      add                    = early ? std::min(add, extra) : std::max(add, extra);
      slew                   = early ? std::min(slew, out_slew) : std::max(slew, out_slew);
    }
  }
  if (std::isfinite(add)) {
    seed.arrival += add;
    seed.slew     = slew;
  }
  return seed;
}

void Engine::prepare() {
  const size_t pins = netlist_.pins(), cells = netlist_.cells();
  const size_t C = slots_;

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
  predecessor_.assign(pins * groups_ * 2, {});
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
        port_load_[it->second] = out.load_ff;
        if (out.check) {
          port_delay_[it->second] = out.delay_ps;
        }
      }
    }
  }
  for (const auto& in : constraints_.inputs) {
    if (in.group >= groups_) {
      throw std::invalid_argument("input delay group out of range");
    }
    if (in.drive_type != no_index && (in.drive_type >= types_.types() || in.drive_pin >= types_.type(in.drive_type).pin_count)) {
      throw std::invalid_argument("driving cell out of range");
    }
  }
  for (const auto& clock : constraints_.clocks) {
    if (clock.group >= groups_) {
      throw std::invalid_argument("clock group out of range");
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
    if (type.untimed() && !netlist_.gating_latch(c)) {
      continue;
    }
    if (type_id == Cell_types::output_port_type) {
      continue;  // below
    }
    bool clocked = true, any_check = false;
    for (uint32_t local = 0; local < type.pin_count; ++local) {
      if (netlist_.untimed(netlist_.pin(c, local))) {
        continue;
      }
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
      has_clock = clock_pin_[clock_pin] == 2 || (type.characterized_gate && clock_pin_[clock_pin] == 1);
      clocked   = clocked && has_clock;
      if (has_setup && !excluded_[netlist_.pin(c, local)]) {
        if (has_clock) {
          endpoints_.push_back({netlist_.pin(c, local), pin_clock_[clock_pin], false, false});
        } else {
          ++report_.unclocked_endpoints;
        }
      }
    }
    if (any_check && !clocked) {
      ++report_.unclocked_flops;
    }
  }
  {
    std::unordered_map<std::string_view, uint32_t> output_pin;
    for (const auto& port : netlist_.ports()) {
      if (!port.input) {
        output_pin[port.name] = netlist_.pin(port.cell, 0);
      }
    }
    for (const auto& out : constraints_.outputs) {
      const auto it = output_pin.find(out.port);
      if ((!out.check && !out.min_delay_ps) || it == output_pin.end()) {
        continue;
      }
      // An output port inside a clock network forwards the clock: it is not a data endpoint.
      if (const auto pin = it->second; !excluded_[pin] && !netlist_.untimed(pin) && clock_pin_[pin] == 0) {
        endpoints_.push_back({pin,
                              clock_index(out.clock),
                              true,
                              !out.clocked || !out.check,
                              out.min_delay_ps ? clock_index(out.min_clock) : no_index,
                              out.min_delay_ps.value_or(0)});
      }
    }
  }
  std::sort(endpoints_.begin(), endpoints_.end(), [](const Endpoint& a, const Endpoint& b) { return a.pin < b.pin; });
  endpoints_.erase(std::unique(endpoints_.begin(), endpoints_.end(), [](const Endpoint& a, const Endpoint& b) { return a.pin == b.pin; }),
                   endpoints_.end());
  endpoint_slack_.assign(endpoints_.size(), inf);
  endpoint_path_slack_.assign(endpoints_.size(), inf);
  endpoint_path_transition_.assign(endpoints_.size(), 0);
  endpoint_path_group_.assign(endpoints_.size(), 0);
  endpoint_override_.assign(endpoints_.size() * groups_, std::numeric_limits<float>::quiet_NaN());
  for (const auto& relation : constraints_.endpoint_relations) {
    if (relation.group != no_index && relation.group >= groups_) {
      throw std::invalid_argument("endpoint relation group out of range");
    }
    const auto e = endpoint_of(relation.pin);
    if (e == no_endpoint) {
      continue;
    }
    for (size_t g = 0; g < groups_; ++g) {
      if (relation.group == no_index || relation.group == g) {
        // A false path wins over a delay, and of two delays the smaller.
        auto& slot = endpoint_override_[e * groups_ + g];
        slot       = std::isnan(slot) ? relation.setup_ps : relation.setup_ps == inf || slot == inf ? inf : std::min(slot, relation.setup_ps);
      }
    }
  }
  clock_wns_.assign(std::max<size_t>(1, constraints_.clocks.size()) * corners_, 0.f);
  report_.groups = groups_;

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
        std::format("{} flip-flops have no ideal clock from a clock source: not timed", report_.unclocked_flops));
  }
  if (constraints_.clocks.empty() && !endpoints_.empty()) {
    throw std::invalid_argument("endpoints without a clock");
  }
}

void Engine::mark_clock_network() {
  // The ideal clock: from each clock source through buffers and gates to the clock pins of flip-flops. Nothing
  // inside it is timed as data; a flip-flop's clock pin is launched at time 0 with the clock's transition. A
  // source of another clock (a generated clock on a divider, a clock made on a pin) ends the network that
  // reaches it: what is behind belongs to that clock.
  if (constraints_.clocks.size() > 255) {
    throw std::invalid_argument("at most 255 clocks");
  }
  std::vector<std::vector<uint32_t>> sources(constraints_.clocks.size());
  clock_sources_.clear();
  std::unordered_map<uint32_t, uint32_t> source_clock;  // the last clock made on a pin owns it
  for (size_t c = 0; c < constraints_.clocks.size(); ++c) {
    const auto& clock = constraints_.clocks[c];
    if (!clock.source_pins.empty()) {
      for (const auto pin : clock.source_pins) {
        if (pin >= netlist_.pins()) {
          throw std::invalid_argument("clock source pin out of range");
        }
        sources[c].push_back(pin);
      }
    } else if (!clock.port.empty()) {
      for (const auto& port : netlist_.ports()) {
        if (port.input && port.name == clock.port) {
          sources[c].push_back(netlist_.pin(port.cell, 0));
        }
      }
      if (sources[c].empty()) {
        throw std::invalid_argument("clock port not found: " + clock.port);
      }
    }
    for (const auto pin : sources[c]) {
      source_clock[pin] = static_cast<uint32_t>(c);
    }
  }
  std::vector<uint32_t> work;
  const auto            reach = [&](uint32_t pin, uint32_t clock, bool source) {
    if (clock_pin_[pin] != 0) {
      return;
    }
    if (!source && source_clock.contains(pin)) {
      return;
    }
    const auto  cell   = netlist_.cell_of(pin);
    const auto& type   = types_.type(netlist_.type_of(cell));
    const auto& pin_ty = netlist_.pin_type(pin);
    const bool  latch_clock = netlist_.gating_latch(cell) && netlist_.local_of(pin) == type.clock_shape.inputs[0];
    if (!source && netlist_.has_published_clock() && !netlist_.is_driver(pin) && !latch_clock && !pin_ty.clock
        && netlist_.type_of(cell) != Cell_types::output_port_type) {
      const auto local       = netlist_.local_of(pin);
      const bool distributes = std::ranges::any_of(types_.arcs(netlist_.type_of(cell)), [&](const Arc& arc) {
        if (arc.kind != Arc_kind::combinational || arc.from != local || type.clock_gate) {
          return false;
        }
        const auto out  = netlist_.pin(cell, arc.to);
        const auto gate = netlist_.discrete_clock_gates().find(out);
        return netlist_.published_clock_driver(out) && (gate == netlist_.discrete_clock_gates().end() || gate->second.clock == pin);
      });
      if (!distributes) {
        return;
      }
    }
    pin_clock_[pin] = static_cast<uint8_t>(clock);
    if (latch_clock || (pin_ty.clock && type.sequential && !type.clock_gate)) {
      clock_pin_[pin] = 2;
      return;
    }
    clock_pin_[pin] = 1;
    work.push_back(pin);
  };
  for (size_t c = 0; c < constraints_.clocks.size(); ++c) {
    for (const auto pin : sources[c]) {
      if (source_clock[pin] == c) {
        reach(pin, static_cast<uint32_t>(c), true);
        if (constraints_.clocks[c].propagated) {
          clock_sources_.push_back(pin);
        }
      }
    }
    while (!work.empty()) {
      const auto pin = work.back();
      work.pop_back();
      const auto clock = pin_clock_[pin];
      if (netlist_.net_of(pin) != no_index && netlist_.is_driver(pin)) {
        for (const auto sink : netlist_.sinks_of(netlist_.net_of(pin))) {
          reach(sink, clock, false);
        }
      }
      const auto cell  = netlist_.cell_of(pin);
      const auto local = netlist_.local_of(pin);
      if (types_.type(netlist_.type_of(cell)).clock_gate && !netlist_.pin_type(pin).clock) {
        continue;
      }
      for (const auto& arc : types_.arcs(netlist_.type_of(cell))) {
        if (arc.kind == Arc_kind::combinational && arc.from == local) {
          const auto output = netlist_.pin(cell, arc.to);
          if (netlist_.has_published_clock() && !netlist_.published_clock_driver(output)) {
            continue;
          }
          const auto found = netlist_.discrete_clock_gates().find(output);
          if (found != netlist_.discrete_clock_gates().end() && found->second.clock != pin) {
            continue;
          }
          reach(output, clock, false);
        }
      }
    }
  }
}

// Walk only the marked clock topology. Both polarities need their own slews because an inverter
// turns a rising edge at a register into a falling transition upstream. Source edges are the
// zero of insertion delay. Data timing is seeded only at the sequential clock pins.
void Engine::dirty_clock(uint32_t pin) {
  if (pin < clock_index_.size() && clock_index_[pin] != no_index) {
    dirty_clocks_.push_back(pin);
  }
}

// A generated clock may be declared at a buffer after a counter and decoder.
// The counter Q and decoder still carry data. Track their clock waveforms
// separately, from the named master to the declared source only.
uint32_t Engine::waveform_domain(uint32_t pin) const {
  if (clock_pin_[pin]) return pin_clock_[pin];
  const auto it = generation_logic_.find(pin);
  return it == generation_logic_.end() ? no_index : it->second;
}
void Engine::trace_generated_clock_logic() {
  generation_logic_.clear();
  std::map<uint32_t, std::vector<uint32_t>> roots;
  for (const auto& [pin, master] : generated_masters_) roots[master].push_back(pin);
  for (const auto& [master, sources] : roots) {
    auto parents = [&](uint32_t pin, auto visit) {
      const auto net = netlist_.net_of(pin);
      if (net != no_index && !netlist_.is_driver(pin)) {
        visit(netlist_.driver_of(net));
        return;
      }
      const auto cell = netlist_.cell_of(pin), type = netlist_.type_of(cell);
      for (const auto& arc : types_.arcs_into(type, netlist_.local_of(pin))) {
        const auto from = netlist_.pin(cell, arc.from);
        if (arc.kind == Arc_kind::launch_rise) {
          if (clock_pin_[from] && pin_clock_[from] == master) visit(from);
        } else if (arc.kind == Arc_kind::combinational) {
          if (types_.type(type).clock_gate && !netlist_.pin_type(from).clock) continue;
          const auto gate = netlist_.discrete_clock_gates().find(pin);
          if (gate == netlist_.discrete_clock_gates().end() || gate->second.clock == from) visit(from);
        }
      }
    };
    std::vector<uint32_t> candidates;
    std::unordered_set<uint32_t> cone;
    const auto collect = [&](uint32_t pin) {
      if (!clock_pin_[pin] && cone.insert(pin).second) candidates.push_back(pin);
    };
    for (auto source : sources) parents(source, collect);
    for (size_t i = 0; i < candidates.size(); ++i) parents(candidates[i], collect);
    if (cone.empty()) continue;
    std::unordered_set<uint32_t> reached;
    for (auto pin : netlist_.order()) {
      if (!cone.contains(pin) || cut_[pin]) continue;
      bool from_master = false;
      parents(pin, [&](uint32_t from) {
        from_master |= (clock_pin_[from] && pin_clock_[from] == master) || reached.contains(from);
      });
      if (!from_master) continue;
      reached.insert(pin);
      auto [it, added] = generation_logic_.emplace(pin, master);
      if (!added && it->second != master)
        throw std::invalid_argument("shared generated-clock logic has multiple master clocks");
    }
  }
}

void Engine::propagate_clocks(std::span<const uint32_t> dirty) {
  if (clock_sources_.empty()) {
    return;
  }
  const auto& clocks  = constraints_.clocks;
  const bool  partial = !dirty.empty();
  if (!partial) {
    clock_index_.assign(netlist_.pins(), no_index);
    std::vector<std::vector<uint32_t>> domains(clocks.size());
    generated_masters_.clear();
    for (auto p : clock_sources_) {
      if (clocks[pin_clock_[p]].master != no_index) {
        generated_masters_.emplace(p, clocks[pin_clock_[p]].master);
      }
    }
    trace_generated_clock_logic();
    size_t count = 0;
    for (auto p : netlist_.order()) {
      const auto domain = waveform_domain(p);
      if (domain != no_index && (!clock_pin_[p] || clocks[domain].propagated)) {
        clock_index_[p] = count++;
        domains[domain].push_back(p);
      }
    }
    // Sequential clock-to-Q arcs cut the data ordering. Complete each master's clock pins
    // before walking its generated domain, including cascaded dividers.
    std::vector<uint8_t> visited(clocks.size());
    clock_order_.clear();
    clock_order_.reserve(count);
    const auto append = [&](auto& self, uint32_t domain) -> void {
      if (domain >= clocks.size() || visited[domain] == 1) {
        throw std::invalid_argument("invalid generated clock master or cycle");
      }
      if (visited[domain] == 2) {
        return;
      }
      visited[domain] = 1;
      if (clocks[domain].master != no_index) {
        self(self, clocks[domain].master);
      }
      clock_order_.insert(clock_order_.end(), domains[domain].begin(), domains[domain].end());
      visited[domain] = 2;
    };
    for (uint32_t i = 0; i < clocks.size(); ++i) {
      append(append, i);
    }

    clock_position_.resize(count);
    clock_children_.assign(count, {});
    for (size_t i = 0; i < clock_order_.size(); ++i) {
      const auto p                     = clock_order_[i];
      clock_position_[clock_index_[p]] = i;
      const auto master                = generated_masters_.find(p);
      const auto domain                = master != generated_masters_.end() ? master->second : waveform_domain(p);
      const auto link                  = [&](uint32_t parent) {
        if (parent != no_index && clock_index_[parent] != no_index && waveform_domain(parent) == domain) {
          clock_children_[clock_index_[parent]].push_back(p);
        }
      };
      const auto net = netlist_.net_of(p);
      if (net != no_index && !netlist_.is_driver(p)) {
        link(netlist_.driver_of(net));
      } else {
        const auto cell = netlist_.cell_of(p);
        for (const auto& arc : types_.arcs_into(netlist_.type_of(cell), netlist_.local_of(p))) {
          if (arc.kind == Arc_kind::combinational || ((master != generated_masters_.end() || !clock_pin_[p]) && arc.kind == Arc_kind::launch_rise)) {
            link(netlist_.pin(cell, arc.from));
          }
        }
      }
    }
  }
  std::vector<uint32_t> subset;
  if (partial) {
    std::unordered_set<uint32_t> visited;
    for (auto p : dirty) {
      if (visited.insert(p).second) {
        subset.push_back(p);
      }
    }
    for (size_t i = 0; i < subset.size(); ++i) {
      for (auto child : clock_children_[clock_index_[subset[i]]]) {
        if (visited.insert(child).second) {
          subset.push_back(child);
        }
      }
    }
    std::ranges::sort(subset, {}, [&](auto p) { return clock_position_[clock_index_[p]]; });
    clock_edit_pins_ = subset.size();
  }
  const auto& ordered = partial ? subset : clock_order_;
  for (bool early : {false, true}) {
    auto&       edges   = early ? clock_early_ : clock_late_;
    const float missing = early ? inf : -inf;
    if (!partial) {
      edges.assign(clock_order_.size() * corners_ * 2, {missing, 0});
    } else {
      for (auto p : subset) {
        const size_t start = size_t(clock_index_[p]) * corners_ * 2;
        for (size_t i = start; i < start + corners_ * 2; ++i) {
          clock_undo_.push_back(edges[i]);
          journal_.push_back({Undo_kind::clock_edge, static_cast<uint8_t>(early), 0, i});
          edges[i] = {missing, 0};
        }
      }
    }
    const auto at = [&](uint32_t pin, size_t k, unsigned t) -> Clock_edge& {
      return edges[(size_t(clock_index_[pin]) * corners_ + k) * 2 + t];
    };
    for (auto source : clock_sources_) {
      if (generated_masters_.contains(source)) {
        continue;
      }
      if (partial && !std::ranges::binary_search(subset, clock_position_[clock_index_[source]], {}, [&](auto p) {
            return clock_position_[clock_index_[p]];
          })) {
        continue;
      }
      const auto cell = netlist_.cell_of(source);
      const bool port = netlist_.type_of(cell) == Cell_types::input_port_type;
      for (size_t k = 0; k < corners_; ++k) {
        for (unsigned t = 0; t < 2; ++t) {
          // A propagated clock starts with its port drive. set_clock_transition
          // describes the ideal network, and an input data delay is not clock latency.
          const auto seed  = port ? input_seed(cell, k, t, early, true) : Seed{0, 0};
          at(source, k, t) = {seed.arrival, seed.slew};
        }
      }
    }
    for (auto p : ordered) {
      if (at(p, 0, 0).arrival != missing) {
        continue;
      }
      const auto     it        = generated_masters_.find(p);
      const bool     generated = it != generated_masters_.end();
      const uint32_t upstream  = generated ? it->second : waveform_domain(p);
      const auto     on_domain = [&](uint32_t pin) { return pin != no_index && waveform_domain(pin) == upstream; };
      const auto     input     = [&](uint32_t pin, size_t k, unsigned t) {
        if (const auto value = scheduled_latency(pin, t, early)) {
          return Clock_edge{*value, clocks[upstream].slew_ps};
        }
        return clock_index_[pin] == no_index ? Clock_edge{0, clocks[upstream].slew_ps} : at(pin, k, t);
      };
      const auto parent = [&](uint32_t pin, unsigned t) {
        return clock_index_[pin] == no_index || scheduled_latency(pin, t, early) ? no_index : clock_index_[pin] * 2 + t;
      };
      const auto net = netlist_.net_of(p);
      if (net != no_index && !netlist_.is_driver(p)) {
        const auto driver = netlist_.driver_of(net);
        if (!on_domain(driver)) {
          continue;
        }
        for (size_t k = 0; k < corners_; ++k) {
          for (unsigned t = 0; t < 2; ++t) {
            const auto in = input(driver, k, t);
            if (in.arrival == missing) {
              continue;
            }
            const auto [delay, slew] = ramp_through_pole(in.slew, wire_delay(p, k), types_.slew_measure(k));
            at(p, k, t)
                = {in.arrival + delay * constraints_.clock_derates.net[!early], slew, parent(driver, t), !generated && in.inverted};
          }
        }
        continue;
      }
      const auto                           cell = netlist_.cell_of(p), type = netlist_.type_of(cell);
      std::array<std::array<float, 2>, 32> loads;
      for (size_t k = 0; k < corners_; ++k) {
        loads[k] = transition_loads(net, k, early);
      }
      for (const auto& arc : types_.arcs_into(type, netlist_.local_of(p))) {
        const auto from = netlist_.pin(cell, arc.from);
        const auto gate = netlist_.discrete_clock_gates().find(p);
        if (gate != netlist_.discrete_clock_gates().end() && from != gate->second.clock) {
          continue;
        }
        if (!on_domain(from) || !(arc.kind == Arc_kind::combinational || ((generated || !clock_pin_[p]) && arc.kind == Arc_kind::launch_rise))) {
          continue;
        }
        for (size_t k = 0; k < corners_; ++k) {
          for (const auto& [in_t, out_t] : combos_of(arc)) {
            const auto in    = input(from, k, in_t);
            const auto table = types_.table(arc, k, 0, out_t);
            if (in.arrival == missing || table == no_index) {
              continue;
            }
            const Query query{in.slew, loads[k][out_t], 0, 0};
            const auto  arrival = in.arrival + types_.lookup(table, query) * constraints_.clock_derates.cell[!early];
            auto&       out     = at(p, k, out_t);
            if ((early && arrival < out.arrival) || (!early && arrival > out.arrival)) {
              const auto st    = types_.table(arc, k, 1, out_t);
              // Keep the physical master path for insertion and common-path
              // credit. Polarity downstream is relative to the declared waveform.
              const bool flips = arc.kind != Arc_kind::launch_rise && in_t != out_t;
              out              = {arrival,
                                  st == no_index ? in.slew : std::max(0.f, types_.lookup(st, query)),
                                  parent(from, in_t),
                                  !generated && (in.inverted != flips)};
            }
          }
        }
      }
    }
  }
  refresh_clock_metadata();
  if (!partial) {
    return;
  }
  for (auto p : subset) {
    if (clock_pin_[p] != 2 || cut_[p]) {
      continue;
    }
    const auto& c = constraints_.clocks[pin_clock_[p]];
    for (size_t k = 0; k < corners_; ++k) {
      store(arrival_, index(p, c.group * corners_ + k, 0), clock_edge(p, k, 0, false).arrival);
    }
    for (size_t slot = 0; slot < slots_; ++slot) {
      for (unsigned t = 0; t < 2; ++t) {
        store(slew_, index(p, slot, t), clock_edge(p, slot % corners_, t, false).slew);
      }
    }
    const auto cell = netlist_.cell_of(p);
    for (const auto& arc : types_.arcs(netlist_.type_of(cell))) {
      if (arc.from != netlist_.local_of(p)) {
        continue;
      }
      const auto dest = netlist_.pin(cell, arc.to);
      if (arc.kind == Arc_kind::launch_rise) {
        push_forward(dest);
      }
      if (arc.kind != Arc_kind::setup_rise) {
        continue;
      }
      const auto e = endpoint_of(dest);
      if (e == no_endpoint) {
        continue;
      }
      for (size_t slot = 0; slot < slots_; ++slot) {
        for (unsigned t = 0; t < 2; ++t) {
          store(endpoint_required_, (e * slots_ + slot) * 2 + t, endpoint_requirement(e, slot, t));
        }
      }
      push_backward(dest);
      touch_endpoint(dest);
    }
  }
}
void Engine::refresh_clock_metadata() {
  clock_credit_cache_.assign(corners_, {});
  clock_has_children_.assign(clock_order_.size() * 2, 0);
  for (size_t i = 0; i < clock_children_.size(); ++i) {
    if (!clock_children_[i].empty()) {
      clock_has_children_[i * 2] = clock_has_children_[i * 2 + 1] = 1;
    }
  }
  report_.unsupported_clock_phase = false;
  for (auto p : clock_order_) {
    if (!netlist_.pin_type(p).clock) {
      continue;
    }
    for (size_t k = 0; k < corners_; ++k) {
      const auto edge                  = (size_t(clock_index_[p]) * corners_ + k) * 2;
      report_.unsupported_clock_phase |= clock_early_[edge].inverted || clock_late_[edge].inverted;
    }
  }
  constexpr std::string_view inverted_note = "inverted branch clocks a sequential pin: half-cycle endpoint timing is unsupported";
  std::erase(report_.unsupported, inverted_note);
  if (report_.unsupported_clock_phase) {
    report_.unsupported.emplace_back(inverted_note);
  }
  clock_spread_ = !std::equal(clock_early_.begin(), clock_early_.end(), clock_late_.begin(), [](const auto& a, const auto& b) {
    return a.arrival == b.arrival;
  });
}
// Credit only the identical prefix of the selected early and late clock paths, including polarity.
float Engine::common_clock_credit(uint32_t early_pin, uint32_t late_pin, size_t corner) const {
  if (early_pin == no_index || late_pin == no_index || clock_index_.empty() || clock_index_[early_pin] == no_index
      || clock_index_[late_pin] == no_index || pin_clock_[early_pin] != pin_clock_[late_pin]) {
    return 0;
  }
  if (scheduled_latency(early_pin, 0, true) || scheduled_latency(late_pin, 0, false)) {
    return 0;
  }
  const auto index = [&](uint32_t state) { return (static_cast<size_t>(state / 2) * corners_ + corner) * 2 + state % 2; };
  uint32_t   a = clock_index_[early_pin] * 2, b = clock_index_[late_pin] * 2;
  if (a != b) {
    if (!clock_has_children_[a]) {
      a = clock_early_[index(a)].parent;
    }
    if (!clock_has_children_[b]) {
      b = clock_late_[index(b)].parent;
    }
  }
  if (a == no_index || b == no_index) {
    return 0;
  }
  const uint64_t key   = (static_cast<uint64_t>(a) << 32) | b;
  auto&          cache = clock_credit_cache_[corner];
  if (const auto it = cache.find(key); it != cache.end()) {
    return it->second;
  }
  const auto path = [&](uint32_t state, const std::vector<Clock_edge>& edges) {
    std::vector<uint32_t> states;
    for (; state != no_index; state = edges[index(state)].parent) {
      states.push_back(state);
    }
    return states;
  };
  auto  early = path(a, clock_early_), late = path(b, clock_late_);
  float credit = 0;
  while (!early.empty() && !late.empty() && early.back() == late.back()) {
    const auto i = index(early.back());
    credit       = clock_late_[i].arrival - clock_early_[i].arrival;
    early.pop_back();
    late.pop_back();
  }
  const float result = std::max(0.f, credit);
  cache.emplace(key, result);
  return result;
}

// Removing common-clock pessimism can make a different data launch critical.
// Walk backwards in order of slack lost relative to the raw worst arrival.
// Every step costs at least zero, so the first visit to a pin/transition
// dominates all later visits. Stop when no remaining path can win.
float Engine::setup_clock_credit(uint32_t pin, size_t slot, unsigned transition, uint32_t capture,
                                 std::vector<Path_point>* path) const {
  if (clock_index_[capture] == no_index) {
    return 0;
  }
  const size_t corner = slot % corners_;
  struct Step {
    float    deficit;
    uint32_t pin, transition;
    size_t   next;
  };
  const auto later = [](const Step& a, const Step& b) {
    return std::tie(a.deficit, a.pin, a.transition) > std::tie(b.deficit, b.pin, b.transition);
  };
  std::priority_queue<Step, std::vector<Step>, decltype(later)> todo(later);
  std::unordered_set<size_t>                                    visited;
  std::vector<Step>                                             trace;
  constexpr auto                                                end = std::numeric_limits<size_t>::max();
  todo.push({0, pin, transition, end});
  float  best   = inf;
  size_t winner = end;
  while (!todo.empty() && todo.top().deficit < best) {
    const Step step = todo.top();
    todo.pop();
    const auto p = step.pin, t = step.transition;
    const auto at = index(p, slot, t);
    if (!visited.insert(at).second) {
      continue;
    }
    const size_t next = path ? trace.size() : end;
    if (path) {
      trace.push_back(step);
    }
    const auto cell = netlist_.cell_of(p), type = netlist_.type_of(cell);
    if (clock_pin_[p] == 2 || type == Cell_types::input_port_type) {
      const float credit = clock_pin_[p] == 2 ? common_clock_credit(capture, p, corner) : 0;
      if (step.deficit + credit < best) {
        best   = step.deficit + credit;
        winner = next;
      }
      continue;
    }
    const auto net = netlist_.net_of(p);
    if (net != no_index && !netlist_.is_driver(p)) {
      // The net has just one upstream pin, so it cannot change path order.
      todo.push({step.deficit, netlist_.driver_of(net), t, next});
      continue;
    }
    const auto* base = types_.arcs(type).data();
    for (const auto& arc : types_.arcs_into(type, netlist_.local_of(p))) {
      if (!timed_arc(arc) || types_.table(arc, corner, 0, t) == no_index) {
        continue;
      }
      const auto from = netlist_.pin(cell, arc.from);
      if (cut_[from]) {
        continue;
      }
      const auto combos = combos_of(arc);
      const auto offset
          = cell_base_[cell] + arc_offset_[types_.type(type).first_arc + (&arc - base)] * slots_ + slot * combos.size();
      for (size_t m = 0; m < combos.size(); ++m) {
        if (static_cast<unsigned>(combos[m].out) != t) {
          continue;
        }
        const auto  in        = static_cast<unsigned>(combos[m].in);
        const float candidate = arrival_[index(from, slot, in)] + arc_delay_[offset + m];
        if (candidate == -inf) {
          continue;
        }
        const float deficit = step.deficit + std::max(0.f, arrival_[at] - candidate);
        if (deficit < best) {
          todo.push({deficit, from, in, next});
        }
      }
    }
  }
  if (path && winner != end) {
    const auto launch_deficit = trace[winner].deficit;
    for (size_t i = winner; i != end; i = trace[i].next) {
      const auto& point = trace[i];
      path->push_back(
          {point.pin, point.transition, arrival_[index(point.pin, slot, point.transition)] - (launch_deficit - point.deficit)});
    }
  }
  return best == inf ? 0 : best;
}

std::optional<float> Engine::scheduled_latency(uint32_t pin, unsigned t, bool early) const {
  if (!constraints_.sink_latencies.empty()) {
    const auto entry = constraints_.sink_latencies.find(pin);
    if (entry != constraints_.sink_latencies.end()) {
      const auto value = entry->second[(early ? 0 : 2) + t];
      if (std::isfinite(value)) {
        return value;
      }
    }
  }
  return {};
}

Engine::Clock_edge Engine::clock_edge(uint32_t pin, size_t k, unsigned t, bool early) const {
  if (pin >= pin_clock_.size() || k >= corners_ || t >= 2) {
    throw std::out_of_range("clock edge index out of range");
  }
  if (const auto value = scheduled_latency(pin, t, early)) {
    return {*value, constraints_.clocks[pin_clock_[pin]].slew_ps};
  }
  if (pin >= clock_index_.size() || clock_index_[pin] == no_index) {
    return {0, constraints_.clocks[pin_clock_[pin]].slew_ps};
  }
  const auto edge = (early ? clock_early_ : clock_late_)[(size_t(clock_index_[pin]) * corners_ + k) * 2 + t];
  if (!std::isfinite(edge.arrival)) {
    throw std::runtime_error(
        std::format("uncharacterized propagated clock path at cell {} pin {}", netlist_.cell_of(pin), netlist_.pin_type(pin).name));
  }
  return edge;
}

void Engine::compute_loads() {
  load_.assign(netlist_.nets() * corners_, 0.f);
  early_load_.assign(netlist_.nets(), 0.f);
  virt_levels_.clear();
  for (uint32_t net = 0; net < netlist_.nets(); ++net) {
    compute_net_load(net);
  }
  plan_virtual_trees();
  if (!virt_levels_.empty()) {
    for (uint32_t net = 0; net < netlist_.nets(); ++net) {
      if (virtual_net(net)) {
        compute_net_load(net);
      }
    }
  }
}

// Virtual buffer trees: decide, from the direct loads just computed, which virtual roots the placement's expansion
// will rebuild -- over the fanout and over the driver's Liberty limits (capacitance, or the output slew at that
// load, degraded through the longest sink wire, against the max transition) -- and plan the balanced tree each one
// is timed as until then. Branching b = ceil(N^(1/L)) <= fanout over L = ceil(log_fanout N) buffer levels; the
// net's wire capacitance is split evenly over the L + 1 driving stages and then over the drivers of each stage, so
// the root sees b buffer inputs plus its share of the wire, an inner buffer b buffer inputs plus a smaller share,
// a leaf buffer b sink pins plus its share. The estimate never assumes a wire shorter than the direct net's.
void Engine::plan_virtual_trees() {
  if (wire_.net_virtual.empty()) {
    return;
  }
  const size_t C = corners_, stride = max_virtual_levels + 1;
  const auto   nets = std::min<size_t>(netlist_.nets(), wire_.net_virtual.size());
  virt_levels_.assign(netlist_.nets(), 0);
  virt_buffer_.assign(netlist_.nets(), no_index);
  virt_stage_load_.assign(netlist_.nets() * C * stride, 0.f);
  direct_load_.assign(netlist_.nets() * C, 0.f);
  for (uint32_t net = 0; net < nets; ++net) {
    const auto& vt = wire_.net_virtual[net];
    if (vt.buffer_type == no_index || vt.buffer_type >= types_.types()) {
      continue;
    }
    const auto driver = netlist_.driver_of(net);
    if (driver == no_index || clock_pin_[driver] != 0) {
      continue;
    }
    const auto dcell = netlist_.cell_of(driver);
    const auto dtype = netlist_.type_of(dcell);
    if (dtype == Cell_types::input_port_type || dtype == Cell_types::output_port_type) {
      continue;  // a port's drive is the environment's (the expansion leaves it alone too)
    }
    const auto dlocal = netlist_.local_of(driver);
    // The buffer's input pin and its one delay arc.
    const auto  btype = vt.buffer_type;
    const Arc*  barc  = nullptr;
    for (const auto& arc : types_.arcs(btype)) {
      if (arc.kind == Arc_kind::combinational) {
        barc = &arc;
        break;
      }
    }
    if (!barc) {
      continue;
    }
    size_t sinks = 0;
    float  sink_cap[32]{};
    float  far_wire = 0;
    for (const auto sink : netlist_.sinks_of(net)) {
      if (netlist_.untimed(sink)) {
        continue;
      }
      ++sinks;
      const auto type = netlist_.type_of(netlist_.cell_of(sink));
      for (size_t k = 0; k < C; ++k) {
        sink_cap[k] += type == Cell_types::output_port_type
                           ? port_load_[netlist_.cell_of(sink)]
                           : std::max(types_.pin_cap(type, netlist_.local_of(sink), k, 0),
                                      types_.pin_cap(type, netlist_.local_of(sink), k, 1));
      }
      far_wire = std::max(far_wire, wire_delay(sink, 0));
    }
    const auto fanout = std::max<uint32_t>(2, vt.fanout);
    if (sinks <= fanout) {
      continue;
    }
    // Over the driver's limits as a direct net?
    bool over = false;
    for (size_t k = 0; k < C && !over; ++k) {
      const float load     = load_[net * C + k];
      const float cap_lim  = types_.max_capacitance(dtype, dlocal, k);
      const float tran_lim = types_.max_transition(dtype, dlocal, k);
      if (cap_lim != inf && cap_lim > 0 && load > cap_lim) {
        over = true;
        break;
      }
      if (tran_lim == inf || tran_lim <= 0) {
        continue;
      }
      for (const auto& arc : types_.arcs_into(dtype, dlocal)) {
        if (!timed_arc(arc)) {
          continue;
        }
        for (unsigned t = 0; t < 2 && !over; ++t) {
          const auto slew_table = types_.table(arc, k, 1, t);
          if (slew_table == no_index) {
            continue;
          }
          const float out  = std::max(0.f, types_.lookup(slew_table, Query{10.f, load, 0.f, 0.f}));
          const float far  = ramp_through_pole(out, far_wire, types_.slew_measure(k)).second;
          over             = out > tran_lim || far > tran_lim;
        }
        break;  // the first timed arc stands for the drive
      }
    }
    if (!over) {
      continue;
    }
    // The balanced tree.
    const double n      = static_cast<double>(sinks);
    auto         levels = static_cast<size_t>(std::ceil(std::log(n) / std::log(static_cast<double>(fanout)) - 1e-9));
    levels              = std::clamp<size_t>(levels, 1, max_virtual_levels);
    const double branch = std::min<double>(fanout, std::ceil(std::pow(n, 1.0 / static_cast<double>(levels)) - 1e-9));
    const auto   bin    = barc->from;
    virt_levels_[net]   = static_cast<uint8_t>(levels);
    virt_buffer_[net]   = btype;
    for (size_t k = 0; k < C; ++k) {
      direct_load_[net * C + k] = load_[net * C + k];
    }
    for (size_t k = 0; k < C; ++k) {
      const float  cin      = std::max(types_.pin_cap(btype, bin, k, 0), types_.pin_cap(btype, bin, k, 1));
      const float  wire_cap = wire_.net_cap_ff.empty() ? 0.f : wire_.net_cap_ff[net * C + k];
      const double per_stage_wire = wire_cap / static_cast<double>(levels + 1);
      const double leaves         = std::ceil(n / branch);
      for (size_t j = 0; j <= levels; ++j) {
        const double drivers = std::min(std::pow(branch, static_cast<double>(j)), leaves);
        const double pins    = j < levels ? branch * cin : branch * (sink_cap[k] / n);
        virt_stage_load_[(net * C + k) * stride + j] = static_cast<float>(pins + per_stage_wire / std::max(1.0, drivers));
      }
    }
  }
}

std::pair<float, float> Engine::virtual_sink(uint32_t net, uint32_t sink, size_t k, unsigned t, float slew) const {
  const size_t C = corners_, stride = max_virtual_levels + 1;
  const auto   levels = virt_levels_[net];
  const auto   btype  = virt_buffer_[net];
  const Arc*   barc   = nullptr;
  for (const auto& arc : types_.arcs(btype)) {
    if (arc.kind == Arc_kind::combinational) {
      barc = &arc;
      break;
    }
  }
  // Each stage's wire. The direct net's Elmore delay to this sink is about R_path * W / 2 (W its whole wire and
  // pin load), which gives the path resistance; a stage covers 1/(levels + 1) of that path and drives only its
  // own load, so its Elmore delay is that share of R_path times the stage load (the whole stage load, not half:
  // kept pessimistic), and never more than its share of the direct delay.
  const float direct    = wire_delay(sink, k);
  const float net_total = load_[static_cast<size_t>(net) * C + k] > 0 ? direct_load_[static_cast<size_t>(net) * C + k] : 0.f;
  const float r_share   = net_total > 0 ? 2.f * direct / net_total / static_cast<float>(levels + 1) : 0.f;
  const auto  stage_tau = [&](size_t j) {
    const float load = virt_stage_load_[(static_cast<size_t>(net) * C + k) * stride + j];
    return std::min(direct / static_cast<float>(levels + 1), r_share * load);
  };
  const auto& m     = types_.slew_measure(k);
  float       delay = 0;
  for (size_t j = 1; j <= levels; ++j) {
    const auto [dw, sw] = ramp_through_pole(slew, stage_tau(j - 1), m);
    delay += dw;
    slew                 = sw;
    const Query q{slew, virt_stage_load_[(static_cast<size_t>(net) * C + k) * stride + j], 0.f, 0.f};
    const auto  dt = barc ? types_.table(*barc, k, 0, t) : no_index;
    const auto  st = barc ? types_.table(*barc, k, 1, t) : no_index;
    delay += dt == no_index ? 0.f : types_.lookup(dt, q);
    slew = st == no_index ? slew : std::max(0.f, types_.lookup(st, q));
  }
  const auto [dw, sw] = ramp_through_pole(slew, stage_tau(levels), m);
  return {delay + dw, sw};
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
  if (virtual_net(net)) {  // the root drives the first stage of its planned tree
    const size_t stride = max_virtual_levels + 1;
    for (size_t k = 0; k < C; ++k) {
      sum[k] = virt_stage_load_[(static_cast<size_t>(net) * C + k) * stride];
    }
    early = virt_stage_load_[(static_cast<size_t>(net) * C + hold_corner_) * stride];
  }
  bool changed = false;
  for (size_t k = 0; k < C; ++k) {
    changed = changed || load_[net * C + k] != sum[k];
    store(load_, net * C + k, sum[k]);
  }
  store(early_load_, net, early);
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
    if (const auto entry = port_input_[port.cell]; entry != no_index && !constraints_.inputs[entry].late) {
      continue;
    }
    const auto g = input_group(port.cell);
    for (size_t k = 0; k < C; ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        const auto seed                    = input_seed(port.cell, k, t, false);
        arrival_[index(pin, g * C + k, t)] = seed.arrival;
        slew_[index(pin, g * C + k, t)]    = seed.slew;
      }
    }
  }
  for (uint32_t pin = 0; pin < netlist_.pins(); ++pin) {
    if (clock_pin_[pin] == 2 && !cut_[pin]) {
      const auto& clock      = constraints_.clocks[pin_clock_[pin]];
      for (size_t k = 0; k < C; ++k) {
        arrival_[index(pin, clock.group * C + k, 0)] = clock_edge(pin, k, 0, false).arrival;
      }
      for (size_t slot = 0; slot < slots_; ++slot) {
        for (unsigned t = 0; t < 2; ++t) {
          slew_[index(pin, slot, t)] = clock_edge(pin, slot % C, t, false).slew;
        }
      }
    }
  }
}

void Engine::forward_late() {
  propagate_clocks();
  seed_late();
  for (const uint32_t p : netlist_.order()) {
    recompute_arrival(p);
  }
}

// The late arrival, slew, depth and worst predecessor of one pin from its sources, which must be current.
// A pin the engine does not time keeps its seed.
void Engine::recompute_arrival(uint32_t p) {
  const size_t C = corners_, S = slots_;
  if (netlist_.untimed(p) || clock_pin_[p] != 0 || cut_[p]) {
    return;
  }
  const auto net  = netlist_.net_of(p);
  const auto cell = netlist_.cell_of(p);
  const auto type = netlist_.type_of(cell);
  if (type == Cell_types::input_port_type) {
    // Behind a driving cell the launch depends on the load of the port's net, which an edit may have changed.
    if (const auto entry = port_input_[cell]; entry != no_index && constraints_.inputs[entry].drive_type != no_index
                                              && constraints_.inputs[entry].late) {
      const auto g = constraints_.inputs[entry].group;
      for (size_t k = 0; k < C; ++k) {
        for (unsigned t = 0; t < 2; ++t) {
          const auto seed = input_seed(cell, k, t, false);
          store(arrival_, index(p, g * C + k, t), seed.arrival);
          store(slew_, index(p, g * C + k, t), seed.slew);
        }
      }
    }
    return;
  }
  if (net == no_index && netlist_.pin_type(p).direction == Direction::input) {
    // A sink with no driver (never connected, or taken off its net by an edit): nothing arrives.
    for (size_t s = 0; s < S; ++s) {
      for (unsigned t = 0; t < 2; ++t) {
        store(arrival_, index(p, s, t), -inf);
        store(slew_, index(p, s, t), 0.f);
        store(depth_, index(p, s, t), uint16_t{0});
        if (s % C == options_.path_corner) {
          store_predecessor(path_index(p, s / C, t), {});
        }
      }
    }
    return;
  }
  if (net != no_index && !netlist_.is_driver(p)) {
    const auto driver  = netlist_.driver_of(net);
    const bool virt    = virtual_net(net);
    for (size_t s = 0; s < S; ++s) {
      const size_t k  = s % C;
      const float  wd = wire_delay(p, k);
      for (unsigned t = 0; t < 2; ++t) {
        const auto from = index(driver, s, t), to = index(p, s, t);
        const auto [d, sl] = virt ? virtual_sink(net, p, k, t, slew_[from]) : ramp_through_pole(slew_[from], wd, types_.slew_measure(k));
        store(arrival_, to, arrival_[from] + d);
        store(slew_, to, sl);
        store(depth_, to, depth_[from]);
        if (k == options_.path_corner) {
          store_predecessor(path_index(p, s / C, t), {driver, static_cast<uint8_t>(t)});
        }
      }
    }
    return;
  }
  const auto local = netlist_.local_of(p);
  const auto arcs  = types_.arcs_into(type, local);
  if (arcs.empty()) {
    return;
  }
  const auto* type_arcs = types_.arcs(type).data();
  for (size_t s = 0; s < S; ++s) {
    const size_t k = s % C;
    // Start the pin from nothing: this is also the update of a pin that already has a value.
    for (unsigned t = 0; t < 2; ++t) {
      store(arrival_, index(p, s, t), -inf);
      store(slew_, index(p, s, t), 0.f);
      store(depth_, index(p, s, t), uint16_t{0});
      if (k == options_.path_corner) {
        store_predecessor(path_index(p, s / C, t), {});
      }
    }
    const auto load = transition_loads(net, k, false);
    for (const auto& arc : arcs) {
      if (!timed_arc(arc)) {
        continue;
      }
      const auto from_pin = netlist_.pin(cell, arc.from);
      if (cut_[from_pin]) {
        continue;
      }
      const auto combos = combos_of(arc);
      const auto offset = cell_base_[cell] + (arc_offset_[types_.type(type).first_arc + (&arc - type_arcs)]) * S + s * combos.size();
      for (size_t m = 0; m < combos.size(); ++m) {
        const auto in = static_cast<unsigned>(combos[m].in), out = static_cast<unsigned>(combos[m].out);
        const auto from = index(from_pin, s, in), to = index(p, s, out);
        if (arrival_[from] == -inf) {
          delay_changed_ = delay_changed_ || arc_delay_[offset + m] != 0.f;
          store(arc_delay_, offset + m, 0.f);
          continue;
        }
        const auto delay_table = types_.table(arc, k, 0, out);
        if (delay_table == no_index) {
          continue;
        }
        const Query query{slew_[from], load[out], 0.f, 0.f};
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
            store_predecessor(path_index(p, s / C, out), {from_pin, static_cast<uint8_t>(in)});
          }
        }
        store(slew_, to, std::max(slew_[to], out_slew));
      }
    }
  }
}

// The requirement of one endpoint in a slot (a corner of a launch group) and transition: the time to the capture
// edge less the setup (or the output delay), uncertainty and depth margin. +inf: not checked.
float Engine::endpoint_requirement(size_t e, size_t s, unsigned t, std::vector<Path_point>* path) const {
  const auto&  endpoint = endpoints_[e];
  const auto   pin      = endpoint.pin;
  const auto   at       = index(pin, s, t);
  const size_t k = s % corners_, g = s / corners_;
  if (arrival_[at] == -inf) {
    return inf;
  }
  // The relation between the group and the capture clock, or the one given for this endpoint.
  float relation = endpoint.unclocked ? inf : setup_relation(g, endpoint.clock);
  if (const float given = endpoint_override_[e * groups_ + g]; !std::isnan(given)) {
    relation = given == inf || (relation == inf && !endpoint.unclocked) ? inf : given;
  }
  if (relation == inf) {
    return inf;
  }
  const auto& clock       = constraints_.clocks[endpoint.clock];
  const auto  cell        = netlist_.cell_of(pin);
  float       requirement = inf;
  if (endpoint.port) {
    requirement = relation - port_delay_[cell];
  } else {
    float      setup         = -inf;
    uint32_t   worst_capture = no_index;
    const auto type          = netlist_.type_of(cell);
    for (const auto& arc : types_.arcs_into(type, netlist_.local_of(pin))) {
      if (arc.kind != Arc_kind::setup_rise) {
        continue;
      }
      const auto table = types_.table(arc, k, 2, t);
      if (table == no_index) {
        continue;
      }
      const auto  clock_pin = netlist_.pin(cell, arc.from);
      const auto  capture   = clock_edge(clock_pin, k, 0, true);
      const Query query{0.f, 0.f, capture.slew, slew_[at]};
      const float credit = clock_spread_ ? setup_clock_credit(pin, s, t, clock_pin, nullptr) : 0;
      const float check  = types_.lookup(table, query) - capture.arrival - credit;
      if (check > setup) {
        setup         = check;
        worst_capture = clock_pin;
      }
    }
    if (setup == -inf) {
      return inf;
    }
    if (path && clock_spread_) {
      setup_clock_credit(pin, s, t, worst_capture, path);
    }
    requirement = relation - setup;
  }
  requirement        -= endpoint.unclocked ? 0.f : clock.uncertainty_ps;
  const float margin  = options_.depth_margin[k];
  if (margin > 0.f) {
    requirement -= margin * arrival_[at] / std::sqrt(static_cast<float>(std::max<uint16_t>(depth_[at], 1)));
  }
  return requirement;
}

float Engine::endpoint_seed(size_t e, size_t s, unsigned t) const {
  const float raw = endpoint_required_[(e * slots_ + s) * 2 + t];
  return rebased_ ? raw - std::min(0.f, clock_wns_[endpoints_[e].clock * corners_ + s % corners_]) : raw;
}

void Engine::seed_endpoints(bool rebased, bool refresh) {
  rebased_ = rebased;
  std::fill(required_.begin(), required_.end(), inf);
  if (refresh) {
    endpoint_required_.resize(endpoints_.size() * slots_ * 2);
  }
  for (size_t e = 0; e < endpoints_.size(); ++e) {
    for (size_t s = 0; s < slots_; ++s) {
      for (unsigned t = 0; t < 2; ++t) {
        if (refresh) {
          endpoint_required_[(e * slots_ + s) * 2 + t] = endpoint_requirement(e, s, t);
        }
        const float r = endpoint_seed(e, s, t);
        if (r != inf) {
          required_[index(endpoints_[e].pin, s, t)] = std::min(required_[index(endpoints_[e].pin, s, t)], r);
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
  const size_t C = corners_, S = slots_;
  const auto   net  = netlist_.net_of(p);
  const auto   cell = netlist_.cell_of(p);
  const auto   local = netlist_.local_of(p);
  const auto   type  = netlist_.type_of(cell);
  // The endpoint of this pin, if any.
  const size_t e         = endpoint_of(p);
  const auto*  type_arcs = types_.arcs(type).data();
  const auto   arcs      = types_.arcs(type);
  const bool   driver    = net != no_index && netlist_.is_driver(p);
  for (size_t s = 0; s < S; ++s) {
    const size_t k = s % C;
    for (unsigned t = 0; t < 2; ++t) {
      float requirement = e == no_endpoint ? inf : endpoint_seed(e, s, t);
      if (driver) {
        for (const auto sink : netlist_.sinks_of(net)) {
          if (netlist_.untimed(sink) || clock_pin_[sink] != 0) {
            continue;
          }
          const float through = virtual_net(net) ? virtual_sink(net, sink, k, t, slew_[index(p, s, t)]).first : wire_delay(sink, k);
          requirement         = std::min(requirement, required_[index(sink, s, t)] - through);
        }
      }
      for (size_t j = 0; j < arcs.size(); ++j) {
        const auto& arc = arcs[j];
        if (arc.kind != Arc_kind::combinational || arc.from != local) {
          continue;
        }
        const auto combos = combos_of(arc);
        const auto offset
            = cell_base_[cell] + (arc_offset_[types_.type(type).first_arc + (&arc - type_arcs)]) * S + s * combos.size();
        const auto to_pin = netlist_.pin(cell, arc.to);
        for (size_t m = 0; m < combos.size(); ++m) {
          if (static_cast<unsigned>(combos[m].in) != t) {
            continue;
          }
          const auto out_required = required_[index(to_pin, s, static_cast<unsigned>(combos[m].out))];
          if (out_required != inf) {
            requirement = std::min(requirement, out_required - arc_delay_[offset + m]);
          }
        }
      }
      store(required_, index(p, s, t), requirement);
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
      for (size_t g = 0; g < groups_; ++g) {
        for (unsigned t = 0; t < 2; ++t) {
          const auto at = index(endpoint.pin, g * C + k, t);
          if (arrival_[at] == -inf || required_[at] == inf) {
            continue;
          }
          const float s = required_[at] - arrival_[at];
          slack_k       = std::min(slack_k, s);
          if (k == options_.path_corner && s < endpoint_path_slack_[e]) {
            endpoint_path_slack_[e]      = s;
            endpoint_path_transition_[e] = static_cast<uint8_t>(t);
            endpoint_path_group_[e]      = static_cast<uint8_t>(g);
          }
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
    for (size_t s = 0; s < slots_; ++s) {
      if (!(options_.setup_corners >> (s % C) & 1u)) {
        continue;
      }
      for (unsigned t = 0; t < 2; ++t) {
        const auto at = index(p, s, t);
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
  seed_endpoints(false, true);
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

void Engine::evaluate_edits() {
  settle();
  measure_endpoints();
  forward_early();
  check_hold();
  check_electrical();
  report_.hold_computed = true;
  detailed_edits_       = journal_on_;
}

// The two earliest distinct launch clocks are retained exactly. Any additional
// launch stays in a lower bound and gets no credit in the endpoint comparison.
void Engine::Hold_launch::consider(float& first, uint32_t origin, float arrival) {
  if (origin == pin && first != inf) {
    first = std::min(first, arrival);
    return;
  }
  if (origin == second_pin && second != inf) {
    second = std::min(second, arrival);
  } else if (arrival < second) {
    others     = std::min(others, second);
    second_pin = origin;
    second     = arrival;
  } else {
    others = std::min(others, arrival);
    return;
  }
  if (second < first) {
    std::swap(pin, second_pin);
    std::swap(first, second);
  }
}

void Engine::forward_early(bool trace_origins) {
  const size_t h = hold_corner_, G = groups_;
  // The hold walk's state is allocated here, on first use: a caller that only ranks never pays for it.
  early_arrival_.assign(netlist_.pins() * G * 2, inf);
  early_slew_.assign(netlist_.pins() * G * 2, 0.f);
  hold_launch_.assign((clock_spread_ || trace_origins) ? early_arrival_.size() : 0, {});
  const auto at = [&](uint32_t pin, size_t g, unsigned t) { return (static_cast<size_t>(pin) * G + g) * 2 + t; };
  for (const auto& port : netlist_.ports()) {
    const auto pin = netlist_.pin(port.cell, 0);
    if (!port.input || clock_pin_[pin] != 0 || cut_[pin]) {
      continue;
    }
    if (const auto entry = port_input_[port.cell]; entry != no_index && !constraints_.inputs[entry].early) {
      continue;
    }
    const auto g = input_group(port.cell);
    for (unsigned t = 0; t < 2; ++t) {
      const auto seed               = input_seed(port.cell, h, t, true);
      early_arrival_[at(pin, g, t)] = seed.arrival;
      early_slew_[at(pin, g, t)]    = seed.slew;
    }
  }
  for (uint32_t pin = 0; pin < netlist_.pins(); ++pin) {
    if (clock_pin_[pin] == 2 && !cut_[pin]) {
      const auto& clock                       = constraints_.clocks[pin_clock_[pin]];
      early_arrival_[at(pin, clock.group, 0)] = clock_edge(pin, h, 0, true).arrival;
      if (!hold_launch_.empty()) {
        hold_launch_[at(pin, clock.group, 0)].pin = pin;
      }
      for (size_t g = 0; g < G; ++g) {
        early_slew_[at(pin, g, 0)] = clock_edge(pin, h, 0, true).slew;
        early_slew_[at(pin, g, 1)] = clock_edge(pin, h, 1, true).slew;
      }
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
      for (size_t g = 0; g < G; ++g) {
        for (unsigned t = 0; t < 2; ++t) {
          const auto [d, sl]          = virtual_net(net) ? virtual_sink(net, p, h, t, early_slew_[at(driver, g, t)])
                                                             : ramp_through_pole(early_slew_[at(driver, g, t)], wd, types_.slew_measure(h));
          early_arrival_[at(p, g, t)] = early_arrival_[at(driver, g, t)] + d;
          early_slew_[at(p, g, t)]    = sl;
          if (!hold_launch_.empty()) {
            auto launch                = hold_launch_[at(driver, g, t)];
            launch.second             += d;
            launch.others             += d;
            hold_launch_[at(p, g, t)]  = launch;
          }
        }
      }
      continue;
    }
    const auto  cell = netlist_.cell_of(p);
    const auto  type = netlist_.type_of(cell);
    const auto  arcs = types_.arcs_into(type, netlist_.local_of(p));
    const auto  load = transition_loads(net, h, true);
    for (size_t g = 0; g < G; ++g) {
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
          if (early_arrival_[at(from_pin, g, in)] == inf) {
            continue;
          }
          const auto delay_table = types_.table(arc, h, 0, out);
          if (delay_table == no_index) {
            continue;
          }
          const Query query{early_slew_[at(from_pin, g, in)], load[out], 0.f, 0.f};
          const float delay             = types_.lookup(delay_table, query);
          const auto  slew_table        = types_.table(arc, h, 1, out);
          const float out_slew          = slew_table == no_index ? 0.f : std::max(0.f, types_.lookup(slew_table, query));
          auto&       best              = early_arrival_[at(p, g, out)];
          const float candidate         = early_arrival_[at(from_pin, g, in)] + delay;
          if (!hold_launch_.empty()) {
            const auto from   = hold_launch_[at(from_pin, g, in)];
            auto&      winner = hold_launch_[at(p, g, out)];
            winner.consider(best, from.pin, candidate);
            if (from.second != inf) {
              winner.consider(best, from.second_pin, from.second + delay);
            }
            winner.others = std::min(winner.others, from.others + delay);
          } else {
            best = std::min(best, candidate);
          }
          slew_min[out]                 = std::min(slew_min[out], out_slew);
        }
      }
      for (unsigned t = 0; t < 2; ++t) {
        if (slew_min[t] != inf) {
          early_slew_[at(p, g, t)] = slew_min[t];
        }
      }
    }
  }
}

Engine::Hold_path Engine::hold_path(size_t e) const {
  Hold_path   result;
  const auto& endpoint      = endpoints_[e];
  const auto  capture_clock = endpoint.port ? endpoint.hold_clock : endpoint.clock;
  if (capture_clock == no_index) {
    return result;
  }
  const auto   pin = endpoint.pin, cell = netlist_.cell_of(pin);
  const auto   type     = netlist_.type_of(cell);
  const size_t h        = hold_corner_;
  const auto   consider = [&](float slack, uint32_t launch, uint32_t capture) {
    if (slack < result.slack_ps) {
      result = {slack, launch, capture};
    }
  };
  for (size_t g = 0; g < groups_; ++g) {
    const float relation = hold_relation(g, capture_clock);
    if (relation == inf || endpoint_override_[e * groups_ + g] == inf) {
      continue;
    }
    for (unsigned t = 0; t < 2; ++t) {
      const size_t index = (static_cast<size_t>(pin) * groups_ + g) * 2 + t;
      const auto   first = early_arrival_[index];
      if (first == inf) {
        continue;
      }
      const auto* origins = hold_launch_.empty() ? nullptr : &hold_launch_[index];
      if (endpoint.port) {
        consider(first - relation + endpoint.output_min_ps, origins ? origins->pin : no_index, no_index);
        continue;
      }
      for (const auto& arc : types_.arcs_into(type, netlist_.local_of(pin))) {
        if (arc.kind != Arc_kind::hold_rise) {
          continue;
        }
        const auto table = types_.table(arc, h, 2, t);
        if (table == no_index) {
          continue;
        }
        const auto  clock_pin = netlist_.pin(cell, arc.from);
        const auto  capture   = clock_edge(clock_pin, h, 0, false);
        const Query query{0.f, 0.f, capture.slew, early_slew_[index]};
        float       adjusted = first;
        uint32_t    launch   = origins ? origins->pin : no_index;
        if (clock_spread_ && origins) {
          adjusted += common_clock_credit(launch, clock_pin, h);
          if (origins->second != inf) {
            const auto second = origins->second + common_clock_credit(origins->second_pin, clock_pin, h);
            if (second < adjusted) {
              launch   = origins->second_pin;
              adjusted = second;
            }
          }
          if (origins->others < adjusted) {
            launch   = no_index;
            adjusted = origins->others;
          }
        }
        const float required = types_.lookup(table, query) + capture.arrival - (adjusted - first);
        consider(first - required - relation, launch, clock_pin);
      }
    }
  }
  return result;
}

Engine::Hold_path Engine::worst_hold_path(uint32_t pin) {
  const auto e = endpoint_of(pin);
  if (e == no_endpoint || hold_slack_.empty() || !std::isfinite(hold_slack_[pin]) || !report_.hold_computed) {
    return {};
  }
  // An unvaried clock needs no CPPR origins for timing. Materialize them once
  // only when a consumer asks for the launch driving this endpoint.
  if (hold_launch_.empty()) {
    forward_early(true);
  }
  return hold_path(e);
}

void Engine::check_hold() {
  hold_slack_.assign(netlist_.pins(), inf);
  std::vector<float> wns(std::max<size_t>(1, constraints_.clocks.size()), inf), tns(wns.size(), 0.f);
  for (size_t e = 0; e < endpoints_.size(); ++e) {
    const auto& endpoint      = endpoints_[e];
    const auto  capture_clock = endpoint.port ? endpoint.hold_clock : endpoint.clock;
    const float worst         = hold_path(e).slack_ps;
    hold_slack_[endpoint.pin] = worst;
    if (worst != inf) {
      wns[capture_clock]  = std::min(wns[capture_clock], worst);
      tns[capture_clock] += std::min(0.f, worst);
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
  const bool clock         = clock_pin_[p] != 0;
  if (netlist_.untimed(p) || (clock && !constraints_.clocks[pin_clock_[p]].propagated)) {
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
    if (limit != inf && limit > 0 && clock && clock_index_[p] != no_index) {
      for (unsigned t = 0; t < 2; ++t) {
        const auto& edge = clock_late_[(size_t(clock_index_[p]) * corners_ + k) * 2 + t];
        if (std::isfinite(edge.arrival)) {
          slew_excess = std::max<double>(slew_excess, (edge.slew - limit) / limit);
        }
      }
    } else if (limit != inf && limit > 0 && !clock) {
      for (size_t g = 0; g < groups_; ++g) {
        for (unsigned t = 0; t < 2; ++t) {
          if (const auto at = index(p, g * corners_ + k, t); arrival_[at] != -inf) {
            slew_excess = std::max<double>(slew_excess, (slew_[at] - limit) / limit);
          }
        }
      }
    }
    const auto net = netlist_.net_of(p);
    if (net != no_index && netlist_.is_driver(p)) {
      const float cap_limit = types_.max_capacitance(type, local, k);
      if (cap_limit != inf && cap_limit > 0) {
        const float load = clock ? std::ranges::max(transition_loads(net, k, false)) : net_load(net, k, false);
        cap_excess       = std::max<double>(cap_excess, (load - cap_limit) / cap_limit);
      }
    }
  }
}

// How far the driver of a net is over the fanout limit of the design, as a ratio (0: within it, or no limit).
double Engine::fanout_excess(uint32_t p) const {
  if (constraints_.max_fanout <= 0 || netlist_.untimed(p) || (clock_pin_[p] && !constraints_.clocks[pin_clock_[p]].propagated)) {
    return 0;
  }
  const auto net = netlist_.net_of(p);
  if (net == no_index || !netlist_.is_driver(p)) {
    return 0;
  }
  const double sinks = static_cast<double>(netlist_.sinks_of(net).size());
  return sinks > constraints_.max_fanout * (1 + 1e-6) ? sinks / constraints_.max_fanout - 1 : 0;
}

Electrical_pin Engine::pin_electrical(uint32_t p) {
  settle();
  double slew_excess, cap_excess;
  electrical_excess(p, slew_excess, cap_excess);
  const double fanout = fanout_excess(p);
  return {p, static_cast<uint8_t>((slew_excess > 0) + (cap_excess > 0) + (fanout > 0)),
          static_cast<float>((slew_excess > 0 ? slew_excess : 0) + (cap_excess > 0 ? cap_excess : 0) + fanout)};
}

void Engine::check_electrical() {
  report_.slew_violations        = 0;
  report_.capacitance_violations = 0;
  report_.fanout_violations      = 0;
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
    const double fanout = fanout_excess(p);
    if (fanout > 0) {
      ++report_.fanout_violations;
      report_.electrical_excess += fanout;
    }
    if (slew_excess > 0 || cap_excess > 0 || fanout > 0) {
      electrical_pins_.push_back({p, static_cast<uint8_t>((slew_excess > 0) + (cap_excess > 0) + (fanout > 0)),
                                  static_cast<float>(slew_excess + cap_excess + fanout)});
    }
  }
}

float Engine::slack(uint32_t pin) const {
  float worst = inf;
  for (size_t s = 0; s < slots_; ++s) {
    if (!(options_.setup_corners >> (s % corners_) & 1u)) {
      continue;
    }
    for (unsigned t = 0; t < 2; ++t) {
      const auto at = index(pin, s, t);
      if (arrival_[at] != -inf && required_[at] != inf) {
        worst = std::min(worst, required_[at] - arrival_[at]);
      }
    }
  }
  return worst;
}

float Engine::slack(uint32_t pin, uint32_t& group) const {
  float worst = inf;
  group       = 0;
  for (size_t s = 0; s < slots_; ++s) {
    if (!(options_.setup_corners >> (s % corners_) & 1u)) {
      continue;
    }
    for (unsigned t = 0; t < 2; ++t) {
      const auto at = index(pin, s, t);
      if (arrival_[at] != -inf && required_[at] != inf && required_[at] - arrival_[at] < worst) {
        worst = required_[at] - arrival_[at];
        group = static_cast<uint32_t>(s / corners_);
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
      // The delay of the group that reaches the arc (the largest, when several do).
      float delay = 0;
      for (size_t g = 0; g < groups_; ++g) {
        delay = std::max(delay, arc_delay_[cell_base_[cell] + arc_offset_[types_.type(type).first_arc + arc] * slots_
                                           + (g * corners_ + corner) * combos.size() + m]);
      }
      return delay;
    }
  }
  return std::numeric_limits<float>::quiet_NaN();
}

std::vector<Path_point> Engine::worst_path(uint32_t endpoint) const {
  std::vector<Path_point> path;
  unsigned                transition = 0;
  size_t                  group      = 0;
  const size_t            C          = corners_;
  if (endpoint == no_index) {
    float worst = inf;
    for (size_t e = 0; e < endpoints_.size(); ++e) {
      if (endpoint_path_slack_[e] < worst) {
        worst      = endpoint_path_slack_[e];
        endpoint   = endpoints_[e].pin;
        transition = endpoint_path_transition_[e];
        group      = endpoint_path_group_[e];
      }
    }
    if (endpoint == no_index) {
      return path;
    }
  } else if (const auto e = endpoint_of(endpoint); e != no_endpoint && endpoint_path_slack_[e] != inf) {
    transition = endpoint_path_transition_[e];
    group      = endpoint_path_group_[e];
  } else {
    float worst = inf;
    for (size_t g = 0; g < groups_; ++g) {
      for (unsigned t = 0; t < 2; ++t) {
        const auto at = index(endpoint, g * C + options_.path_corner, t);
        if (arrival_[at] != -inf && (worst == inf || -arrival_[at] < worst)) {
          worst      = -arrival_[at];
          transition = t;
          group      = g;
        }
      }
    }
  }
  if (const auto e = endpoint_of(endpoint); clock_spread_ && e != no_endpoint && endpoint_path_slack_[e] != inf) {
    (void)endpoint_requirement(e, group * C + options_.path_corner, transition, &path);
    if (!path.empty()) {
      return path;
    }
  }
  uint32_t pin = endpoint;
  while (pin != no_index && path.size() < netlist_.pins() + 1) {
    path.push_back({pin, transition, arrival_[index(pin, group * C + options_.path_corner, transition)]});
    const auto& before = predecessor_[path_index(pin, group, transition)];
    pin                = before.pin;
    transition         = before.transition;
  }
  std::reverse(path.begin(), path.end());
  return path;
}

bool Engine::endpoint_path_covers_worst(uint32_t pin) const {
  auto id = endpoint_of(pin);
  return id != no_endpoint && endpoint_path_slack_[id] == endpoint_slack_[id];
}

std::string Engine::endpoint_clock(uint32_t pin) const {
  auto id = endpoint_of(pin);
  if (id == no_endpoint || endpoints_[id].unclocked || endpoints_[id].clock >= constraints_.clocks.size()) {
    return {};
  }
  return constraints_.clocks[endpoints_[id].clock].name;
}

size_t Engine::bytes() const {
  const auto of
      = [](const auto& vector) { return vector.capacity() * sizeof(typename std::decay_t<decltype(vector)>::value_type); };
  return of(arrival_) + of(slew_) + of(required_) + of(early_arrival_) + of(early_slew_) + of(hold_slack_) + of(criticality_)
         + of(hold_launch_) + of(clock_index_) + of(clock_sources_) + of(clock_late_) + of(clock_early_) + of(depth_)
         + of(predecessor_) + of(clock_pin_) + of(pin_clock_) + of(load_) + of(early_load_) + of(arc_delay_) + of(cell_base_)
         + of(port_input_) + of(electrical_pins_) + of(arc_offset_) + of(port_load_) + of(endpoints_) + of(cut_) + of(excluded_);
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
  key_.assign(netlist_.pins(), unset_key);
  for (size_t i = 0; i < order.size(); ++i) {
    key_[order[i]] = static_cast<uint64_t>(i) << 24;
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
    fwd_heap_.emplace_back(key_[pin], pin);
    std::push_heap(fwd_heap_.begin(), fwd_heap_.end(), std::greater<>());
  }
}

void Engine::push_backward(uint32_t pin) {
  if (!(queued_[pin] & 2u)) {
    queued_[pin] |= 2u;
    bwd_heap_.emplace_back(key_[pin], pin);
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
  for (size_t s = 0; s < slots_; ++s) {
    if (!(options_.setup_corners >> (s % corners_) & 1u)) {
      continue;
    }
    for (unsigned t = 0; t < 2; ++t) {
      const auto at = index(endpoints_[e].pin, s, t);
      if (arrival_[at] != -inf && required_[at] != inf) {
        worst = std::min(worst, required_[at] - arrival_[at]);
      }
    }
  }
  store(endpoint_slack_, e, worst);
}

void Engine::settle() {
  if (!dirty_clocks_.empty() || !fwd_heap_.empty() || !bwd_heap_.empty()) {
    report_.hold_computed = false;
  }
  if (!dirty_clocks_.empty()) {
    propagate_clocks(dirty_clocks_);
    dirty_clocks_.clear();
  }
  if (!journal_on_ || (fwd_heap_.empty() && bwd_heap_.empty())) {
    return;
  }
  const size_t C = slots_;
  before_.resize(C * 6);  // arrival, slew, depth of the 2 transitions of every slot
  auto& before = before_;
  // Forward: lowest position first; a pin that changes wakes what it feeds.
  while (!fwd_heap_.empty()) {
    std::pop_heap(fwd_heap_.begin(), fwd_heap_.end(), std::greater<>());
    const auto [position, p] = fwd_heap_.back();
    fwd_heap_.pop_back();
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
    // An edit can move a competing launch without changing the raw latest
    // arrival. Its common-path credit can still change a downstream check.
    if (!changed && !clock_spread_) {
      continue;
    }
    push_backward(p);
    if (const auto e = endpoint_of(p); e != no_endpoint) {
      for (size_t k = 0; k < C; ++k) {
        for (unsigned t = 0; t < 2; ++t) {
          store(endpoint_required_, (e * C + k) * 2 + t, endpoint_requirement(e, k, t));
        }
      }
      touch_endpoint(p);
    }
    const auto wake = [&](uint32_t pin) {
      if (key_[pin] > position) {
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
    const auto [position, p] = bwd_heap_.back();
    bwd_heap_.pop_back();
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
      if (key_[pin] < position) {
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

float Engine::settled_hold_slack(uint32_t pin) {
  if (endpoint_of(pin) == no_endpoint) {
    return inf;
  }
  settle();
  if (!report_.hold_computed) {
    forward_early();
    check_hold();
    report_.hold_computed = true;
  }
  return hold_slack(pin);
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
  for (const auto& entry : fwd_heap_) {
    queued_[entry.second] = 0;
  }
  for (const auto& entry : bwd_heap_) {
    queued_[entry.second] = 0;
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
  bool clocks_restored = false;
  if (journal_.size() > to) {
    report_.hold_computed = false;
  }
  dirty_clocks_.clear();
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
      case Undo_kind::early_load       : early_load_[u.index] = bits_to_float(u.old_bits); break;
      case Undo_kind::clock_edge:
        (u.aux ? clock_early_ : clock_late_)[u.index] = clock_undo_.back();
        clock_undo_.pop_back();
        clocks_restored = true;
        break;
      case Undo_kind::cell_type        : netlist_.retype(static_cast<uint32_t>(u.index), u.old_bits); break;
      case Undo_kind::netlist_edit     : netlist_.edit_rollback(u.index); break;
      case Undo_kind::grow_cells       : shrink_cells(u.index, u.old_bits); break;
      case Undo_kind::grow_nets        : shrink_nets(u.index); break;
    }
  }
  if (clocks_restored) {
    refresh_clock_metadata();
  }
  if (detailed_edits_) {
    evaluate_edits();
  }
}

void Engine::end_edits() {
  rollback(0);
  journal_on_ = false;
  detailed_edits_ = false;
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
    dirty_clock(pin);
    push_backward(pin);  // its arcs and checks are other tables now
    if (!types_.arcs_into(type, local).empty()) {
      push_forward(pin);
    }
    if (const auto e = endpoint_of(pin); e != no_endpoint) {
      for (size_t k = 0; k < slots_; ++k) {
        for (unsigned t = 0; t < 2; ++t) {
          store(endpoint_required_, (e * slots_ + k) * 2 + t, endpoint_requirement(e, k, t));
        }
      }
      touch_endpoint(pin);
    }
    // An input pin has another capacitance: the net that drives it has another load.
    if (const auto net = netlist_.net_of(pin); net != no_index && !netlist_.is_driver(pin)) {
      if (compute_net_load(net) || !clock_sources_.empty()) {
        dirty_clock(netlist_.driver_of(net));
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
  dirty_clock(netlist_.driver_of(net));
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

uint32_t Engine::add_cell(uint32_t type) {
  begin_edits();
  settle();
  const auto& t = types_.type(type);
  if (t.sequential || t.latch || t.memory || t.clock_gate || type < 2) {
    throw std::invalid_argument("Engine::add_cell: only combinational cells can be added");
  }
  const size_t cells = netlist_.cells(), pins = netlist_.pins();
  journal_.push_back({Undo_kind::netlist_edit, 0, 0, netlist_.edit_mark()});
  const auto cell = netlist_.append_cell(type);
  const size_t n = t.pin_count, C = corners_;
  journal_.push_back({Undo_kind::grow_cells, 0, static_cast<uint32_t>(pins), cells});
  const size_t S = slots_, G = groups_;
  arrival_.resize((pins + n) * S * 2, -inf);
  slew_.resize((pins + n) * S * 2, 0.f);
  required_.resize((pins + n) * S * 2, inf);
  depth_.resize((pins + n) * S * 2, 0);
  predecessor_.resize((pins + n) * G * 2);
  criticality_.resize(pins + n, 0.f);
  clock_pin_.resize(pins + n, 0);
  pin_clock_.resize(pins + n, 0);
  cut_.resize(pins + n, 0);
  excluded_.resize(pins + n, 0);
  queued_.resize(pins + n, 0);
  key_.resize(pins + n, unset_key);
  if (!hold_slack_.empty()) {
    hold_slack_.resize(pins + n, inf);
  }
  if (!early_arrival_.empty()) {
    early_arrival_.resize((pins + n) * G * 2, inf);
    early_slew_.resize((pins + n) * G * 2, 0.f);
  }
  wire_.sink_delay_ps.resize((pins + n) * C, 0.f);
  wire_.sink_slew_ps.resize((pins + n) * C, 0.f);
  port_load_.push_back(0.f);
  port_delay_.push_back(0.f);
  port_input_.push_back(no_index);
  arc_delay_.resize(cell_base_.back() + type_block_[type] * S, 0.f);
  cell_base_.push_back(static_cast<uint32_t>(arc_delay_.size()));
  return cell;
}

void Engine::shrink_cells(size_t cells, uint32_t pins) {
  const size_t C = corners_, S = slots_, G = groups_;
  arrival_.resize(static_cast<size_t>(pins) * S * 2);
  slew_.resize(static_cast<size_t>(pins) * S * 2);
  required_.resize(static_cast<size_t>(pins) * S * 2);
  depth_.resize(static_cast<size_t>(pins) * S * 2);
  predecessor_.resize(static_cast<size_t>(pins) * G * 2);
  criticality_.resize(pins);
  clock_pin_.resize(pins);
  pin_clock_.resize(pins);
  cut_.resize(pins);
  excluded_.resize(pins);
  queued_.resize(pins);
  key_.resize(pins);
  if (!hold_slack_.empty()) {
    hold_slack_.resize(pins);
  }
  if (!early_arrival_.empty()) {
    early_arrival_.resize(static_cast<size_t>(pins) * G * 2);
    early_slew_.resize(static_cast<size_t>(pins) * G * 2);
  }
  wire_.sink_delay_ps.resize(static_cast<size_t>(pins) * C);
  wire_.sink_slew_ps.resize(static_cast<size_t>(pins) * C);
  port_load_.resize(cells);
  port_delay_.resize(cells);
  port_input_.resize(cells);
  cell_base_.resize(cells + 1);
  arc_delay_.resize(cell_base_.back());
}

void Engine::shrink_nets(size_t nets) {
  load_.resize(nets * corners_);
  early_load_.resize(nets);
  wire_.net_cap_ff.resize(nets * corners_);
}

uint32_t Engine::set_net(uint32_t driver, std::span<const uint32_t> sinks, std::span<const float> cap_ff, std::span<const float> delay_ps,
                         std::span<const float> slew_ps) {
  begin_edits();
  settle();
  const size_t C = corners_;
  if (cap_ff.size() != C || delay_ps.size() != sinks.size() * C || slew_ps.size() != sinks.size() * C) {
    throw std::invalid_argument("Engine::set_net: sizes do not match the corners and sinks");
  }
  if (const auto net = netlist_.net_of(driver); net != no_index && netlist_.is_driver(driver)) {
    auto existing = netlist_.sinks_of(net);
    if (existing.size() == sinks.size()) {
      bool same = std::ranges::equal(existing, sinks);
      if (!same) {
        std::vector<uint32_t> before(existing.begin(), existing.end()), after(sinks.begin(), sinks.end());
        std::ranges::sort(before);
        std::ranges::sort(after);
        same = before == after;
      }
      if (same) {
        set_net_wire(net, cap_ff, sinks, delay_ps, slew_ps);
        return net;
      }
    }
  }
  if (key_[driver] == unset_key) {
    throw std::domain_error("Engine::set_net: connect the inputs of a new cell before its output");
  }
  // Keys first, so that a refusal changes nothing. The pins of a new cell go between the driver and what
  // followed it.
  const uint64_t lo      = key_[driver];
  uint64_t       hi      = lo + (uint64_t{1} << 24);
  if (const auto old_net = netlist_.net_of(driver); old_net != no_index) {
    for (const auto sink : netlist_.sinks_of(old_net)) {
      if (key_[sink] != unset_key && key_[sink] > lo) {
        hi = std::min(hi, key_[sink]);
      }
    }
  }
  for (const auto sink : sinks) {
    if (key_[sink] == unset_key) {
      const auto  cell = netlist_.cell_of(sink);
      const auto  first = netlist_.first_pin(cell);
      const auto  count = types_.type(netlist_.type_of(cell)).pin_count;
      std::vector<uint32_t> unset;
      for (uint32_t local = 0; local < count; ++local) {
        if (key_[first + local] == unset_key && netlist_.pin_type(first + local).direction == Direction::input) {
          unset.push_back(first + local);
        }
      }
      for (uint32_t local = 0; local < count; ++local) {
        if (key_[first + local] == unset_key && netlist_.pin_type(first + local).direction != Direction::input) {
          unset.push_back(first + local);
        }
      }
      const uint64_t step = (hi - lo) / (unset.size() + 1);
      if (step < 1) {
        throw std::domain_error("Engine::set_net: no room in the order for a new cell");
      }
      for (size_t j = 0; j < unset.size(); ++j) {
        key_[unset[j]] = lo + (j + 1) * step;
      }
    }
    if (key_[sink] <= lo) {
      throw std::domain_error("Engine::set_net: a sink would come before its driver in the order");
    }
  }
  const size_t nets_before = netlist_.nets();
  std::vector<uint32_t> removed;  // the pins that are on the net now and are not listed
  if (const auto old_net = netlist_.net_of(driver); old_net != no_index) {
    for (const auto sink : netlist_.sinks_of(old_net)) {
      if (std::find(sinks.begin(), sinks.end(), sink) == sinks.end()) {
        removed.push_back(sink);
      }
    }
  }
  journal_.push_back({Undo_kind::netlist_edit, 0, 0, netlist_.edit_mark()});
  std::vector<uint32_t> touched;
  const auto            net = netlist_.set_net_sinks(driver, sinks, &touched);
  if (netlist_.nets() != nets_before) {
    journal_.push_back({Undo_kind::grow_nets, 0, 0, nets_before});
    load_.resize(netlist_.nets() * C, 0.f);
    early_load_.resize(netlist_.nets(), 0.f);
    wire_.net_cap_ff.resize(netlist_.nets() * C, 0.f);
  }
  for (size_t k = 0; k < C; ++k) {
    store(wire_.net_cap_ff, net * C + k, cap_ff[k]);
  }
  for (size_t i = 0; i < sinks.size(); ++i) {
    for (size_t k = 0; k < C; ++k) {
      store(wire_.sink_delay_ps, static_cast<size_t>(sinks[i]) * C + k, delay_ps[i * C + k]);
      store(wire_.sink_slew_ps, static_cast<size_t>(sinks[i]) * C + k, slew_ps[i * C + k]);
    }
  }
  compute_net_load(net);
  push_forward(driver);
  push_backward(driver);
  for (const auto sink : sinks) {
    push_forward(sink);
  }
  for (const auto pin : removed) {
    push_forward(pin);
  }
  // Pins that left a net: the nets they left, and they themselves (nothing arrives any more).
  for (const auto other : touched) {
    compute_net_load(other);
    push_forward(netlist_.driver_of(other));
    push_backward(netlist_.driver_of(other));
  }
  return net;
}

}  // namespace livehd::timing::fast
