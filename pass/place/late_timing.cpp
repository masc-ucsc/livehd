// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "late_timing.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "gate_waveform.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "rc_tree.hpp"
#include "wire_waveform.hpp"

namespace livehd::placement::late {
namespace model = liberty::timing;
namespace {
constexpr size_t absent = std::numeric_limits<size_t>::max();
std::string      declared_pin_name(const hhds::Pin_class& pin) {
  const auto node = pin.get_master_node();
  const bool port = graph_util::is_graph_input_pin(pin) || graph_util::is_graph_output_pin(pin);
  const auto io   = port ? pin.get_graph()->get_io() : node.get_subnode_io();
  if (io) {
    const auto& declarations
        = (port ? graph_util::is_graph_input_pin(pin) : pin.is_sink()) ? io->get_input_pin_decls() : io->get_output_pin_decls();
    for (const auto& declaration : declarations) {
      if (declaration.port_id == pin.get_port_id()) {
        return declaration.name;
      }
    }
  }
  throw std::invalid_argument("mapped timing pin has no IO declaration");
}
std::string label(const hhds::Pin_class& pin) {
  const auto name = declared_pin_name(pin);
  if (graph_util::is_graph_input_pin(pin) || graph_util::is_graph_output_pin(pin)) {
    return name;
  }
  return std::string(pin.get_master_node().get_name()) + ":" + name;
}
void require_nonnegative(double value, const char* what) {
  if (!std::isfinite(value) || value < 0) {
    throw std::invalid_argument(what);
  }
}
void maximum(std::optional<double>& target, double value) {
  if (!target || value > *target) {
    target = value;
  }
}
void minimum(std::optional<double>& target, double value) {
  if (!target || value < *target) {
    target = value;
  }
}
bool combinational_arc(const model::Arc& arc) {
  return arc.type == "combinational" || arc.type == "combinational_rise" || arc.type == "combinational_fall";
}
bool launch_arc(const model::Arc& arc) { return arc.type == "rising_edge" || arc.type == "falling_edge"; }
// Whether a combinational arc's declared unateness and rise/fall-only type
// permit input transition in to produce output transition out.
bool arc_transition(const model::Arc& arc, unsigned in, unsigned out) {
  return !((arc.sense == model::Sense::positive && in != out) || (arc.sense == model::Sense::negative && in == out)
           || (arc.type == "combinational_rise" && out != 0) || (arc.type == "combinational_fall" && out != 1));
}
// arc_transition() restricted by the output function's constant-sensitized
// (positive, negative) unateness.
bool sensitized_transition(const model::Arc& arc, std::pair<bool, bool> sense, unsigned in, unsigned out) {
  return (in == out ? sense.first : sense.second) && arc_transition(arc, in, out);
}
bool depends_on_load(const model::Table& table) {
  const size_t rows = std::max(size_t{1}, table.x.size()), columns = std::max(size_t{1}, table.y.size());
  for (size_t x = 0; x < rows; ++x) {
    for (size_t y = 0; y < columns; ++y) {
      if ((table.x_variable == model::Variable::output_load && table.values[x * columns + y] != table.values[y])
          || (table.y_variable == model::Variable::output_load && table.values[x * columns + y] != table.values[x * columns])) {
        return true;
      }
    }
  }
  return false;
}
}  // namespace

struct Proxy::Impl {
  using Delays = std::array<std::array<std::optional<double>, 2>, 2>;
  struct Launch {
    size_t   pin, clock;
    unsigned transition;
    double   arrival;
  };
  struct Bound_pin {
    std::string                    name;
    hhds::Pin_class                graph_pin;
    const model::Pin*              model      = nullptr;
    const model::Thresholds*       thresholds = nullptr;
    size_t                         cell       = absent;
    bool                           input_port = false, output_port = false, clock = false;
    hhds::Port_id                  declaration_port = 0;
    std::vector<const model::Arc*> driving_arcs;
    std::array<bool, 2>            load_independent{};
    size_t                         ideal_clock = absent;
  };
  struct Cell {
    std::string                   name;
    const model::Cell*            model = nullptr;
    std::map<std::string, size_t> pins;
    hhds::Node_class              node;
  };
  struct Edge {
    size_t            from = 0, to = 0;
    const model::Arc* model = nullptr;  // null denotes a wire
  };
  std::shared_ptr<const model::Library> library;
  Constraints                           constraints;
  Clock_system                          clock_system;
  uint64_t                              topology_generation, model_generation;
  std::vector<Bound_pin>                pins;
  std::map<std::string, size_t>         identities;
  std::vector<Cell>                     cells;
  std::map<size_t, std::vector<size_t>> nets;
  std::vector<Edge>                     edges;
  std::vector<std::vector<size_t>>      outgoing;
  std::vector<size_t>                   order;
  std::vector<std::string>              unsupported;
  std::vector<std::string>              warnings;
  struct Logic {
    std::vector<std::string> variables;
    uint64_t                 truth = 0;
  };
  std::map<std::pair<const model::Cell*, std::string>, std::optional<Logic>> logic;
  std::vector<std::optional<bool>>                                           constants;
  std::vector<size_t>                                                        uncharacterized_outputs;

  void compile_logic(const model::Cell& cell, const std::string& expression) {
    const auto key = std::pair{&cell, expression};
    if (expression.empty() || logic.contains(key)) {
      return;
    }
    liberty::Comb_cell function;
    function.function = expression;
    for (const auto& [name, pin] : cell.pins) {
      if (pin.direction == "input") {
        function.inputs.push_back(name);
      }
    }
    if (expression == "0" || expression == "1") {
      function.inputs.clear();
    }
    const auto truth = expression == "0"   ? std::optional<uint64_t>{0}
                       : expression == "1" ? std::optional<uint64_t>{~uint64_t{0}}
                                           : liberty::cell_truth(function);
    logic[key] = truth ? std::optional<Logic>{{std::move(function.inputs), *truth}} : std::nullopt;
  }
  std::optional<bool> value(const Cell& cell, const std::string& expression) const {
    const auto found = logic.find({cell.model, expression});
    if (found == logic.end() || !found->second) {
      return {};
    }
    const auto& function = *found->second;
    uint64_t    possible = ~uint64_t{0};
    for (size_t i = 0; i < function.variables.size(); ++i) {
      const auto pin = cell.pins.find(function.variables[i]);
      if (pin == cell.pins.end() || !constants[pin->second]) {
        continue;
      }
      uint64_t basis = 0;
      for (unsigned bit = 0; bit < 64; ++bit) {
        basis |= uint64_t{(bit >> i) & 1} << bit;
      }
      possible &= *constants[pin->second] ? basis : ~basis;
    }
    const auto ones = function.truth & possible;
    if (!ones) {
      return false;
    }
    if (ones == possible) {
      return true;
    }
    return {};
  }
  bool active(const Cell& cell, const model::Pin& output, const model::Arc& arc) const {
    if (!arc.when.empty()) {
      return value(cell, arc.when) != false;
    }
    // Liberty's unconditional default is overridden by a sensitized condition
    // for the same pin pair and timing role (OpenSTA Sim::isDisabledCond).
    for (const auto& peer : output.arcs) {
      if (peer.related_pin == arc.related_pin && peer.type == arc.type && !peer.when.empty() && value(cell, peer.when) == true) {
        return false;
      }
    }
    return true;
  }
  std::pair<bool, bool> sensitivity(const Cell& cell, const model::Pin& output, const std::string& input) const {
    const auto found = logic.find({cell.model, output.function});
    if (found == logic.end() || !found->second) {
      return {true, true};
    }
    const auto& function = *found->second;
    const auto  variable = std::find(function.variables.begin(), function.variables.end(), input);
    if (variable == function.variables.end()) {
      // Characterized output-to-output timing is not a Boolean input of the
      // receiving output function. Keep its declared sense, as OpenSTA does.
      return {true, true};
    }
    const size_t changed  = static_cast<size_t>(variable - function.variables.begin());
    uint64_t     possible = ~uint64_t{0};
    for (size_t i = 0; i < function.variables.size(); ++i) {
      if (i == changed) {
        continue;
      }
      const auto pin = cell.pins.find(function.variables[i]);
      if (pin == cell.pins.end() || !constants[pin->second]) {
        continue;
      }
      uint64_t basis = 0;
      for (unsigned bit = 0; bit < 64; ++bit) {
        basis |= uint64_t{(bit >> i) & 1} << bit;
      }
      possible &= *constants[pin->second] ? basis : ~basis;
    }
    bool positive = false, negative = false;
    for (unsigned bit = 0; bit < 64; ++bit) {
      if ((bit & (1U << changed)) || !(possible & (uint64_t{1} << bit))) {
        continue;
      }
      const bool a = (function.truth >> bit) & 1, b = (function.truth >> (bit | (1U << changed))) & 1;
      positive |= !a && b;
      negative |= a && !b;
    }
    return {positive, negative};
  }

  size_t add(const hhds::Pin_class& pin, const model::Pin* timing, size_t owner, bool input = false, bool output = false) {
    const auto name  = label(pin);
    const auto index = pins.size();
    if (!identities.emplace(name, index).second) {
      throw std::invalid_argument("duplicate physical timing identity: " + name);
    }
    pins.push_back({name,
                    pin,
                    timing,
                    owner == absent ? &library->default_thresholds : &cells[owner].model->thresholds,
                    owner,
                    input,
                    output});
    pins.back().declaration_port = pin.get_port_id();
    return index;
  }
  Impl(const hhds::Graph& graph, std::shared_ptr<const model::Library> models, Constraints limits, uint64_t topology,
       uint64_t generation)
      : library(std::move(models))
      , constraints(std::move(limits))
      , clock_system(constraints.clock, constraints.clocks, constraints.clock_aliases, constraints.asynchronous)
      , topology_generation(topology)
      , model_generation(generation) {
    if (!library) {
      throw std::invalid_argument("native late timing needs a Liberty model");
    }
    unsupported = constraints.unsupported;
    if (constraints.clock && constraints.clock->phase_ps != 0) {
      unsupported.push_back("legacy clock form requires zero phase; use named clocks for ideal offsets");
    }
    if (constraints.clock) {
      const auto& clock = *constraints.clock;
      require_nonnegative(clock.period_ps, "invalid native clock period");
      if (!clock.period_ps || clock.roots.empty()) {
        throw std::invalid_argument("native ideal clock requires a positive period and roots");
      }
      require_nonnegative(clock.uncertainty_ps, "invalid native clock uncertainty");
      for (auto slew : clock.slew_ps) {
        require_nonnegative(slew, "invalid native clock slew");
      }
    }
    const auto ports = [&](const auto& declarations, bool input) {
      for (const auto& declaration : declarations) {
        if (declaration.bits > 1) {
          unsupported.push_back("word-valued graph port requires bit-level physical binding: " + declaration.name);
        }
        const auto pin = input ? graph.get_input_pin(declaration.name) : graph.get_output_pin(declaration.name);
        if (!pin.is_invalid()) {
          add(pin, nullptr, absent, input, !input);
        }
      }
    };
    ports(graph.get_io()->get_input_pin_decls(), true);
    ports(graph.get_io()->get_output_pin_decls(), false);
    // HHDS ordering supplies deterministic instance-aware identities, but its
    // cycle tail is never trusted as a timing DAG. State cuts and a validated
    // pin order are constructed below without modifying the mapped LGraph.
    for (auto node : graph.body().nodes(hhds::Node_order::forward)) {
      if (node.get_debug_nid() == hhds::Graph::INPUT_NODE || node.get_debug_nid() == hhds::Graph::OUTPUT_NODE) {
        continue;
      }
      const auto declaration = node.get_subnode_io();
      if (!declaration || declaration->get_graph()) {
        unsupported.push_back("native late timing requires mapped, flat cell instances: " + std::string(node.get_name()));
        continue;
      }
      const auto found = library->cells.find(std::string(declaration->get_name()));
      if (found == library->cells.end()) {
        unsupported.push_back("uncharacterized cell: " + std::string(node.get_name()) + " (" + std::string(declaration->get_name())
                              + ")");
        continue;
      }
      const size_t cell = cells.size();
      cells.push_back({std::string(node.get_name()), &found->second});
      cells.back().node = node;
      for (const auto& issue : found->second.unsupported) {
        unsupported.push_back(cells.back().name + ": " + issue);
      }
      if (found->second.latch) {
        unsupported.push_back("latch borrowing is unsupported: " + cells.back().name);
      }
      const auto bind = [&](const auto& physical_pins) {
        for (auto pin : physical_pins) {
          const auto name   = declared_pin_name(pin);
          const auto timing = found->second.pins.find(name);
          if (timing == found->second.pins.end()) {
            unsupported.push_back("missing Liberty pin: " + cells[cell].name + ":" + name);
            continue;
          }
          const auto index = add(pin, &timing->second, cell);
          cells[cell].pins.emplace(name, index);
          if (timing->second.three_state) {
            unsupported.push_back("three-state timing is unsupported: " + pins[index].name);
          }
        }
      };
      bind(node.inp_sorted_pins());
      bind(node.out_sorted_pins());
      // Internal output timing arcs remain real even when the related output
      // drives no external net. Bind its declared interface without creating
      // a pin or edge in the authoritative mapped graph.
      std::set<std::string>    related_outputs;
      std::vector<std::string> timing_outputs;
      for (const auto& [name, index] : cells[cell].pins) {
        if (pins[index].model->direction == "output") {
          timing_outputs.push_back(name);
        }
      }
      for (size_t i = 0; i < timing_outputs.size(); ++i) {
        for (const auto& arc : found->second.pins.at(timing_outputs[i]).arcs) {
          const auto source = found->second.pins.find(arc.related_pin);
          if (source != found->second.pins.end() && source->second.direction == "output"
              && !cells[cell].pins.contains(source->first) && related_outputs.insert(source->first).second) {
            timing_outputs.push_back(source->first);
          }
        }
      }
      for (const auto& output : declaration->get_output_pin_decls()) {
        if (!related_outputs.contains(output.name) || cells[cell].pins.contains(output.name)) {
          continue;
        }
        const auto index = pins.size();
        Bound_pin  internal;
        internal.name             = cells[cell].name + ":" + output.name;
        internal.graph_pin        = node.try_get_driver_pin(output.port_id);
        internal.model            = &found->second.pins.at(output.name);
        internal.thresholds       = &found->second.thresholds;
        internal.cell             = cell;
        internal.declaration_port = output.port_id;
        if (!identities.emplace(internal.name, index).second) {
          throw std::invalid_argument("duplicate declared timing identity");
        }
        pins.push_back(std::move(internal));
        cells[cell].pins.emplace(output.name, index);
      }
    }
    // Read the plural driver relation, rejecting multiple physical drivers.
    for (size_t sink = 0; sink < pins.size(); ++sink) {
      const auto& pin = pins[sink];
      if (pin.graph_pin.is_invalid() || pin.input_port || (pin.model && pin.model->direction != "input")) {
        continue;
      }
      const auto drivers = pin.graph_pin.get_driver_pins();
      if (drivers.size() > 1) {
        throw std::invalid_argument("multiple physical timing drivers: " + pin.name);
      }
      if (drivers.empty()) {
        continue;
      }
      const auto found = identities.find(label(drivers.front()));
      if (found == identities.end()) {
        unsupported.push_back("unbound timing driver of " + pin.name);
        continue;
      }
      nets[found->second].push_back(sink);
    }
    for (size_t clock_id = 0; clock_id < clock_system.clocks.size(); ++clock_id) {
      std::queue<size_t> pending;
      for (const auto& name : clock_system.clocks[clock_id].model.roots) {
        const auto found = identities.find(name);
        if (found == identities.end()) {
          throw std::invalid_argument("unknown ideal-clock root: " + name);
        }
        if (pins[found->second].clock && pins[found->second].ideal_clock != clock_id) {
          throw std::invalid_argument("multiple ideal clock waveforms reach root: " + name);
        }
        pins[found->second].clock       = true;
        pins[found->second].ideal_clock = clock_id;
        pending.push(found->second);
      }
      while (!pending.empty()) {
        const auto pin = pending.front();
        pending.pop();
        const auto visit = [&](size_t next) {
          if (pins[next].clock && pins[next].ideal_clock != clock_id) {
            throw std::invalid_argument("multiple ideal clock waveforms reach pin: " + pins[next].name);
          }
          if (!pins[next].clock) {
            pins[next].clock       = true;
            pins[next].ideal_clock = clock_id;
            pending.push(next);
          }
        };
        if (const auto net = nets.find(pin); net != nets.end()) {
          for (auto sink : net->second) {
            visit(sink);
          }
        }
        const auto owner = pins[pin].cell;
        if (owner == absent || !pins[pin].model || pins[pin].model->direction != "input" || cells[owner].model->sequential) {
          continue;
        }
        const auto& cell = cells[owner];
        for (const auto& [name, output] : cell.pins) {
          (void)name;
          if (pins[output].model->direction != "output") {
            continue;
          }
          for (const auto& arc : pins[output].model->arcs) {
            if (arc.related_pin != pins[pin].model->name) {
              continue;
            }
            const auto inputs = std::count_if(cell.pins.begin(), cell.pins.end(), [&](const auto& item) {
              return pins[item.second].model->direction == "input";
            });
            if (inputs == 1 && arc.sense == model::Sense::positive && arc.when.empty() && arc.type == "combinational") {
              visit(output);
            } else {
              unsupported.push_back("ideal clock through unsupported inversion/gating: " + pins[output].name);
            }
          }
        }
      }
    }
    for (const auto& [name, expected] : clock_system.alias_roots) {
      const auto found = identities.find(name);
      if (found == identities.end()) {
        throw std::invalid_argument("unknown forwarded-clock root: " + name);
      }
      if (!pins[found->second].clock || pins[found->second].ideal_clock != expected) {
        unsupported.push_back("forwarded-clock root is not reached by a supported source clock path: " + name);
      }
    }
    for (const auto& [name, boundary] : constraints.inputs) {
      if (!boundary.clock.empty()) {
        clock_system.find(boundary.clock);
      }
      if (!identities.contains(name) || !pins[identities.at(name)].input_port) {
        throw std::invalid_argument("unknown native timing input: " + name);
      }
      for (auto value : boundary.arrival_ps) {
        if (!std::isfinite(value)) {
          throw std::invalid_argument("nonfinite input arrival");
        }
      }
      for (auto value : boundary.slew_ps) {
        require_nonnegative(value, "invalid input slew");
      }
      if (boundary.driving_cell) {
        const auto& drive = *boundary.driving_cell;
        for (auto slew : drive.input_slew_ps) {
          require_nonnegative(slew, "invalid driving-cell input transition");
        }
        const auto cell = library->cells.find(drive.cell);
        if (cell == library->cells.end() || !cell->second.pins.contains(drive.output)) {
          unsupported.push_back("uncharacterized input driving cell: " + name);
          continue;
        }
        const auto&           output = cell->second.pins.at(drive.output);
        std::set<std::string> related;
        for (const auto& arc : output.arcs) {
          if (!drive.related_pin.empty() && arc.related_pin != drive.related_pin) {
            continue;
          }
          if (combinational_arc(arc) || launch_arc(arc)) {
            related.insert(arc.related_pin);
          }
        }
        if (related.size() != 1 || output.direction != "output") {
          unsupported.push_back("ambiguous input driving-cell arc: " + name);
          continue;
        }
        auto& port      = pins[identities.at(name)];
        port.thresholds = &cell->second.thresholds;
        for (const auto& arc : output.arcs) {
          if (arc.related_pin != *related.begin()) {
            continue;
          }
          if (!combinational_arc(arc) && !launch_arc(arc)) {
            continue;
          }
          if (!arc.when.empty() || !arc.unsupported.empty()) {
            unsupported.push_back("unsupported input driving-cell condition/model: " + name);
          }
          port.driving_arcs.push_back(&arc);
        }
      }
    }
    for (const auto& [name, boundary] : constraints.outputs) {
      if (!boundary.clock.empty()) {
        clock_system.find(boundary.clock);
      }
      if (!identities.contains(name) || !pins[identities.at(name)].output_port) {
        throw std::invalid_argument("unknown native timing output: " + name);
      }
      for (auto value : boundary.required_ps) {
        if (!std::isfinite(value)) {
          throw std::invalid_argument("nonfinite output requirement");
        }
      }
      for (auto value : boundary.load_ff) {
        require_nonnegative(value, "invalid output load");
      }
    }
    for (const auto& [driver, sinks] : nets) {
      for (auto sink : sinks) {
        if (!pins[sink].clock && !constraints.cuts.contains(pins[sink].name) && !constraints.cuts.contains(pins[driver].name)) {
          edges.push_back({driver, sink});
        }
      }
    }
    for (const auto& cell : cells) {
      for (const auto& [name, pin] : cell.model->pins) {
        (void)name;
        if (!cell.model->sequential) {
          compile_logic(*cell.model, pin.function);
        }
        for (const auto& arc : pin.arcs) {
          compile_logic(*cell.model, arc.when);
        }
      }
      for (const auto& [name, output] : cell.pins) {
        (void)name;
        const auto& pin = *pins[output].model;
        if (pin.direction != "output" || pins[output].clock) {
          continue;
        }
        bool any = false;
        for (const auto& arc : pin.arcs) {
          const bool clocked = launch_arc(arc);
          if (!combinational_arc(arc) && !clocked) {
            continue;
          }
          const auto related = cell.model->pins.find(arc.related_pin);
          if (!clocked && related != cell.model->pins.end() && related->second.direction == "output") {
            warnings.push_back("Liberty propagation group from an output pin: " + cell.name + ":" + arc.related_pin + " -> "
                               + pin.name);
          }
          any = true;
          for (const auto& issue : arc.unsupported) {
            unsupported.push_back(pins[output].name + ": " + issue);
          }
          if (!arc.when.empty() && !logic.at({cell.model, arc.when})) {
            unsupported.push_back("unsupported conditional expression: " + pins[output].name + " when " + arc.when);
          }
          if (clocked) {
            if (arc.type != "rising_edge") {
              unsupported.push_back("falling-edge clock launch is unsupported: " + pins[output].name);
            }
            continue;
          }
          if (cell.model->sequential) {
            unsupported.push_back("combinational arc on state cell is unsupported: " + pins[output].name);
            continue;
          }
          const auto input = cell.pins.find(arc.related_pin);
          if (input == cell.pins.end()) {
            unsupported.push_back("unbound related timing pin: " + cell.name + ":" + arc.related_pin);
            continue;
          }
          if (arc.sense == model::Sense::unknown) {
            unsupported.push_back("missing timing sense: " + pins[output].name);
          }
          edges.push_back({input->second, output, &arc});
        }
        if (!any) {
          uncharacterized_outputs.push_back(output);
        }
      }
    }
    outgoing.resize(pins.size());
    std::vector<size_t> indegree(pins.size());
    for (size_t i = 0; i < edges.size(); ++i) {
      outgoing[edges[i].from].push_back(i);
      ++indegree[edges[i].to];
    }
    std::queue<size_t> ready;
    for (size_t i = 0; i < pins.size(); ++i) {
      if (!indegree[i]) {
        ready.push(i);
      }
    }
    while (!ready.empty()) {
      const auto pin = ready.front();
      ready.pop();
      order.push_back(pin);
      for (auto edge : outgoing[pin]) {
        if (--indegree[edges[edge].to] == 0) {
          ready.push(edges[edge].to);
        }
      }
    }
    if (order.size() != pins.size()) {
      throw std::invalid_argument("unresolved combinational timing cycle after state cuts");
    }
    constants.resize(pins.size());
    for (const auto& [name, constant] : constraints.constants) {
      if (!identities.contains(name)) {
        throw std::invalid_argument("unknown native case-analysis pin: " + name);
      }
      constants[identities.at(name)] = constant;
    }
    for (auto index : order) {
      const auto& pin = pins[index];
      if (!constants[index] && pin.model && pin.model->direction == "output" && !cells[pin.cell].model->sequential) {
        constants[index] = value(cells[pin.cell], pin.model->function);
      }
      if (constants[index]) {
        for (auto edge_id : outgoing[index]) {
          if (!edges[edge_id].model) {
            const auto sink = edges[edge_id].to;
            if (constants[sink] && constants[sink] != constants[index]) {
              throw std::invalid_argument("conflicting native case analysis");
            }
            constants[sink] = constants[index];
          }
        }
      }
    }
    for (auto output : uncharacterized_outputs) {
      if (!constants[output]) {
        unsupported.push_back("output has no characterized propagation arcs: " + pins[output].name);
      }
    }
    for (auto& pin : pins) {
      std::array<bool, 2> characterized{}, dependent{};
      const auto          inspect = [&](const model::Arc& arc) {
        for (unsigned t = 0; t < 2; ++t) {
          if (arc.delay[t]) {
            characterized[t]  = true;
            dependent[t]     |= depends_on_load(*arc.delay[t]);
          }
        }
      };
      if (pin.model) {
        for (const auto& arc : pin.model->arcs) {
          inspect(arc);
        }
      }
      for (const auto* arc : pin.driving_arcs) {
        inspect(*arc);
      }
      for (unsigned t = 0; t < 2; ++t) {
        pin.load_independent[t] = characterized[t] && !dependent[t];
      }
    }
  }

  using Boundaries = std::vector<std::array<std::optional<double>, 2>>;
  template <typename State>
  void capture_graph(Result& result, std::optional<std::string> launch_clock, const Boundaries& boundaries,
                     const std::vector<Delays>& delays, const State& state) const {
    auto&        graph  = *result.feasible_graph;
    const size_t domain = graph.launch_domains.size();
    graph.launch_domains.push_back(std::move(launch_clock));
    std::vector<std::array<size_t, 2>> vertices(pins.size(), {absent, absent});
    for (size_t i = 0; i < pins.size(); ++i) {
      for (unsigned t = 0; t < 2; ++t) {
        const auto& value = state(i, t);
        if (!value.arrival_ps) {
          continue;
        }
        vertices[i][t] = graph.vertices.size();
        graph.vertices.push_back({i, domain, t, boundaries[i][t], result.pins[i].endpoint ? value.required_ps : std::nullopt});
      }
    }
    for (size_t id = 0; id < edges.size(); ++id) {
      const auto& edge = edges[id];
      for (unsigned in = 0; in < 2; ++in) {
        for (unsigned out = 0; out < 2; ++out) {
          if (delays[id][in][out] && vertices[edge.from][in] != absent && vertices[edge.to][out] != absent) {
            graph.arcs.push_back(
                {vertices[edge.from][in], vertices[edge.to][out], id, *delays[id][in][out], edge.model == nullptr});
          }
        }
      }
    }
  }

  Result evaluate_domains(Result result, const std::vector<Launch>& launches, const std::vector<Delays>& cell_delays,
                          const std::vector<Pair>&                                 wire_delay,
                          const std::vector<std::array<std::optional<double>, 2>>& driving_delay) const {
    const auto issue = [&](std::string text) { result.unsupported.push_back(std::move(text)); };
    struct Check {
      size_t                               pin, clock;
      std::array<std::optional<double>, 2> required;
    };
    std::vector<Check> checks;
    for (size_t id = 0; id < clock_system.clocks.size(); ++id) {
      const auto& clock                   = clock_system.clocks[id];
      result.clocks[clock.name].period_ps = clock.model.period_ps;
    }
    for (size_t i = 0; i < pins.size(); ++i) {
      for (auto& state : result.pins[i].transitions) {
        state.arrival_ps.reset();
        state.required_ps.reset();
        state.predecessor.reset();
      }
      const auto& pin = pins[i];
      if (constants[i] || pin.clock || constraints.cuts.contains(pin.name)) {
        continue;
      }
      if (pin.output_port) {
        result.pins[i].endpoint = true;
        const auto boundary     = constraints.outputs.find(pin.name);
        if (boundary == constraints.outputs.end()) {
          issue("unconstrained output requirement: " + pin.name);
        } else {
          const auto& output = boundary->second;
          checks.push_back({
              i,
              output.clock.empty() ? absent : clock_system.find(output.clock),
              {output.required_ps[0], output.required_ps[1]}
          });
        }
      }
      if (!pin.model || pin.model->direction != "input") {
        continue;
      }
      for (const auto& arc : pin.model->arcs) {
        if (!arc.type.starts_with("setup_") || !active(cells[pin.cell], *pin.model, arc)) {
          continue;
        }
        result.pins[i].endpoint = true;
        const auto related      = cells[pin.cell].pins.find(arc.related_pin);
        if (arc.type != "setup_rising") {
          issue("falling-edge setup is unsupported: " + pin.name);
          continue;
        }
        if (!arc.when.empty() && !logic.at({cells[pin.cell].model, arc.when})) {
          issue("unsupported conditional setup expression: " + pin.name);
        }
        if (related == cells[pin.cell].pins.end() || !pins[related->second].clock) {
          issue("unconstrained setup clock: " + pin.name);
          continue;
        }
        const auto  clock_id = pins[related->second].ideal_clock;
        const auto& clock    = clock_system.clocks[clock_id].model;
        Check       check{i, clock_id, {}};
        for (unsigned t = 0; t < 2; ++t) {
          const auto slew = result.pins[i].transitions[t].slew_ps;
          if (!arc.constraint[t] || !slew) {
            issue("missing setup table or data slew: " + pin.name);
            continue;
          }
          const double setup = arc.constraint[t]->lookup({.related_slew_ps = clock.slew_ps[0], .constrained_slew_ps = *slew});
          check.required[t]  = clock.period_ps - clock.uncertainty_ps - setup;
        }
        checks.push_back(check);
      }
    }
    std::vector<Delays> arc_delays(edges.size());
    for (size_t id = 0; id < edges.size(); ++id) {
      const auto& edge = edges[id];
      if (constants[edge.from] || constants[edge.to]
          || (edge.model && !active(cells[pins[edge.to].cell], *pins[edge.to].model, *edge.model))) {
        continue;
      }
      const auto sense = edge.model ? sensitivity(cells[pins[edge.to].cell], *pins[edge.to].model, edge.model->related_pin)
                                    : std::pair{true, true};
      for (unsigned in = 0; in < 2; ++in) {
        for (unsigned out = 0; out < 2; ++out) {
          if (edge.model) {
            if (!sensitized_transition(*edge.model, sense, in, out)) {
              continue;
            }
            arc_delays[id][in][out] = cell_delays[id][in][out];
            if (!arc_delays[id][in][out]) {
              issue("missing cell delay/transition table or input waveform: " + pins[edge.to].name);
            }
          } else if (in == out && driving_delay[edge.from][out]) {
            arc_delays[id][in][out] = *driving_delay[edge.from][out] + wire_delay[edge.to][out];
          }
        }
      }
    }
    const size_t     unclocked = clock_system.clocks.size();
    std::set<size_t> active_domains;
    for (const auto& launch : launches) {
      active_domains.insert(launch.clock);
    }
    for (const auto& [name, input] : constraints.inputs) {
      const auto index = identities.at(name);
      if (!pins[index].clock && !constants[index] && !constraints.cuts.contains(name)) {
        active_domains.insert(input.clock.empty() ? unclocked : clock_system.find(input.clock));
      }
    }
    std::vector<std::optional<double>>          endpoint_slack(pins.size());
    std::map<std::pair<size_t, size_t>, double> clock_endpoint_slack;
    bool                                        absolute_output = false;
    for (size_t source_clock : active_domains) {
      Result::Domain domain;
      if (source_clock != unclocked) {
        domain.launch_clock = clock_system.clocks[source_clock].name;
      }
      domain.pins.resize(pins.size());
      for (const auto& launch : launches) {
        if (launch.clock == source_clock) {
          maximum(domain.pins[launch.pin][launch.transition].arrival_ps, launch.arrival);
        }
      }
      for (const auto& [name, input] : constraints.inputs) {
        const auto index = identities.at(name);
        const auto id    = input.clock.empty() ? unclocked : clock_system.find(input.clock);
        if (id != source_clock || pins[index].clock || constants[index] || constraints.cuts.contains(name)) {
          continue;
        }
        const double phase = id == unclocked ? 0 : clock_system.clocks[id].model.phase_ps;
        for (unsigned t = 0; t < 2; ++t) {
          domain.pins[index][t].arrival_ps = phase + input.arrival_ps[t];
        }
      }
      Boundaries boundaries;
      if (result.feasible_graph) {
        boundaries.resize(pins.size());
        for (size_t i = 0; i < pins.size(); ++i) {
          for (unsigned t = 0; t < 2; ++t) {
            boundaries[i][t] = domain.pins[i][t].arrival_ps;
          }
        }
      }
      for (auto from : order) {
        ++result.visited_pins;
        for (auto id : outgoing[from]) {
          const auto& edge = edges[id];
          for (unsigned in = 0; in < 2; ++in) {
            const auto arrival = domain.pins[from][in].arrival_ps;
            if (!arrival) {
              continue;
            }
            for (unsigned out = 0; out < 2; ++out) {
              const auto delay = arc_delays[id][in][out];
              if (!delay) {
                continue;
              }
              auto& target = domain.pins[edge.to][out];
              if (!target.arrival_ps || *arrival + *delay > *target.arrival_ps) {
                target.arrival_ps  = *arrival + *delay;
                target.predecessor = Predecessor{from, in};
              }
              ++result.evaluated_arcs;
            }
          }
        }
      }
      for (const auto& check : checks) {
        double adjustment = 0;
        if (check.clock != absent) {
          const auto& capture = clock_system.clocks[check.clock].model;
          if (source_clock == unclocked) {
            adjustment = capture.phase_ps;
          } else {
            const auto separation = clock_system.separation(source_clock, check.clock);
            if (!separation) {
              continue;
            }
            adjustment = clock_system.clocks[source_clock].model.phase_ps + *separation - capture.period_ps;
          }
        }
        for (unsigned t = 0; t < 2; ++t) {
          auto& state = domain.pins[check.pin][t];
          if (!state.arrival_ps || !check.required[t]) {
            continue;
          }
          const double required = *check.required[t] + adjustment;
          minimum(state.required_ps, required);
          const double slack = required - *state.arrival_ps;
          minimum(endpoint_slack[check.pin], slack);
          if (check.clock == absent) {
            absolute_output = true;
          } else {
            const auto key = std::pair{check.clock, check.pin};
            if (const auto old = clock_endpoint_slack.find(key); old != clock_endpoint_slack.end()) {
              old->second = std::min(old->second, slack);
            } else {
              clock_endpoint_slack.emplace(key, slack);
            }
            if (source_clock != unclocked && source_clock != check.clock) {
              result.clocks.at(clock_system.clocks[check.clock].name).independent_period = false;
            }
          }
        }
      }
      if (result.feasible_graph) {
        capture_graph(result, domain.launch_clock, boundaries, arc_delays, [&](size_t i, unsigned t) -> const auto& {
          return domain.pins[i][t];
        });
      }
      for (auto it = order.rbegin(); it != order.rend(); ++it) {
        ++result.required_pins;
        for (auto id : outgoing[*it]) {
          const auto& edge = edges[id];
          for (unsigned in = 0; in < 2; ++in) {
            for (unsigned out = 0; out < 2; ++out) {
              const auto required = domain.pins[edge.to][out].required_ps;
              if (required && arc_delays[id][in][out]) {
                minimum(domain.pins[*it][in].required_ps, *required - *arc_delays[id][in][out]);
              }
            }
          }
        }
      }
      for (size_t i = 0; i < pins.size(); ++i) {
        for (unsigned t = 0; t < 2; ++t) {
          const auto& source    = domain.pins[i][t];
          auto&       aggregate = result.pins[i].transitions[t];
          if (source.arrival_ps && (!aggregate.arrival_ps || *source.arrival_ps > *aggregate.arrival_ps)) {
            aggregate.arrival_ps  = source.arrival_ps;
            aggregate.predecessor = source.predecessor;
          }
        }
      }
      result.domains.push_back(std::move(domain));
    }
    // Aggregate required=maximum arrival+worst eligible slack, matching the
    // reference-style scalar report. Optimization retains each launch domain's
    // actual required times and predecessor records above.
    for (size_t i = 0; i < pins.size(); ++i) {
      for (unsigned t = 0; t < 2; ++t) {
        std::optional<double> slack;
        for (const auto& domain : result.domains) {
          const auto& state = domain.pins[i][t];
          if (state.arrival_ps && state.required_ps) {
            minimum(slack, *state.required_ps - *state.arrival_ps);
          }
        }
        auto& aggregate = result.pins[i].transitions[t];
        if (slack && aggregate.arrival_ps) {
          aggregate.required_ps = *aggregate.arrival_ps + *slack;
        }
        if (result.pins[i].endpoint && !aggregate.arrival_ps) {
          issue("uncovered endpoint transition: " + pins[i].name);
        }
      }
      if (endpoint_slack[i]) {
        minimum(result.wns_ps, *endpoint_slack[i]);
        result.tns_ps = result.tns_ps.value_or(0) + std::min(0., *endpoint_slack[i]);
      }
    }
    for (const auto& [key, slack] : clock_endpoint_slack) {
      auto& clock = result.clocks.at(clock_system.clocks[key.first].name);
      minimum(clock.wns_ps, slack);
      clock.tns_ps = clock.tns_ps.value_or(0) + std::min(0., slack);
      ++clock.timed_endpoints;
    }
    std::sort(result.unsupported.begin(), result.unsupported.end());
    result.unsupported.erase(std::unique(result.unsupported.begin(), result.unsupported.end()), result.unsupported.end());
    result.certified     = constraints.complete && result.unsupported.empty() && result.wns_ps.has_value();
    size_t active_clocks = 0;
    for (auto& [name, clock] : result.clocks) {
      (void)name;
      if (clock.wns_ps) {
        ++active_clocks;
        if (result.certified && clock.independent_period) {
          clock.achievable_period_ps = std::max(0., clock.period_ps - *clock.wns_ps);
        }
      }
    }
    if (result.certified && active_clocks == 1 && !absolute_output) {
      for (const auto& [name, clock] : result.clocks) {
        (void)name;
        if (clock.achievable_period_ps) {
          result.achievable_period_ps = clock.achievable_period_ps;
          if (result.feasible_graph) {
            result.feasible_graph->reference_period_ps = clock.period_ps;
          }
        }
      }
    }
    return result;
  }

  Result evaluate(const physical::Parasitics* rc, Star_context* star, Evaluation_options options) const {
    if (star && !rc) {
      throw std::invalid_argument("continuous wire timing requires reference RC");
    }
    Result result;
    if (options.capture_feasible_graph) {
      result.feasible_graph.emplace();
    }
    result.topology_generation           = topology_generation;
    result.model_generation              = model_generation;
    result.characterized_boundary_policy = options.driver.characterized_boundary;
    result.bounded_boundary_policy       = options.driver.bounded_boundary;
    result.unsupported                   = unsupported;
    result.warnings                      = warnings;
    const auto issue                     = [&](std::string text) { result.unsupported.push_back(std::move(text)); };
    for (const auto& pin : pins) {
      Pin_result bound;
      bound.name             = pin.name;
      bound.graph_pin        = pin.graph_pin;
      bound.graph_node       = pin.cell == absent ? pin.graph_pin.get_master_node() : cells[pin.cell].node;
      bound.declaration_port = pin.declaration_port;
      bound.clock            = pin.clock;
      result.pins.push_back(std::move(bound));
    }
    for (size_t i = 0; i < pins.size(); ++i) {
      result.pins[i].constant = constants[i];
    }
    std::vector<Pair>                                   load(pins.size()), elmore(pins.size()), wire_delay(pins.size());
    std::vector<std::optional<std::array<Pi_model, 2>>> pi_models(pins.size());
    if (rc) {
      for (const auto& [driver, sinks] : nets) {
        (void)sinks;
        if (!pins[driver].clock && !constants[driver] && (pins[driver].load_independent[0] || pins[driver].load_independent[1])) {
          ++result.delay_only_wire_drivers;
        }
      }
      if (result.delay_only_wire_drivers) {
        result.warnings.push_back("load-independent delay tables: RC uses Elmore delay with unchanged driver waveform on "
                                  + std::to_string(result.delay_only_wire_drivers) + " drivers");
      }
    }
    for (const auto& [driver, sinks] : nets) {
      for (auto sink : sinks) {
        for (unsigned t = 0; t < 2; ++t) {
          const auto& pin = pins[sink];
          if (pin.model) {
            if (pin.model->capacitance_ff[t]) {
              load[driver][t] += *pin.model->capacitance_ff[t];
            } else {
              issue("missing Liberty input capacitance: " + pin.name);
            }
          } else if (const auto boundary = constraints.outputs.find(pin.name); boundary != constraints.outputs.end()) {
            load[driver][t] += boundary->second.load_ff[t];
          }
        }
      }
    }
    std::map<size_t, const physical::Rc_net*> physical_nets;
    std::vector<Star_net>                     star_nets;
    if (rc) {
      for (const auto& net : rc->nets) {
        const auto found = identities.find(net.driver);
        if (found == identities.end() || !nets.contains(found->second)) {
          throw std::invalid_argument("RC driver is absent from mapped connectivity: " + net.driver);
        }
        if (!physical_nets.emplace(found->second, &net).second) {
          throw std::invalid_argument("duplicate physical RC driver");
        }
      }
      for (const auto& [driver, sinks] : nets) {
        if (pins[driver].clock || constraints.cuts.contains(pins[driver].name)) {
          continue;
        }
        const auto found = physical_nets.find(driver);
        if (found == physical_nets.end()) {
          issue("missing physical RC net: " + pins[driver].name);
          continue;
        }
        const auto&                   net = *found->second;
        std::vector<Pair>             cap(net.nodes.size());
        std::map<std::string, size_t> nodes;
        for (size_t i = 0; i < net.nodes.size(); ++i) {
          const auto& node = net.nodes[i];
          require_nonnegative(node.capacitance_ff, "invalid wire capacitance");
          cap[i] = {node.capacitance_ff, node.capacitance_ff};
          if (node.pin.empty()) {
            continue;
          }
          if (!nodes.emplace(node.pin, i).second) {
            throw std::invalid_argument("duplicate physical RC pin");
          }
          if (node.pin == net.driver) {
            continue;
          }
          const auto pin = identities.find(node.pin);
          if (pin == identities.end() || std::find(sinks.begin(), sinks.end(), pin->second) == sinks.end()) {
            throw std::invalid_argument("physical RC sink differs from mapped connectivity: " + node.pin);
          }
          for (unsigned t = 0; t < 2; ++t) {
            const auto& bound = pins[pin->second];
            if (bound.model) {
              cap[i][t] += bound.model->capacitance_ff[t].value_or(0);
            } else if (const auto output = constraints.outputs.find(bound.name); output != constraints.outputs.end()) {
              cap[i][t] += output->second.load_ff[t];
            }
          }
        }
        if (!nodes.contains(net.driver)) {
          throw std::invalid_argument("physical RC has no driver node");
        }
        for (auto sink : sinks) {
          if (!nodes.contains(pins[sink].name)) {
            throw std::invalid_argument("physical RC omits mapped sink: " + pins[sink].name);
          }
        }
        if (star) {
          Star_net input;
          input.reference = &net;
          const auto add  = [&](size_t pin, Pair pin_cap) {
            const auto location = star->locations.find(pins[pin].name);
            if (location == star->locations.end()) {
              throw std::invalid_argument("continuous wire timing lacks physical pin position: " + pins[pin].name);
            }
            input.pins.push_back({pins[pin].name, location->second.first, location->second.second, pin_cap});
          };
          add(driver, {});
          for (auto sink : sinks) {
            Pair        pin_cap{};
            const auto& bound = pins[sink];
            if (bound.model) {
              for (unsigned t = 0; t < 2; ++t) {
                pin_cap[t] = bound.model->capacitance_ff[t].value_or(0);
              }
            } else if (const auto output = constraints.outputs.find(bound.name); output != constraints.outputs.end()) {
              pin_cap = output->second.load_ff;
            }
            add(sink, pin_cap);
          }
          star_nets.push_back(std::move(input));
        } else {
          const auto state  = analyze_rc_tree(net, static_cast<unsigned>(nodes.at(net.driver)), cap);
          load[driver]      = state.total_cap_ff;
          pi_models[driver] = state.pi;
          for (auto sink : sinks) {
            elmore[sink] = state.elmore_ps[nodes.at(pins[sink].name)];
          }
        }
      }
    }
    if (star) {
      auto snapshot = star->bank.evaluate(star_nets, star->policy, star->refresh, star->options);
      star->stats   = snapshot.stats;
      for (const auto& net : snapshot.nets) {
        const auto driver = identities.at(net.pins.front());
        load[driver]      = net.timing.total_cap_ff;
        pi_models[driver] = net.timing.pi;
        for (size_t i = 1; i < net.pins.size(); ++i) {
          elmore[identities.at(net.pins[i])] = net.timing.elmore_ps[i];
        }
      }
      result.warnings.push_back(
          "continuous calibrated star wire model; input-admittance and timing fidelity require separate acceptance");
    }
    std::vector<Delays>                                      cell_delays(edges.size());
    std::vector<std::array<std::vector<Driver_waveform>, 2>> driver_shapes(pins.size());
    std::set<size_t> failed_drivers, boundary_drivers, bounded_load_drivers, bounded_crossing_drivers;
    const auto       evaluate_arc
        = [&](const model::Arc& arc, unsigned t, double input_slew, size_t driver) -> std::optional<Driver_waveform> {
      if (!arc.delay[t] || !arc.slew[t]) {
        return {};
      }
      auto fitted = fit_driver(*arc.delay[t],
                               *arc.slew[t],
                               input_slew,
                               load[driver][t],
                               *pins[driver].thresholds,
                               t,
                               pi_models[driver] ? &(*pi_models[driver])[t] : nullptr,
                               options.driver);
      ++result.driver_fits;
      if (fitted.mode == Driver_mode::distributed) {
        ++result.distributed_driver_fits;
      } else if (fitted.mode == Driver_mode::lumped) {
        ++result.lumped_driver_fits;
      } else if (fitted.mode == Driver_mode::table_boundary) {
        ++result.table_boundary_driver_fits;
        if (boundary_drivers.insert(driver).second) {
          result.warnings.push_back("native characterized total-load boundary after proven positive-ramp infeasibility: "
                                    + pins[driver].name);
        }
      } else if (fitted.mode == Driver_mode::full_load_boundary) {
        ++result.full_load_boundary_driver_fits;
        if (bounded_load_drivers.insert(driver).second) {
          result.warnings.push_back("native low-wire-resistance full-load characterized boundary: " + pins[driver].name);
        }
      } else if (fitted.mode == Driver_mode::crossing_boundary) {
        ++result.crossing_boundary_driver_fits;
        if (bounded_crossing_drivers.insert(driver).second) {
          result.warnings.push_back("native low-wire-resistance bounded crossing characterized boundary: " + pins[driver].name);
        }
      } else if (fitted.mode == Driver_mode::failed) {
        ++result.driver_fit_failures;
        if (failed_drivers.insert(driver).second) {
          std::ostringstream detail;
          detail << std::setprecision(17)
                 << "native distributed driver fit failed; total-load observation retained: " << pins[driver].name
                 << " related=" << arc.related_pin << " when=" << arc.when << " transition=" << t
                 << " reason=" << static_cast<unsigned>(fitted.failure) << " input_slew_ps=" << input_slew
                 << " load_ff=" << load[driver][t];
          if (pi_models[driver]) {
            const auto& pi = (*pi_models[driver])[t];
            detail << " near_ff=" << pi.near_cap_ff << " far_ff=" << pi.far_cap_ff << " resistance_ohm=" << pi.resistance_ohm;
          }
          issue(detail.str());
        }
      }
      driver_shapes[driver][t].push_back(fitted);
      return fitted;
    };
    std::vector<std::array<std::optional<double>, 2>> driving_delay(pins.size(), {0., 0.});
    for (const auto& [name, boundary] : constraints.inputs) {
      if (boundary.driving_cell) {
        const auto index     = identities.at(name);
        driving_delay[index] = {};
        const auto& drive    = *boundary.driving_cell;
        for (const auto* arc : pins[index].driving_arcs) {
          for (unsigned in = 0; in < 2; ++in) {
            for (unsigned out = 0; out < 2; ++out) {
              if ((arc->type == "rising_edge" && in != 0) || (arc->type == "falling_edge" && in != 1)) {
                continue;
              }
              // Edge launch arcs are non-unate; only their launching edge applies.
              if (!launch_arc(*arc) && !arc_transition(*arc, in, out)) {
                continue;
              }
              if (!arc->delay[out] || !arc->slew[out]) {
                continue;
              }
              const auto         fitted = evaluate_arc(*arc, out, drive.input_slew_ps[in], index);
              const model::Query zero{.input_slew_ps = drive.input_slew_ps[in], .output_load_ff = 0};
              // SDC's driving-cell intrinsic delay is removed. The remaining
              // load-dependent delay belongs to each outgoing input-port wire arc.
              maximum(driving_delay[index][out], fitted->delay_ps - arc->delay[out]->lookup(zero));
              maximum(result.pins[index].transitions[out].slew_ps, fitted->slew_ps);
            }
          }
        }
        for (unsigned t = 0; t < 2; ++t) {
          if (!driving_delay[index][t]) {
            issue("missing driving-cell delay/transition table: " + name);
          }
        }
      }
    }
    for (size_t i = 0; i < pins.size(); ++i) {
      for (unsigned t = 0; t < 2; ++t) {
        result.pins[i].transitions[t].load_ff = load[i][t];
      }
      if (pins[i].input_port && !pins[i].clock && !constants[i] && !constraints.cuts.contains(pins[i].name)) {
        const auto boundary = constraints.inputs.find(pins[i].name);
        if (boundary == constraints.inputs.end()) {
          issue("unconstrained input arrival: " + pins[i].name);
          continue;
        }
        for (unsigned t = 0; t < 2; ++t) {
          result.pins[i].transitions[t].arrival_ps = boundary->second.arrival_ps[t];
          if (!boundary->second.driving_cell) {
            result.pins[i].transitions[t].slew_ps = boundary->second.slew_ps[t];
          }
        }
      }
    }
    // State outputs launch from clock-to-Q; D/setup checks are separate cuts.
    std::vector<Launch> launches;
    for (size_t i = 0; i < pins.size(); ++i) {
      const auto& pin = pins[i];
      if (!pin.model || pin.model->direction != "output" || pin.cell == absent || !cells[pin.cell].model->sequential || pin.clock) {
        continue;
      }
      for (const auto& arc : pin.model->arcs) {
        if (!active(cells[pin.cell], *pin.model, arc)) {
          continue;
        }
        if (arc.type != "rising_edge") {
          continue;
        }
        const auto clock = cells[pin.cell].pins.find(arc.related_pin);
        if (clock == cells[pin.cell].pins.end() || !pins[clock->second].clock) {
          issue("unconstrained clock-to-Q launch: " + pin.name);
          continue;
        }
        for (unsigned t = 0; t < 2; ++t) {
          const auto  clock_id = pins[clock->second].ideal_clock;
          const auto& ideal    = clock_system.clocks[clock_id].model;
          const auto  value    = evaluate_arc(arc, t, ideal.slew_ps[0], i);
          if (!value) {
            issue("missing clock-to-Q delay/transition table: " + pin.name);
            continue;
          }
          auto& state = result.pins[i].transitions[t];
          maximum(state.arrival_ps, ideal.phase_ps + value->delay_ps);
          launches.push_back({i, clock_id, t, ideal.phase_ps + value->delay_ps});
          maximum(state.slew_ps, value->slew_ps);
          ++result.evaluated_arcs;
        }
      }
    }
    // Waveform calculation is independent of setup-path feasibility. A
    // side input can be unsensitized for arrival propagation while its
    // physical gate waveform still contributes to the conservative max slew.
    // OpenSTA's DcalcPred likewise differs from its search/simulation predicate.
    for (size_t i = 0; i < pins.size(); ++i) {
      if (pins[i].input_port || pins[i].clock || constants[i]
          || (pins[i].model && pins[i].model->direction == "output" && pins[i].model->arcs.empty())) {
        for (unsigned t = 0; t < 2; ++t) {
          if (!result.pins[i].transitions[t].slew_ps) {
            result.pins[i].transitions[t].slew_ps = pins[i].clock ? clock_system.clocks[pins[i].ideal_clock].model.slew_ps[t] : 0;
          }
        }
      }
    }
    result.waveform_pins = order.size();
    for (auto from : order) {
      for (auto edge_id : outgoing[from]) {
        const auto& edge = edges[edge_id];
        for (unsigned in = 0; in < 2; ++in) {
          const auto& source = result.pins[from].transitions[in];
          if (!source.slew_ps) {
            continue;
          }
          for (unsigned out = 0; out < 2; ++out) {
            double slew = 0;
            if (edge.model) {
              const auto& arc = *edge.model;
              if (!arc_transition(arc, in, out) || !arc.slew[out]) {
                continue;
              }
              if (const auto fitted = evaluate_arc(arc, out, *source.slew_ps, edge.to)) {
                cell_delays[edge_id][in][out] = fitted->delay_ps;
                slew                          = fitted->slew_ps;
              } else {
                slew
                    = std::max(0., arc.slew[out]->lookup({.input_slew_ps = *source.slew_ps, .output_load_ff = load[edge.to][out]}));
              }
            } else {
              if (in != out) {
                continue;
              }
              if (!driving_delay[from][out]) {
                continue;
              }
              std::optional<double> worst_delay, worst_slew;
              if (driver_shapes[from][out].empty()) {
                Driver_waveform ideal;
                ideal.slew_ps       = *source.slew_ps;
                const auto response = ideal.wire(elmore[edge.to][out], *pins[from].thresholds, *pins[edge.to].thresholds, out);
                worst_delay         = response.delay_ps;
                worst_slew          = response.slew_ps;
              } else {
                for (const auto& shape : driver_shapes[from][out]) {
                  const auto response = shape.wire(elmore[edge.to][out], *pins[from].thresholds, *pins[edge.to].thresholds, out);
                  maximum(worst_delay, response.delay_ps);
                  maximum(worst_slew, response.slew_ps);
                }
              }
              wire_delay[edge.to][out] = *worst_delay;
              slew                     = *worst_slew;
            }
            maximum(result.pins[edge.to].transitions[out].slew_ps, slew);
            ++result.waveform_arcs;
          }
        }
      }
      // All incoming waveforms are complete before a pin's outgoing arcs are
      // visited. Release its transient candidates after the last sink uses them.
      for (auto& shapes : driver_shapes[from]) {
        std::vector<Driver_waveform>().swap(shapes);
      }
    }
    if (!constraints.clocks.empty()) {
      return evaluate_domains(std::move(result), launches, cell_delays, wire_delay, driving_delay);
    }
    Boundaries boundaries;
    if (result.feasible_graph) {
      boundaries.resize(pins.size());
      for (size_t i = 0; i < pins.size(); ++i) {
        for (unsigned t = 0; t < 2; ++t) {
          boundaries[i][t] = result.pins[i].transitions[t].arrival_ps;
        }
      }
    }
    std::vector<Delays> delays(edges.size());
    for (auto from : order) {
      ++result.visited_pins;
      if (constants[from]) {
        continue;
      }
      for (auto edge_id : outgoing[from]) {
        const auto& edge = edges[edge_id];
        if (constants[edge.to] || (edge.model && !active(cells[pins[edge.to].cell], *pins[edge.to].model, *edge.model))) {
          continue;
        }
        const auto sense = edge.model ? sensitivity(cells[pins[edge.to].cell], *pins[edge.to].model, edge.model->related_pin)
                                      : std::pair{true, true};
        for (unsigned in = 0; in < 2; ++in) {
          const auto& source = result.pins[from].transitions[in];
          if (!source.arrival_ps || !source.slew_ps) {
            continue;
          }
          for (unsigned out = 0; out < 2; ++out) {
            double delay = 0, slew = 0;
            if (edge.model) {
              if (!sensitized_transition(*edge.model, sense, in, out)) {
                continue;
              }
              const auto value = cell_delays[edge_id][in][out];
              if (!value) {
                issue("missing cell delay/transition table: " + pins[edge.to].name);
                continue;
              }
              delay = *value;
              slew  = result.pins[edge.to].transitions[out].slew_ps.value_or(0);
            } else {
              if (in != out) {
                continue;
              }
              if (!driving_delay[from][out]) {
                continue;
              }
              delay = *driving_delay[from][out] + wire_delay[edge.to][out];
              slew  = result.pins[edge.to].transitions[out].slew_ps.value_or(0);
            }
            delays[edge_id][in][out] = delay;
            auto&        target      = result.pins[edge.to].transitions[out];
            const double arrival     = *source.arrival_ps + delay;
            if (!target.arrival_ps || arrival > *target.arrival_ps) {
              target.arrival_ps  = arrival;
              target.predecessor = Predecessor{from, in};
            }
            maximum(target.slew_ps, slew);
            ++result.evaluated_arcs;
          }
        }
      }
    }
    for (size_t i = 0; i < pins.size(); ++i) {
      const auto& pin = pins[i];
      if (constants[i]) {
        continue;
      }
      if (pin.output_port && !pin.clock && !constraints.cuts.contains(pin.name)) {
        const auto boundary = constraints.outputs.find(pin.name);
        if (boundary == constraints.outputs.end()) {
          issue("unconstrained output requirement: " + pin.name);
          continue;
        }
        result.pins[i].endpoint = true;
        for (unsigned t = 0; t < 2; ++t) {
          result.pins[i].transitions[t].required_ps = boundary->second.required_ps[t];
        }
      }
      if (!pin.model || pin.model->direction != "input" || pin.clock || constraints.cuts.contains(pin.name)) {
        continue;
      }
      for (const auto& arc : pin.model->arcs) {
        if (!active(cells[pin.cell], *pin.model, arc)) {
          continue;
        }
        if (!arc.type.starts_with("setup_")) {
          continue;
        }
        result.pins[i].endpoint = true;
        if (arc.type != "setup_rising") {
          issue("falling-edge setup is unsupported: " + pin.name);
          continue;
        }
        if (!arc.when.empty() && !logic.at({cells[pin.cell].model, arc.when})) {
          issue("unsupported conditional setup expression: " + pin.name);
        }
        const auto clock = cells[pin.cell].pins.find(arc.related_pin);
        if (!constraints.clock || clock == cells[pin.cell].pins.end() || !pins[clock->second].clock) {
          issue("unconstrained setup clock: " + pin.name);
          continue;
        }
        for (unsigned t = 0; t < 2; ++t) {
          const auto& slew = result.pins[i].transitions[t].slew_ps;
          if (!arc.constraint[t] || !slew) {
            issue("missing setup table or data slew: " + pin.name);
            continue;
          }
          const auto setup
              = arc.constraint[t]->lookup({.related_slew_ps = constraints.clock->slew_ps[0], .constrained_slew_ps = *slew});
          minimum(result.pins[i].transitions[t].required_ps,
                  constraints.clock->period_ps - constraints.clock->uncertainty_ps - setup);
        }
      }
    }
    if (result.feasible_graph) {
      capture_graph(result, std::nullopt, boundaries, delays, [&](size_t i, unsigned t) -> const auto& {
        return result.pins[i].transitions[t];
      });
      if (constraints.clock) {
        result.feasible_graph->reference_period_ps = constraints.clock->period_ps;
      }
    }
    result.required_pins = order.size();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      for (auto edge_id : outgoing[*it]) {
        const auto& edge = edges[edge_id];
        for (unsigned in = 0; in < 2; ++in) {
          for (unsigned out = 0; out < 2; ++out) {
            const auto& target = result.pins[edge.to].transitions[out];
            if (target.required_ps && delays[edge_id][in][out]) {
              minimum(result.pins[edge.from].transitions[in].required_ps, *target.required_ps - *delays[edge_id][in][out]);
            }
          }
        }
      }
    }
    for (const auto& pin : result.pins) {
      if (pin.endpoint) {
        std::optional<double> slack;
        for (const auto& state : pin.transitions) {
          if (state.arrival_ps && state.required_ps) {
            minimum(slack, *state.required_ps - *state.arrival_ps);
          } else {
            issue("uncovered endpoint transition: " + pin.name);
          }
        }
        if (slack) {
          minimum(result.wns_ps, *slack);
          result.tns_ps = result.tns_ps.value_or(0) + std::min(0., *slack);
        }
      }
    }
    std::sort(result.unsupported.begin(), result.unsupported.end());
    result.unsupported.erase(std::unique(result.unsupported.begin(), result.unsupported.end()), result.unsupported.end());
    result.certified = constraints.complete && result.unsupported.empty() && result.wns_ps.has_value();
    if (result.certified && constraints.clock) {
      result.achievable_period_ps = std::max(0., constraints.clock->period_ps - *result.wns_ps);
    }
    return result;
  }
};

Proxy::Proxy(const hhds::Graph& graph, std::shared_ptr<const model::Library> models, Constraints constraints, uint64_t topology,
             uint64_t generation)
    : impl(std::make_unique<Impl>(graph, std::move(models), std::move(constraints), topology, generation)) {}
Proxy::~Proxy()                           = default;
Proxy::Proxy(Proxy&&) noexcept            = default;
Proxy& Proxy::operator=(Proxy&&) noexcept = default;
Result Proxy::evaluate(const physical::Parasitics* parasitics, Star_context* star, Evaluation_options options) const {
  return impl->evaluate(parasitics, star, options);
}

timing::Feedback Result::feedback() const {
  if (!certified || !achievable_period_ps) {
    throw std::invalid_argument("native timing feedback requires a certified achievable period");
  }
  timing::Feedback result;
  result.time_unit_ps  = 1;
  result.wns           = wns_ps;
  result.tns           = tns_ps;
  result.delay         = *achievable_period_ps;
  result.source_report = "native_late_proxy";
  for (const auto& pin : pins) {
    auto& target = result.pins[pin.name];
    target.clock = pin.clock;
    for (const auto& state : pin.transitions) {
      if (state.arrival_ps) {
        maximum(target.arrival, *state.arrival_ps);
      }
      if (state.slew_ps) {
        maximum(target.slew, *state.slew_ps);
      }
      if (state.load_ff) {
        maximum(target.load_ff, *state.load_ff);
      }
      if (state.required_ps) {
        minimum(target.required, *state.required_ps);
      }
      if (state.arrival_ps && state.required_ps) {
        minimum(target.slack, *state.required_ps - *state.arrival_ps);
      }
    }
  }
  return result;
}
}  // namespace livehd::placement::late
