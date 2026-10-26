// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "candidate.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace livehd::placement::buffering {
namespace {
namespace model         = liberty::timing;
constexpr size_t absent = std::numeric_limits<size_t>::max();
void             require(bool condition, const char* reason) {
  if (!condition) {
    throw std::invalid_argument(reason);
  }
}
bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool valid(Rect r) { return finite({r.xl, r.yl}) && finite({r.xh, r.yh}) && r.xl < r.xh && r.yl < r.yh; }
bool contains(Rect r, Point p) { return p.x >= r.xl && p.y >= r.yl && p.x <= r.xh && p.y <= r.yh; }
bool overlaps(Rect a, Rect b) { return a.xl < b.xh && b.xl < a.xh && a.yl < b.yh && b.yl < a.yh; }
struct Stage {
  const model::Cell* cell   = nullptr;
  const model::Pin*  input  = nullptr;
  const model::Pin*  output = nullptr;
  const model::Arc*  arc    = nullptr;
};
std::optional<Stage> stage(const model::Library& lib, const Cell_ports& ports, bool buffer) {
  const auto found = lib.cells.find(ports.cell);
  if (found == lib.cells.end()) {
    return {};
  }
  const auto& c  = found->second;
  const auto  in = c.pins.find(ports.input), out = c.pins.find(ports.output);
  if (c.sequential || c.latch || c.memory || !c.unsupported.empty() || in == c.pins.end() || out == c.pins.end()) {
    return {};
  }
  if (in->second.direction != "input" || out->second.direction != "output" || in->second.clock || out->second.three_state) {
    return {};
  }
  if (buffer) {
    std::string function;
    for (unsigned char ch : out->second.function) {
      if (!std::isspace(ch) && ch != '(' && ch != ')') {
        function += char(ch);
      }
    }
    if (function != ports.input) {
      return {};
    }
    for (const auto& [name, pin] : c.pins) {
      if (pin.direction == "input" && name != ports.input) {
        return {};
      }
      if (pin.direction == "output" && name != ports.output) {
        return {};
      }
    }
  }
  const model::Arc* arc = nullptr;
  for (const auto& a : out->second.arcs) {
    if (a.related_pin != ports.input || (!a.type.empty() && a.type != "combinational")) {
      continue;
    }
    if (arc || !a.when.empty() || !a.unsupported.empty()
        || (a.sense != model::Sense::positive && a.sense != model::Sense::negative)) {
      return {};
    }
    arc = &a;
  }
  if (!arc || (buffer && arc->sense != model::Sense::positive)) {
    return {};
  }
  for (unsigned t = 0; t < 2; ++t) {
    if (!arc->delay[t] || !arc->slew[t] || !in->second.capacitance_ff[t]) {
      return {};
    }
  }
  return Stage{&c, &in->second, &out->second, arc};
}
std::string pin_name(const Context& c, const Candidate& candidate, size_t index, bool output) {
  const auto& ports = index ? candidate.buffers[index - 1].model : c.driver.model;
  const auto& name  = index ? candidate.buffers[index - 1].name : c.driver.name;
  return name + ":" + (output ? ports.output : ports.input);
}
}  // namespace

Evaluation evaluate(const model::Library& lib, const Context& c, const Candidate& candidate) {
  require(valid(c.core) && finite(c.driver.output), "invalid buffering context geometry");
  require(!c.driver.name.empty() && std::isfinite(c.normalization_period_ps) && c.normalization_period_ps > 0,
          "invalid driver identity or multiplier normalization");
  for (unsigned t = 0; t < 2; ++t) {
    require(
        std::isfinite(c.driver.input_arrival_ps[t]) && std::isfinite(c.driver.input_slew_ps[t]) && c.driver.input_slew_ps[t] >= 0,
        "invalid source timing context");
  }
  for (const auto& r : c.placement_exclusions) {
    require(valid(r), "invalid placement exclusion");
  }
  const size_t count = candidate.buffers.size() + 1;
  require(candidate.trees.size() == count && !c.sinks.empty(), "buffer candidate needs one tree per driver and physical sinks");
  Evaluation result;
  result.characterized_boundary_policy = c.driver_options.characterized_boundary;
  result.bounded_boundary_policy       = c.driver_options.bounded_boundary;
  const auto unsupported               = [&](std::string reason) {
    result.status = Status::unsupported;
    result.reason = std::move(reason);
    return result;
  };
  if (c.clock_net) {
    return unsupported("clock distribution is outside signal buffering");
  }
  std::vector<Stage> stages;
  for (size_t i = 0; i < count; ++i) {
    const auto s = stage(lib, i ? candidate.buffers[i - 1].model : c.driver.model, i != 0);
    if (!s) {
      return unsupported("missing, ambiguous or unsupported stage model: " + pin_name(c, candidate, i, true));
    }
    stages.push_back(*s);
  }
  std::set<std::string> identities;
  for (size_t i = 0; i < count; ++i) {
    require(identities.insert(pin_name(c, candidate, i, false)).second && identities.insert(pin_name(c, candidate, i, true)).second,
            "duplicate buffer/driver pin identity");
  }
  std::vector<const model::Cell*> sink_cells;
  std::vector<const model::Pin*>  sink_pins;
  double                          mass = 0;
  for (const auto& sink : c.sinks) {
    require(!sink.name.empty() && finite(sink.position) && identities.insert(sink.name).second,
            "invalid or duplicate sink identity");
    const auto cell = lib.cells.find(sink.cell);
    if (cell == lib.cells.end()) {
      return unsupported("missing sink cell: " + sink.cell);
    }
    const auto pin = cell->second.pins.find(sink.pin);
    if (pin == cell->second.pins.end() || pin->second.direction != "input" || pin->second.clock) {
      return unsupported("unsupported sink pin: " + sink.name);
    }
    for (unsigned t = 0; t < 2; ++t) {
      if (!pin->second.capacitance_ff[t]) {
        return unsupported("missing sink capacitance: " + sink.name);
      }
      require(std::isfinite(sink.multiplier[t]) && sink.multiplier[t] >= 0
                  && (!sink.required_ps[t] || std::isfinite(*sink.required_ps[t]))
                  && (sink.required_ps[t] || sink.multiplier[t] == 0),
              "invalid sink multiplier or physical requirement");
      mass += sink.multiplier[t];
    }
    sink_cells.push_back(&cell->second);
    sink_pins.push_back(&pin->second);
  }
  require(std::isfinite(mass) && mass <= 1 + 1e-9, "endpoint multiplier mass exceeds one");
  const auto resource_violation = [&](std::string object, std::string kind, double actual, double limit) {
    result.resources_feasible = false;
    result.violations.push_back({std::move(object), std::move(kind), actual, limit});
  };
  for (size_t i = 0; i < candidate.buffers.size(); ++i) {
    const auto& b = candidate.buffers[i];
    require(!b.name.empty() && valid(b.body) && finite(b.input) && finite(b.output) && contains(b.body, b.input)
                && contains(b.body, b.output),
            "invalid buffer physical footprint or pins");
    result.area_um2 += (b.body.xh - b.body.xl) * (b.body.yh - b.body.yl);
    if (!b.placement_allowed || !contains(c.core, {b.body.xl, b.body.yl}) || !contains(c.core, {b.body.xh, b.body.yh})) {
      resource_violation(b.name, "placement", 1, 0);
    }
    for (const auto& r : c.placement_exclusions) {
      if (overlaps(b.body, r)) {
        resource_violation(b.name, "exclusion", 1, 0);
      }
    }
    for (size_t j = 0; j < i; ++j) {
      if (overlaps(b.body, candidate.buffers[j].body)) {
        resource_violation(b.name, "buffer_overlap", 1, 0);
      }
    }
    for (const auto& demand : b.demand) {
      require(!demand.resource.empty() && demand.units > 0, "invalid buffer resource demand");
      auto& value = result.demand[demand.resource];
      require(demand.units <= std::numeric_limits<uint64_t>::max() - value, "buffer resource demand overflows");
      value += demand.units;
    }
  }
  require(std::isfinite(result.area_um2), "nonfinite buffer area");
  for (const auto& [resource, demand] : result.demand) {
    const auto cap = c.capacity.find(resource);
    require(cap != c.capacity.end(), "buffer demand lacks an explicit resource capacity");
    if (demand > cap->second) {
      resource_violation(resource, "capacity", double(demand), double(cap->second));
    }
  }
  std::vector<size_t> trees(count, absent), parent(count), sink_uses(c.sinks.size());
  size_t              nodes = 0;
  for (size_t i = 0; i < candidate.trees.size(); ++i) {
    const auto& tree = candidate.trees[i];
    require(tree.driver_stage < count && trees[tree.driver_stage] == absent, "duplicate or invalid tree driver stage");
    trees[tree.driver_stage] = i;
    require(tree.rc.driver == pin_name(c, candidate, tree.driver_stage, true), "wire driver identity differs from stage binding");
    physical::validate_rc_tree(tree.rc);
    require(tree.driver_node < tree.rc.nodes.size() && tree.positions.size() == tree.rc.nodes.size(),
            "invalid RC positions or root");
    for (auto point : tree.positions) {
      require(finite(point), "nonfinite wire position");
    }
    require(tree.rc.nodes[tree.driver_node].pin == tree.rc.driver, "wire root pin identity differs from declared driver");
    double minimum_length = 0;
    for (const auto& edge : tree.rc.edges) {
      const auto a = tree.positions[edge.from], b = tree.positions[edge.to];
      minimum_length += std::abs(a.x - b.x) + std::abs(a.y - b.y);
    }
    require(std::isfinite(minimum_length) && tree.rc.wire_length_um + 1e-9 * std::max(1., minimum_length) >= minimum_length,
            "candidate wire length is below its rectilinear geometry bound");
    require(
        tree.positions[tree.driver_node] == (tree.driver_stage ? candidate.buffers[tree.driver_stage - 1].output : c.driver.output),
        "driver physical position differs from RC root");
    require(tree.rc.nodes.size() <= std::numeric_limits<size_t>::max() - nodes, "RC node count overflows");
    nodes += tree.rc.nodes.size();
    require(std::isfinite(tree.rc.wire_length_um) && tree.rc.wire_length_um >= 0, "invalid candidate wire length");
    result.wire_length_um += tree.rc.wire_length_um;
    if (!tree.routing_allowed) {
      resource_violation(pin_name(c, candidate, tree.driver_stage, true), "routing_obstruction", 1, 0);
    }
    require(!tree.terminals.empty(), "buffer candidate has an unloaded output tree");
    for (const auto& terminal : tree.terminals) {
      require(terminal.node < tree.rc.nodes.size(), "invalid receiver RC node");
      if (terminal.buffer) {
        require(terminal.index < candidate.buffers.size(), "invalid receiver buffer");
        ++parent[terminal.index + 1];
        require(tree.positions[terminal.node] == candidate.buffers[terminal.index].input,
                "buffer input position differs from wire terminal");
      } else {
        require(terminal.index < c.sinks.size(), "invalid receiver sink");
        ++sink_uses[terminal.index];
        require(tree.positions[terminal.node] == c.sinks[terminal.index].position, "sink position differs from wire terminal");
      }
    }
  }
  require(std::all_of(parent.begin() + 1, parent.end(), [](size_t n) { return n == 1; })
              && std::all_of(sink_uses.begin(), sink_uses.end(), [](size_t n) { return n == 1; }),
          "buffer/sink connectivity is incomplete or duplicated");
  std::vector<size_t> order{0};
  for (size_t i = 0; i < order.size(); ++i) {
    for (const auto& terminal : candidate.trees[trees[order[i]]].terminals) {
      if (terminal.buffer) {
        order.push_back(terminal.index + 1);
      }
    }
    require(order.size() <= count, "cycle in buffer graph");
  }
  require(order.size() == count, "disconnected cycle in buffer graph");
  if (nodes > c.max_rc_nodes || count > c.max_waveform_fits / 2) {
    result.status = Status::budget_exhausted;
    result.reason = "candidate waveform/node work bound";
    return result;
  }
  const auto electrical = [&](const std::string& pin, std::string kind, double actual, std::optional<double> limit) {
    require(std::isfinite(actual) && actual >= 0, "nonfinite/negative candidate electrical metric");
    if (limit && actual > *limit) {
      result.electrically_feasible = false;
      result.violations.push_back({pin, std::move(kind), actual, *limit});
    }
  };
  result.pins[pin_name(c, candidate, 0, false)] = {c.driver.input_arrival_ps, c.driver.input_slew_ps, {}};
  std::vector<std::array<double, 2>>              gate_delay(count);
  std::vector<std::vector<std::array<double, 2>>> wire_delay(candidate.trees.size());
  for (size_t index : order) {
    const size_t ti      = trees[index];
    const auto&  tree    = candidate.trees[ti];
    const auto&  s       = stages[index];
    const auto   in_name = pin_name(c, candidate, index, false), out_name = pin_name(c, candidate, index, true);
    const auto   input = result.pins.at(in_name);
    std::vector<std::array<double, 2>> caps;
    for (const auto& node : tree.rc.nodes) {
      caps.push_back({node.capacitance_ff, node.capacitance_ff});
    }
    for (const auto& terminal : tree.terminals) {
      const auto* pin = terminal.buffer ? stages[terminal.index + 1].input : sink_pins[terminal.index];
      for (unsigned t = 0; t < 2; ++t) {
        caps[terminal.node][t] += *pin->capacitance_ff[t];
      }
    }
    const auto rc = late::analyze_rc_tree(tree.rc, unsigned(tree.driver_node), caps);
    wire_delay[ti].resize(tree.terminals.size());
    for (unsigned t = 0; t < 2; ++t) {
      const unsigned input_transition = s.arc->sense == model::Sense::negative ? 1 - t : t;
      electrical(in_name, "input_slew", input.slew_ps[input_transition], s.input->max_transition_ps);
      electrical(out_name, "capacitance", rc.total_cap_ff[t], s.output->max_capacitance_ff);
      const auto fit = late::fit_driver(*s.arc->delay[t],
                                        *s.arc->slew[t],
                                        input.slew_ps[input_transition],
                                        rc.total_cap_ff[t],
                                        s.cell->thresholds,
                                        t,
                                        &rc.pi[t],
                                        c.driver_options);
      ++result.waveform_fits;
      ++result.driver_modes[fit.mode];
      if (fit.failure != late::Driver_failure::none) {
        ++result.driver_failures[fit.failure];
      }
      if (fit.mode == late::Driver_mode::failed) {
        return unsupported("native driver waveform fit failed: " + out_name + " transition=" + std::to_string(t));
      }
      gate_delay[index][t] = fit.delay_ps;
      auto& out            = result.pins[out_name];
      out.arrival_ps[t]    = input.arrival_ps[input_transition] + fit.delay_ps;
      out.slew_ps[t]       = fit.slew_ps;
      electrical(out_name, "output_slew", fit.slew_ps, s.output->max_transition_ps);
      for (size_t k = 0; k < tree.terminals.size(); ++k) {
        const auto& terminal = tree.terminals[k];
        const auto* cell     = terminal.buffer ? stages[terminal.index + 1].cell : sink_cells[terminal.index];
        const auto* pin      = terminal.buffer ? stages[terminal.index + 1].input : sink_pins[terminal.index];
        const auto  name     = terminal.buffer ? pin_name(c, candidate, terminal.index + 1, false) : c.sinks[terminal.index].name;
        const auto  wire     = fit.wire(rc.elmore_ps[terminal.node][t], s.cell->thresholds, cell->thresholds, t);
        wire_delay[ti][k][t] = wire.delay_ps;
        auto& state          = result.pins[name];
        state.arrival_ps[t]  = out.arrival_ps[t] + wire.delay_ps;
        state.slew_ps[t]     = wire.slew_ps;
        require(std::isfinite(state.arrival_ps[t]), "nonfinite candidate arrival");
        electrical(name, "receiver_slew", wire.slew_ps, pin->max_transition_ps);
      }
    }
  }
  for (const auto& sink : c.sinks) {
    auto& pin       = result.pins.at(sink.name);
    pin.required_ps = sink.required_ps;
    for (unsigned t = 0; t < 2; ++t) {
      if (sink.required_ps[t]) {
        result.meets_required = result.meets_required.value_or(true) && pin.arrival_ps[t] <= *sink.required_ps[t];
      }
      const unsigned input_transition = stages[0].arc->sense == model::Sense::negative ? 1 - t : t;
      result.multiplier_cost
          += sink.multiplier[t] * (pin.arrival_ps[t] - c.driver.input_arrival_ps[input_transition]) / c.normalization_period_ps;
    }
  }
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    const size_t index = *it, ti = trees[index];
    const auto&  tree = candidate.trees[ti];
    auto&        out  = result.pins.at(pin_name(c, candidate, index, true));
    auto&        in   = result.pins.at(pin_name(c, candidate, index, false));
    for (size_t k = 0; k < tree.terminals.size(); ++k) {
      const auto& terminal = tree.terminals[k];
      const auto  name     = terminal.buffer ? pin_name(c, candidate, terminal.index + 1, false) : c.sinks[terminal.index].name;
      for (unsigned t = 0; t < 2; ++t) {
        if (const auto required = result.pins.at(name).required_ps[t]) {
          const double r = *required - wire_delay[ti][k][t];
          require(std::isfinite(r), "candidate required time overflows");
          if (!out.required_ps[t] || r < *out.required_ps[t]) {
            out.required_ps[t] = r;
          }
        }
      }
    }
    for (unsigned t = 0; t < 2; ++t) {
      if (out.required_ps[t]) {
        const unsigned input_transition = stages[index].arc->sense == model::Sense::negative ? 1 - t : t;
        const double   required         = *out.required_ps[t] - gate_delay[index][t];
        require(std::isfinite(required), "candidate input required time overflows");
        in.required_ps[input_transition] = required;
      }
    }
  }
  result.root_required_ps = result.pins.at(pin_name(c, candidate, 0, false)).required_ps;
  require(std::isfinite(result.multiplier_cost) && std::isfinite(result.wire_length_um), "nonfinite candidate objective");
  return result;
}
}  // namespace livehd::placement::buffering
