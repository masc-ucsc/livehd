// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "endpoint_netlist.hpp"

#include <algorithm>
#include <bit>
#include <map>
#include <set>
#include <unordered_set>

namespace livehd::usyn {
namespace {
Window_function formula_function(const Gate_formula& formula, uint32_t inputs, Budget& work) {
  Window_function result;
  if (inputs > max_logical_inputs || formula.nodes.empty()) {
    return result;
  }
  if (!work.spend(formula.nodes.size() + (uint64_t{1} << inputs) / 64 + 1)) {
    result.status = Status::search_exhausted;
    return result;
  }
  result.table = Truth_table(inputs);
  std::vector<uint64_t>             values(formula.nodes.size());
  constexpr std::array<uint64_t, 6> variables{0xaaaaaaaaaaaaaaaaULL,
                                              0xccccccccccccccccULL,
                                              0xf0f0f0f0f0f0f0f0ULL,
                                              0xff00ff00ff00ff00ULL,
                                              0xffff0000ffff0000ULL,
                                              0xffffffff00000000ULL};
  for (size_t word = 0; word < result.table.words.size(); ++word) {
    if (!work.spend(formula.nodes.size())) {
      result.status = Status::search_exhausted;
      return result;
    }
    for (size_t i = 0; i < formula.nodes.size(); ++i) {
      const auto& n = formula.nodes[i];
      switch (n.kind) {
        case Gate_formula::Kind::constant: values[i] = n.inverted ? ~uint64_t{0} : 0; break;
        case Gate_formula::Kind::literal:
          if (n.variable >= inputs) {
            return result;
          }
          values[i] = n.variable < 6 ? variables[n.variable] : ((word >> (n.variable - 6)) & 1) ? ~uint64_t{0} : 0;
          if (n.inverted) {
            values[i] = ~values[i];
          }
          break;
        case Gate_formula::Kind::series  : values[i] = values[n.left] & values[n.right]; break;
        case Gate_formula::Kind::parallel: values[i] = values[n.left] | values[n.right]; break;
      }
    }
    result.table.words[word] = formula.output_inverted ? ~values.back() : values.back();
  }
  if (inputs < 6) {
    result.table.words[0] &= (uint64_t{1} << (1U << inputs)) - 1;
  }
  result.status = Status::feasible;
  return result;
}

std::vector<Endpoint_rail> rails(const Frozen_cell& cell) {
  std::vector<Endpoint_rail> result;
  for (uint32_t i = 0; i < cell.inputs.size(); ++i) {
    if (cell.metrics.positive & (1U << i)) {
      result.push_back({i, cell.inputs[i]});
    }
    if (cell.metrics.negative & (1U << i)) {
      auto opposite     = cell.inputs[i];
      opposite.inverted = !opposite.inverted;
      result.push_back({i, opposite});
    }
  }
  return result;
}
}  // namespace

Xag_region expand_endpoint_netlist(const Endpoint_netlist& n, Budget& work, uint32_t max_nodes, uint32_t max_formula_nodes) {
  Xag_region result;
  const auto invalid = [&](const char* reason) {
    Xag_region refused;
    refused.reason = reason;
    return refused;
  };
  const auto exhausted = [&] {
    auto refused   = invalid("endpoint netlist expansion budget");
    refused.status = Status::search_exhausted;
    return refused;
  };
  if (n.policy_version != Endpoint_netlist::version || n.residual_kind != Endpoint_kind::Static || !max_nodes || !max_formula_nodes
      || n.clock_phases < 1 || n.clock_phases > 2 || !n.gates.logical_inputs || n.gates.logical_inputs > max_logical_inputs
      || !n.gates.stack || !n.gates.branches || !n.costs.static_and || !n.costs.static_xor || !n.costs.static_not || !n.costs.domino
      || !n.costs.domino_latch) {
    return invalid("invalid endpoint netlist policy");
  }
  if (n.native.size() > max_nodes || n.inputs.size() > max_nodes || n.outputs.size() > max_nodes || n.state.size() > max_nodes
      || n.cells.size() > max_nodes
      || !work.spend(n.native.size() + n.inputs.size() + n.outputs.size() + n.state.size() + n.cells.size())) {
    return exhausted();
  }
  if (n.inputs.size() + n.state.size() != n.native.input_names().size()) {
    return invalid("incomplete endpoint source inventory");
  }
  std::vector<bool>      sources(n.native.input_names().size()), used(n.cells.size());
  std::unordered_set<Id> free_rails;
  std::set<std::string>  names;
  const auto             source = [&](Xsignal signal) {
    if (signal.id >= n.native.size() || signal.inverted || n.native.node(signal.id).kind != Xag::Kind::source) {
      return false;
    }
    const auto index = n.native.node(signal.id).source_index;
    if (index >= sources.size() || sources[index]) {
      return false;
    }
    sources[index] = true;
    return true;
  };
  for (const auto input : n.inputs) {
    if (!source(input)) {
      return invalid("invalid or duplicated primary source");
    }
  }
  std::vector<Xsignal> static_roots;
  for (const auto& output : n.outputs) {
    if (output.signal.id >= n.native.size()) {
      return invalid("invalid endpoint output reference");
    }
    static_roots.push_back(output.signal);
  }
  for (uint32_t i = 0; i < n.state.size(); ++i) {
    const auto& s = n.state[i];
    if (!source(s.q) || s.reference_d.id >= n.native.size() || s.name.empty() || !names.insert(s.name).second
        || (s.init != '0' && s.init != '1' && s.init != 'x')) {
      return invalid("invalid endpoint state identity");
    }
    if (s.domino_latch) {
      if (*s.domino_latch >= n.cells.size() || n.cells[*s.domino_latch].kind != Endpoint_kind::DominoLatch
          || n.cells[*s.domino_latch].state != i) {
        return invalid("invalid endpoint storage owner");
      }
      used[*s.domino_latch] = true;
      free_rails.insert(s.q.id);
    } else {
      static_roots.push_back(s.reference_d);
    }
  }
  Logical_cost cost;
  for (uint32_t i = 0; i < n.cells.size(); ++i) {
    const auto& cell  = n.cells[i];
    const bool  latch = cell.kind == Endpoint_kind::DominoLatch;
    if ((cell.kind != Endpoint_kind::Domino && !latch) || cell.state >= n.state.size()
        || cell.outputs != std::array<std::string, 2>{"Q", "!Q"} || cell.inputs.size() > n.gates.logical_inputs
        || cell.phase != (latch ? n.clock_phases : 1) || (!latch && n.clock_phases == 1)
        || (latch && (n.state[cell.state].domino_latch != i || cell.name != n.state[cell.state].name))) {
      return invalid("invalid endpoint cell kind, ports, phase or storage correspondence");
    }
    if (cell.formula.nodes.size() > max_formula_nodes || !work.spend(cell.formula.nodes.size() + 3 * cell.inputs.size())) {
      return exhausted();
    }
    const auto metrics = cell.formula.metrics();
    if (!metrics || *metrics != cell.metrics || metrics->support != (1U << cell.inputs.size()) - 1 || metrics->stack > n.gates.stack
        || metrics->branches > n.gates.branches || cell.rails != rails(cell)) {
      return invalid("invalid endpoint formula, rail demand or technology legality");
    }
    const auto function = formula_function(cell.formula, cell.inputs.size(), work);
    if (function.status == Status::search_exhausted) {
      return exhausted();
    }
    if (function.status != Status::feasible || function.table != cell.function) {
      return invalid("endpoint formula does not implement its exact function");
    }
    for (const auto& input : cell.inputs) {
      if (input.space == Endpoint_ref::Space::native) {
        if (input.index >= n.native.size()) {
          return invalid("invalid static cell input");
        }
      } else if (input.space == Endpoint_ref::Space::cell) {
        if (input.index >= i || n.cells[input.index].kind != Endpoint_kind::Domino || n.cells[input.index].phase >= cell.phase) {
          return invalid("invalid same-cycle cell dependency or clock phase");
        }
        const auto producer_state = n.cells[input.index].state;
        if (producer_state != cell.state
            && (n.state[cell.state].domain == unknown_clock_domain
                || n.state[cell.state].domain != n.state[producer_state].domain)) {
          return invalid("cell dependency crosses clock domains");
        }
        used[input.index] = true;
      } else {
        return invalid("invalid cell input space");
      }
    }
    for (const auto& rail : cell.rails) {
      if (rail.producer.space == Endpoint_ref::Space::native) {
        static_roots.push_back({rail.producer.index, rail.producer.inverted});
      }
    }
    cost.domino += uint64_t{metrics->transistors} + (latch ? n.costs.domino_latch : n.costs.domino);
  }
  if (std::find(used.begin(), used.end(), false) != used.end()) {
    return invalid("unused or unowned endpoint cell");
  }
  std::unordered_set<Id> visited, inverted;
  while (!static_roots.empty()) {
    if (!work.spend()) {
      return exhausted();
    }
    const auto signal = static_roots.back();
    static_roots.pop_back();
    if (!signal.id) {
      continue;
    }
    if (signal.inverted && !free_rails.contains(signal.id)) {
      inverted.insert(signal.id);
    }
    if (!visited.insert(signal.id).second) {
      continue;
    }
    const auto& node = n.native.node(signal.id);
    if (node.kind == Xag::Kind::and_gate || node.kind == Xag::Kind::xor_gate) {
      cost.static_logic += node.kind == Xag::Kind::and_gate ? n.costs.static_and : n.costs.static_xor;
      static_roots.insert(static_roots.end(), node.inputs.begin(), node.inputs.end());
    }
  }
  cost.inverters = inverted.size() * uint64_t{n.costs.static_not};
  if (cost.static_logic != n.estimated_cost.static_logic || cost.inverters != n.estimated_cost.inverters
      || cost.domino != n.estimated_cost.domino) {
    return invalid("endpoint mixed-network cost mismatch");
  }
  result.graph   = n.native;
  result.inputs  = n.inputs;
  result.outputs = n.outputs;
  for (const auto& s : n.state) {
    result.state.push_back({s.name, s.init, s.q, s.reference_d});
  }
  std::vector<Xsignal> cells;
  for (const auto& cell : n.cells) {
    std::vector<Xsignal> values;
    for (const auto& node : cell.formula.nodes) {
      if (!work.spend()) {
        return exhausted();
      }
      Xsignal value;
      switch (node.kind) {
        case Gate_formula::Kind::constant: value = result.graph.constant(node.inverted); break;
        case Gate_formula::Kind::literal : {
          const auto& input = cell.inputs[node.variable];
          value = input.space == Endpoint_ref::Space::native ? Xsignal{input.index, input.inverted} : cells[input.index];
          if (input.space == Endpoint_ref::Space::cell && input.inverted) {
            value = ~value;
          }
          if (node.inverted) {
            value = ~value;
          }
          break;
        }
        case Gate_formula::Kind::series  : value = result.graph.land(values[node.left], values[node.right]); break;
        case Gate_formula::Kind::parallel: value = result.graph.lor(values[node.left], values[node.right]); break;
      }
      if (result.graph.size() > max_nodes) {
        return exhausted();
      }
      values.push_back(value);
    }
    cells.push_back(cell.formula.output_inverted ? ~values.back() : values.back());
    if (cell.kind == Endpoint_kind::DominoLatch) {
      if (cells.back() != n.state[cell.state].reference_d) {
        return invalid("frozen endpoint does not reconstruct its canonical next-state function");
      }
      result.state[cell.state].d = cells.back();
    }
  }
  result.status = Status::feasible;
  return result;
}

Endpoint_netlist_result freeze_endpoint_netlist(const Logical_region& region, const Logical_options& options, Budget& work,
                                                std::span<const uint32_t> domains) {
  Endpoint_netlist_result result;
  const auto&             logic = region.logic;
  if (logic.status != Status::feasible || !valid_endpoint_options(options.endpoint) || !options.max_nodes
      || (!domains.empty() && domains.size() != logic.state.size())) {
    result.reason = "invalid endpoint freezing request";
    return result;
  }
  const auto exhausted = [&] {
    result.status = Status::search_exhausted;
    result.reason = "endpoint freezing budget";
    return std::move(result);
  };
  if (logic.graph.size() > options.max_nodes || logic.inputs.size() > options.max_nodes || logic.outputs.size() > options.max_nodes
      || logic.state.size() > options.max_nodes || region.endpoints.size() > logic.state.size()
      || !work.spend(logic.graph.size() + logic.inputs.size() + logic.outputs.size() + logic.state.size()
                     + region.endpoints.size())) {
    return exhausted();
  }
  Endpoint_netlist frozen;
  frozen.native         = logic.graph;
  frozen.inputs         = logic.inputs;
  frozen.outputs        = logic.outputs;
  frozen.clock_phases   = options.endpoint.clock_phases;
  frozen.gates          = options.endpoint.gates;
  frozen.costs          = options.endpoint.cost;
  frozen.estimated_cost = region.cost;
  for (uint32_t i = 0; i < logic.state.size(); ++i) {
    const auto& s = logic.state[i];
    frozen.state.push_back({s.name, s.init, s.q, s.d, domains.empty() ? unknown_clock_domain : domains[i], {}});
  }
  // A stored reference carries the emitted cell's logical output polarity;
  // aliases select Q or !Q relative to that first representative.
  std::map<std::vector<uint64_t>, Endpoint_ref> phase_cells;
  for (const auto& e : region.endpoints) {
    if (e.state_index >= frozen.state.size() || frozen.state[e.state_index].domino_latch || e.cells.empty()
        || e.name != frozen.state[e.state_index].name) {
      result.reason = "invalid or duplicate endpoint storage owner";
      return result;
    }
    std::vector<Endpoint_ref> mapped;
    for (size_t i = 0; i < e.cells.size(); ++i) {
      const auto& c = e.cells[i];
      if (frozen.cells.size() >= options.max_nodes || c.formula.nodes.size() > options.endpoint.functions.max_formula_nodes
          || !work.spend(c.formula.nodes.size() + 3 * c.inputs.size() + 1)) {
        return exhausted();
      }
      if (c.latch != (i + 1 == e.cells.size()) || c.producers.size() != c.inputs.size()
          || c.inputs.size() > options.endpoint.gates.logical_inputs) {
        result.reason = "invalid logical endpoint cells";
        return result;
      }
      Frozen_cell cell;
      cell.kind          = c.latch ? Endpoint_kind::DominoLatch : Endpoint_kind::Domino;
      cell.phase         = c.phase;
      cell.state         = e.state_index;
      cell.name          = c.latch ? e.name : "";
      cell.formula       = c.formula;
      cell.metrics       = c.metrics;
      const auto metrics = cell.formula.metrics();
      if (!metrics || *metrics != cell.metrics || metrics->support != (1U << c.inputs.size()) - 1) {
        result.reason = "invalid logical endpoint formula";
        return result;
      }
      for (size_t j = 0; j < c.inputs.size(); ++j) {
        if (c.producers[j] == -1) {
          cell.inputs.push_back({Endpoint_ref::Space::native, c.inputs[j].id, c.inputs[j].inverted});
        } else if (c.producers[j] >= 0 && static_cast<size_t>(c.producers[j]) < i) {
          cell.inputs.push_back(mapped[c.producers[j]]);
        } else {
          result.reason = "invalid logical endpoint producer";
          return result;
        }
      }
      std::optional<std::vector<uint64_t>> key;
      if (!c.latch) {
        key = phase_cell_key(c, e.state_index, frozen.state[e.state_index].domain, work);
        if (!key) {
          if (work.exhausted) {
            return exhausted();
          }
          result.reason = "invalid phase-one cell sharing topology";
          return result;
        }
        if (!work.spend(key->size() * (1 + std::bit_width(phase_cells.size())))) {
          return exhausted();
        }
        if (const auto prior = phase_cells.find(*key); prior != phase_cells.end()) {
          auto ref      = prior->second;
          ref.inverted ^= c.formula.output_inverted;
          mapped.push_back(ref);
          continue;
        }
      }
      cell.rails    = rails(cell);
      auto function = formula_function(cell.formula, cell.inputs.size(), work);
      if (function.status != Status::feasible) {
        return exhausted();
      }
      cell.function = std::move(function.table);
      if (c.latch) {
        frozen.state[e.state_index].domino_latch = frozen.cells.size();
      }
      Endpoint_ref ref{Endpoint_ref::Space::cell, static_cast<uint32_t>(frozen.cells.size()), false};
      mapped.push_back(ref);
      if (key) {
        ref.inverted = c.formula.output_inverted;
        phase_cells.emplace(std::move(*key), ref);
      }
      frozen.cells.push_back(std::move(cell));
    }
  }
  auto expanded = expand_endpoint_netlist(frozen, work, options.max_nodes, options.endpoint.functions.max_formula_nodes);
  if (expanded.status != Status::feasible) {
    result.status = expanded.status;
    result.reason = std::move(expanded.reason);
    return result;
  }
  result.status  = Status::feasible;
  result.netlist = std::move(frozen);
  return result;
}

}  // namespace livehd::usyn
