// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "logical_region.hpp"

#include <algorithm>
#include <bit>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "joint_endpoint.hpp"

namespace livehd::usyn {
std::optional<std::vector<uint64_t>> phase_cell_key(const Logical_cell& cell, uint32_t state, uint32_t domain, Budget& work) {
  if (cell.latch || cell.phase != 1 || cell.inputs.size() != cell.producers.size()
      || !work.spend(4 + cell.inputs.size() + 3 * cell.formula.nodes.size())
      || std::any_of(cell.producers.begin(), cell.producers.end(), [](auto producer) { return producer != -1; })) {
    return {};
  }
  std::vector<uint64_t> key{domain, domain == unknown_clock_domain ? state : 0U, cell.phase, cell.inputs.size()};
  key.reserve(4 + cell.inputs.size() + 3 * cell.formula.nodes.size());
  for (const auto input : cell.inputs) {
    key.push_back((uint64_t{input.id} << 1) | input.inverted);
  }
  for (const auto& node : cell.formula.nodes) {
    key.push_back((uint64_t{static_cast<uint32_t>(node.kind)} << 1) | node.inverted);
    key.push_back((uint64_t{node.left} << 32) | node.right);
    key.push_back(node.variable);
  }
  return key;
}

namespace {

// Charges completed work without calling the guard again while unwinding.
using Slice = Work_slice;

bool valid_region(const Xag_region& r, Budget& work) {
  if (r.status != Status::feasible || r.inputs.size() + r.state.size() != r.graph.input_names().size()) {
    return false;
  }
  std::vector<bool> source(r.graph.input_names().size(), false);
  const auto        port = [&](Xsignal s) {
    if (!work.spend() || s.id >= r.graph.size() || s.inverted || r.graph.node(s.id).kind != Xag::Kind::source) {
      return false;
    }
    auto i = r.graph.node(s.id).source_index;
    if (i >= source.size() || source[i]) {
      return false;
    }
    source[i] = true;
    return true;
  };
  for (auto s : r.inputs) {
    if (!port(s)) {
      return false;
    }
  }
  for (const auto& s : r.state) {
    if (!port(s.q) || s.d.id >= r.graph.size() || (s.init != 'x' && s.init != '0' && s.init != '1')) {
      return false;
    }
  }
  for (const auto& o : r.outputs) {
    if (!work.spend() || o.signal.id >= r.graph.size()) {
      return false;
    }
  }
  return true;
}

Logical_endpoint bind(uint32_t state, const Endpoint_solution& s) {
  Logical_endpoint out;
  out.state_index = state;
  out.name        = s.name;
  out.whole_cone  = s.whole_cone;
  out.origin      = s.origin;
  for (const auto& c : s.cells) {
    Logical_cell cell;
    cell.formula   = c.function->formula;
    cell.metrics   = c.function->metrics;
    cell.producers = c.producers;
    cell.phase     = c.phase;
    cell.latch     = c.latch;
    for (auto id : c.function->inputs) {
      cell.inputs.push_back({id, false});
    }
    out.cells.push_back(std::move(cell));
  }
  return out;
}

// Enumerate actual residual consumers, never all original endpoint D cones.
// Both rails are retained here for accounting; residual optimization below
// deduplicates by positive signal and restores each consumer's polarity.
std::vector<Residual_output> roots(const Logical_region& r, std::optional<size_t> skip = {}, std::optional<size_t> skip_state = {},
                                   std::optional<size_t> skip_second = {}) {
  std::vector<bool> selected(r.logic.state.size(), false);
  for (const auto& e : r.endpoints) {
    selected[e.state_index] = true;
  }
  std::vector<Residual_output> out;
  for (const auto& po : r.logic.outputs) {
    out.push_back({po.signal, false});
  }
  for (size_t i = 0; i < selected.size(); ++i) {
    if (!selected[i] && skip_state != i) {
      out.push_back({r.logic.state[i].d, false});
    }
  }
  for (size_t ei = 0; ei < r.endpoints.size(); ++ei) {
    if (skip == ei || skip_second == ei) {
      continue;
    }
    for (const auto& c : r.endpoints[ei].cells) {
      for (size_t i = 0; i < c.inputs.size(); ++i) {
        if (c.producers[i] >= 0) {
          continue;
        }
        if (c.metrics.positive & (1U << i)) {
          out.push_back({c.inputs[i], true});
        }
        if (c.metrics.negative & (1U << i)) {
          out.push_back({~c.inputs[i], true});
        }
      }
    }
  }
  return out;
}

std::vector<Id> free_rails(const Logical_region& r) {
  std::vector<Id> out;
  for (const auto& e : r.endpoints) {
    out.push_back(r.logic.state[e.state_index].q.id);
  }
  return out;
}

std::optional<Logical_cost> cost(const Logical_region& r, const Logical_options& o, Budget& work, std::span<const uint32_t> domains,
                                 std::optional<Endpoint_pair> skip = {}) {
  Logical_cost c;
  if (!work.spend(r.logic.state.size() + r.logic.outputs.size() + r.endpoints.size())) {
    return {};
  }
  const auto                      rails = free_rails(r);
  const std::unordered_set<Id>    free(rails.begin(), rails.end());
  std::unordered_set<Id>          visited, inverted;
  std::set<std::vector<uint64_t>> phase_cells;
  for (size_t ei = 0; ei < r.endpoints.size(); ++ei) {
    if (skip && (ei == (*skip)[0] || ei == (*skip)[1])) {
      continue;
    }
    const auto& e = r.endpoints[ei];
    for (const auto& cell : e.cells) {
      if (!work.spend(cell.inputs.size() + 1)) {
        return {};
      }
      if (!cell.latch) {
        auto key = phase_cell_key(cell, e.state_index, domains.empty() ? unknown_clock_domain : domains[e.state_index], work);
        if (!key || !work.spend(key->size() * (1 + std::bit_width(phase_cells.size())))) {
          return {};
        }
        if (!phase_cells.insert(std::move(*key)).second) {
          continue;
        }
      }
      c.domino += uint64_t{cell.metrics.transistors} + (cell.latch ? o.endpoint.cost.domino_latch : o.endpoint.cost.domino);
    }
  }
  auto todo = skip ? roots(r, (*skip)[0], {}, (*skip)[1]) : roots(r);
  while (!todo.empty()) {
    if (!work.spend()) {
      return {};
    }
    const auto s = todo.back().signal;
    todo.pop_back();
    if (s.id == 0) {
      continue;
    }
    if (s.inverted && !free.contains(s.id)) {
      inverted.insert(s.id);
    }
    if (!visited.insert(s.id).second) {
      continue;
    }
    if (visited.size() > o.endpoint.cost_nodes) {
      return {};
    }
    const auto& n = r.logic.graph.node(s.id);
    if (n.kind == Xag::Kind::and_gate || n.kind == Xag::Kind::xor_gate) {
      c.static_logic += n.kind == Xag::Kind::and_gate ? o.endpoint.cost.static_and : o.endpoint.cost.static_xor;
      for (auto in : n.inputs) {
        todo.push_back({in, false});
      }
    }
  }
  c.inverters = uint64_t{inverted.size()} * o.endpoint.cost.static_not;
  return c;
}

bool expand(Logical_region& r, uint32_t max_nodes, Budget& work) {
  for (const auto& e : r.endpoints) {
    std::vector<Xsignal> cells;
    for (const auto& c : e.cells) {
      std::vector<Xsignal> values;
      for (const auto& n : c.formula.nodes) {
        if (!work.spend() || r.logic.graph.size() >= max_nodes) {
          return false;
        }
        Xsignal s;
        switch (n.kind) {
          case Gate_formula::Kind::constant: s = r.logic.graph.constant(n.inverted); break;
          case Gate_formula::Kind::literal:
            s = c.producers[n.variable] < 0 ? c.inputs[n.variable] : cells[c.producers[n.variable]];
            if (n.inverted) {
              s = ~s;
            }
            break;
          case Gate_formula::Kind::series  : s = r.logic.graph.land(values[n.left], values[n.right]); break;
          case Gate_formula::Kind::parallel: s = r.logic.graph.lor(values[n.left], values[n.right]); break;
        }
        values.push_back(s);
      }
      cells.push_back(c.formula.output_inverted ? ~values.back() : values.back());
    }
    r.logic.state[e.state_index].d = cells.back();
  }
  return true;
}

// Charge copies before allocation. Formula/table limits were checked during
// endpoint selection; there are no hidden per-node truth tables in this copy.
bool copy_budget(const Logical_region& r, Budget& work) {
  uint64_t size = r.logic.graph.size() + r.logic.inputs.size() + r.logic.state.size() + r.logic.outputs.size();
  for (const auto& e : r.endpoints) {
    for (const auto& c : e.cells) {
      size += c.formula.nodes.size() + c.inputs.size();
    }
  }
  return work.spend(size);
}

bool roots_budget(const Logical_region& r, Budget& work) {
  if (!work.spend(r.logic.state.size() + r.logic.outputs.size() + r.endpoints.size())) {
    return false;
  }
  for (const auto& e : r.endpoints) {
    if (!work.spend(e.cells.size())) {
      return false;
    }
    for (const auto& c : e.cells) {
      if (!work.spend(c.inputs.size())) {
        return false;
      }
    }
  }
  return true;
}

struct Residual_trial {
  std::optional<Logical_region> region;
  std::vector<bool>             affected;
  Residual_report               report;
  bool                          exhausted = false;
  std::string                   error;
};

Residual_trial residual_trial(const Logical_region& current, const Logical_options& o, Budget& work, bool optimize,
                              std::span<const uint32_t> domains) {
  Residual_trial trial;
  if (!copy_budget(current, work)) {
    trial.exhausted = true;
    return trial;
  }
  std::vector<Residual_output>   outputs;
  std::unordered_map<Id, size_t> index;
  for (auto output : roots(current)) {
    auto [it, fresh] = index.emplace(output.signal.id, outputs.size());
    if (fresh) {
      outputs.push_back({
          {output.signal.id, false},
          output.endpoint_input
      });
    } else {
      outputs[it->second].endpoint_input |= output.endpoint_input;
    }
  }
  auto ro          = o.residual;
  ro.max_nodes     = std::min(ro.max_nodes, o.max_nodes);
  ro.and_cost      = o.endpoint.cost.static_and;
  ro.xor_cost      = o.endpoint.cost.static_xor;
  ro.rewrite      &= optimize;
  ro.resubstitute &= optimize;
  auto optimized   = optimize_residual(current.logic.graph, outputs, ro, work);
  if (optimized.status == Status::invalid || optimized.status == Status::unsupported) {
    trial.error = optimized.reason;
    return trial;
  }
  trial.report    = std::move(optimized.report);
  trial.exhausted = trial.report.exhausted || optimized.status == Status::search_exhausted;
  if (!optimized.network) {
    return trial;
  }
  const auto& net    = *optimized.network;
  const auto  mapped = [&](Xsignal signal) {
    auto value = net.outputs[index.at(signal.id)].signal;
    return signal.inverted ? ~value : value;
  };
  Logical_region next;
  next.logic.graph  = std::move(optimized.network->graph);
  next.logic.status = Status::feasible;
  std::vector<Xsignal> sources(next.logic.graph.input_names().size());
  for (Id id = 1; id < next.logic.graph.size(); ++id) {
    if (!work.spend()) {
      trial.exhausted = true;
      return trial;
    }
    const auto& n = next.logic.graph.node(id);
    if (n.kind == Xag::Kind::source) {
      sources[n.source_index] = {id, false};
    }
  }
  const auto source = [&](Xsignal s) { return sources[current.logic.graph.node(s.id).source_index]; };
  for (auto in : current.logic.inputs) {
    next.logic.inputs.push_back(source(in));
  }
  for (const auto& state : current.logic.state) {
    next.logic.state.push_back({state.name, state.init, source(state.q), {}});
  }
  for (const auto& po : current.logic.outputs) {
    next.logic.outputs.push_back({po.name, mapped(po.signal)});
  }
  std::vector<bool> selected(current.logic.state.size(), false);
  next.endpoints = current.endpoints;
  for (auto& e : next.endpoints) {
    selected[e.state_index] = true;
    bool changed            = false;
    e.whole_cone            = true;
    for (auto& c : e.cells) {
      for (size_t i = 0; i < c.inputs.size(); ++i) {
        if (c.producers[i] >= 0) {
          c.inputs[i] = {};  // the producer, not this unused native slot, owns it
          continue;
        }
        changed         |= net.affected[index.at(c.inputs[i].id)];
        c.inputs[i]      = mapped(c.inputs[i]);
        const auto kind  = next.logic.graph.node(c.inputs[i].id).kind;
        e.whole_cone    &= kind == Xag::Kind::source || kind == Xag::Kind::constant;
      }
    }
    trial.affected.push_back(changed);
  }
  for (size_t i = 0; i < selected.size(); ++i) {
    if (!selected[i]) {
      next.logic.state[i].d = mapped(current.logic.state[i].d);
    }
  }
  if (!expand(next, o.max_nodes, work)) {
    trial.exhausted = true;
    return trial;
  }
  const auto estimate = cost(next, o, work, domains);
  if (!estimate) {
    trial.exhausted = true;
    return trial;
  }
  next.cost    = *estimate;
  trial.region = std::move(next);
  return trial;
}

std::optional<std::vector<Pair_endpoint>> pair_endpoints(const Logical_region& current, std::span<const uint32_t> domains,
                                                         Budget& work, bool complete_functions = false) {
  std::vector<Pair_endpoint> endpoints;
  for (const auto& e : current.endpoints) {
    if (!work.spend(e.cells.size() + 1)) {
      return {};
    }
    Pair_endpoint endpoint;
    endpoint.domain = domains[e.state_index];
    if (complete_functions) {
      endpoint.inputs.push_back(current.logic.state[e.state_index].d);
      endpoints.push_back(std::move(endpoint));
      continue;
    }
    for (const auto& c : e.cells) {
      if (!work.spend(2 * c.inputs.size())) {
        return {};
      }
      for (size_t i = 0; i < c.inputs.size(); ++i) {
        if (c.producers[i] >= 0) {
          continue;
        }
        if (c.metrics.positive & (1U << i)) {
          endpoint.inputs.push_back(c.inputs[i]);
        }
        if (c.metrics.negative & (1U << i)) {
          endpoint.inputs.push_back(~c.inputs[i]);
        }
      }
    }
    endpoints.push_back(std::move(endpoint));
  }
  return endpoints;
}

// Sixty-four deterministic source assignments can prove nonconstancy cheaply.
// Uniform samples are inconclusive: use a zero-transistor lower bound then,
// never infer a constant function or skip a potentially profitable pair.
std::optional<bool> nonconstant_witness(const Xag& graph, const Xag_window& window, Budget& work) {
  if (!window.whole_cone) {
    return false;  // independent internal cut values may be unreachable in the source graph
  }
  if (!work.spend(3 * (window.leaves.size() + window.interior.size()) + 1)) {
    return {};
  }
  std::unordered_map<Id, uint64_t> values;
  values.reserve(window.leaves.size() + window.interior.size());
  constexpr std::array<uint64_t, 6> patterns{0xaaaaaaaaaaaaaaaaULL,
                                             0xccccccccccccccccULL,
                                             0xf0f0f0f0f0f0f0f0ULL,
                                             0xff00ff00ff00ff00ULL,
                                             0xffff0000ffff0000ULL,
                                             0xffffffff00000000ULL};
  for (size_t i = 0; i < window.leaves.size(); ++i) {
    // Extra columns need not enumerate all combinations. Every lane still
    // represents one valid assignment of independent whole-cone sources.
    uint64_t pattern = i < patterns.size() ? patterns[i] : 0x9e3779b97f4a7c15ULL * (i + 1);
    if (i >= patterns.size()) {
      pattern ^= pattern >> 30;
      pattern *= 0xbf58476d1ce4e5b9ULL;
      pattern ^= pattern >> 27;
    }
    values.emplace(window.leaves[i], pattern);
  }
  const auto read = [&](Xsignal signal) {
    const uint64_t value = signal.id ? values.at(signal.id) : 0;
    return signal.inverted ? ~value : value;
  };
  for (const auto id : window.interior) {
    const auto& node = graph.node(id);
    const auto  a = read(node.inputs[0]), b = read(node.inputs[1]);
    values.emplace(id, node.kind == Xag::Kind::and_gate ? a & b : a ^ b);
  }
  const auto observed = read(window.root);
  return observed != 0 && observed != std::numeric_limits<uint64_t>::max();
}

enum class Pair_mode { individual, fanout_free, joint, joint_care, joint_recode };

void pair_sweep(Logical_region& current, const Logical_options& o, std::span<const uint32_t> domains, Pair_report& report,
                Budget& work, Pair_mode mode = Pair_mode::individual) {
  const bool fanout_free = mode == Pair_mode::fanout_free, joint_care = mode == Pair_mode::joint_care;
  const bool joint_recode = mode == Pair_mode::joint_recode;
  const bool joint        = mode == Pair_mode::joint || joint_care || joint_recode;
  // Ordinary reselection targets paid residual sharing. Joint synthesis also
  // searches fully absorbed functions for a profitable shared first-phase cell.
  if (current.endpoints.size() < 2 || domains.empty() || !o.pair_candidates || (!joint && !current.cost.static_logic)) {
    return;
  }
  auto endpoints = pair_endpoints(current, domains, work, joint);
  if (!endpoints) {
    report.exhausted = true;
    return;
  }
  const auto initial_trials = report.trials;
  if (initial_trials >= o.pair_trials) {
    report.exhausted = true;
    return;
  }
  Pair_queue queue(o.pair_candidates, o.pair_trials - initial_trials);
  const auto initial_requeues = report.requeues, initial_stale = report.stale_skips;
  const auto refresh = [&](std::span<const uint8_t> affected) {
    ++report.refreshes;
    const auto found           = find_endpoint_pairs(current.logic.graph, *endpoints, o.pair_candidates, o.max_nodes, work, joint);
    report.candidates         += found.pairs.size();
    report.shared_nodes       += found.shared_nodes;
    report.more_than_two      += found.more_than_two;
    report.domain_skips       += found.domain_skips;
    report.joint_source_pairs += found.source_pairs;
    report.exhausted          |= found.status != Status::feasible || found.capped;
    if (found.status != Status::feasible) {
      return false;
    }
    const auto status   = queue.refresh(found.pairs, affected, work);
    report.requeues     = initial_requeues + queue.report().requeues;
    report.stale_skips  = initial_stale + queue.report().stale_skips;
    report.exhausted   |= status != Status::feasible;
    return status == Status::feasible;
  };
  if (!refresh({})) {
    return;
  }
  while (true) {
    const auto pending  = queue.pop(work);
    report.trials       = initial_trials + queue.report().trials;
    report.exhausted   |= queue.report().exhausted;
    if (!pending) {
      break;
    }
    const auto&               pair = *pending;
    std::set<Id>              basis;
    std::vector<Id>           analysis_boundary;
    bool                      admitted = true;
    uint64_t                  nodes    = 0;
    std::array<Xag_window, 2> windows;
    std::vector<Xsignal>      protected_roots;
    if (fanout_free) {
      if (!roots_budget(current, work)) {
        report.exhausted = true;
        break;
      }
      for (const auto& output : roots(current, pair[0], {}, pair[1])) {
        protected_roots.push_back(output.signal);
      }
      Pair_windows bounded;
      {
        Slice admission(work, o.endpoint.candidate_work, 2);
        bounded = fanout_free_pair_windows(current.logic.graph,
                                           {current.logic.state[current.endpoints[pair[0]].state_index].d,
                                            current.logic.state[current.endpoints[pair[1]].state_index].d},
                                           protected_roots,
                                           o.endpoint.window,
                                           o.pair_inputs,
                                           o.max_nodes,
                                           admission.work);
      }
      report.exhausted |= bounded.exhausted;
      // With no internal outside ports this merely repeats a whole-cone trial.
      if (bounded.status != Status::feasible || bounded.outside_boundary.empty() || work.exhausted) {
        ++report.fanout_skips;
        continue;
      }
      windows           = std::move(bounded.windows);
      analysis_boundary = std::move(bounded.basis);
      ++report.fanout_windows;
      report.fanout_ports += bounded.outside_boundary.size();
    }
    for (size_t i = 0; !fanout_free && i < pair.size(); ++i) {
      const auto ei      = pair[i];
      const auto si      = current.endpoints[ei].state_index;
      auto       limits  = o.endpoint.window;
      limits.inputs      = std::min(limits.inputs, o.pair_inputs);
      windows[i]         = whole_cone(current.logic.graph, current.logic.state[si].d, limits, work);
      const auto& window = windows[i];
      if (window.status != Status::feasible) {
        report.exhausted |= work.exhausted;
        ++report.window_skips;
        admitted = false;
        break;
      }
      if (!work.spend(window.leaves.size())) {
        report.exhausted = true;
        admitted         = false;
        break;
      }
      basis.insert(window.leaves.begin(), window.leaves.end());
      nodes += window.interior.size();
      if (basis.size() > o.pair_inputs) {
        ++report.support_skips;
        admitted = false;
        break;
      }
      if (nodes > o.endpoint.window.nodes) {
        ++report.window_skips;
        admitted = false;
        break;
      }
    }
    if (!fanout_free && !admitted && !work.exhausted) {
      Pair_windows bounded;
      {
        Slice admission(work, o.endpoint.candidate_work, 2);
        bounded = grow_pair_windows(current.logic.graph,
                                    {current.logic.state[current.endpoints[pair[0]].state_index].d,
                                     current.logic.state[current.endpoints[pair[1]].state_index].d},
                                    o.endpoint.window,
                                    o.pair_inputs,
                                    admission.work);
      }
      report.exhausted |= bounded.exhausted;
      if (bounded.status == Status::feasible && !work.exhausted) {
        windows           = std::move(bounded.windows);
        analysis_boundary = std::move(bounded.basis);
        admitted          = true;
        ++report.bounded_windows;
      }
    }
    if (!admitted) {
      continue;
    }
    if (!fanout_free && !roots_budget(current, work)) {
      report.exhausted = true;
      break;
    }
    // Reselection leaves all outside readers and other endpoints unchanged.
    // Their ledger is an unavoidable cost, including shared demanded NOTs.
    // Give each changed endpoint only the mandatory latch and, when witnessed
    // nonconstant, one transistor. New static gates and inversions have a zero
    // lower bound. This deliberately optimistic estimate cannot hide a gain.
    const auto fixed = cost(current, o, work, domains, pair);
    if (!fixed) {
      report.exhausted = true;
      break;
    }
    uint64_t lower_bound = fixed->total() + 2 * uint64_t{o.endpoint.cost.domino_latch};
    if (mode == Pair_mode::joint) {
      // Every accepted joint encoding uses a nonconstant first-phase producer
      // in both endpoints. Charge its minimum cost unless an unchanged cell in
      // this clock domain might already provide it for free. Do not assume a
      // newly synthesized function differs from every outside implementation.
      bool paid = false;
      for (size_t ei = 0; ei < current.endpoints.size() && !paid; ++ei) {
        const auto& endpoint = current.endpoints[ei];
        if (!work.spend(endpoint.cells.size() + 1)) {
          report.exhausted = true;
          admitted         = false;
          break;
        }
        if (ei != pair[0] && ei != pair[1] && domains[endpoint.state_index] == (*endpoints)[pair[0]].domain) {
          paid = std::any_of(endpoint.cells.begin(), endpoint.cells.end(), [](const auto& cell) { return !cell.latch; });
        }
      }
      if (!admitted) {
        break;
      }
      if (!paid) {
        lower_bound += uint64_t{o.endpoint.cost.domino} + 1;
      }
    }
    for (const auto& window : windows) {
      const auto varying = nonconstant_witness(current.logic.graph, window, work);
      if (!varying) {
        report.exhausted = true;
        admitted         = false;
        break;
      }
      lower_bound += *varying;
    }
    if (!admitted) {
      break;
    }
    if (lower_bound >= current.cost.total()) {
      ++report.gain_skips;
      continue;
    }
    ++report.attempts;
    if (!fanout_free) {
      for (const auto& output : roots(current, pair[0], {}, pair[1])) {
        protected_roots.push_back(output.signal);
      }
    }
    const auto                                    rails = free_rails(current);
    std::array<std::vector<Endpoint_solution>, 2> solutions;
    auto                                          options = o.endpoint;
    // The pair ledger may reward an individually worse interface. Collect
    // alternatives through the existing bounded boundary/divisor searches.
    if (o.pair_choices) {
      options.fast_accept = false;
    }
    const auto same = [](const Endpoint_solution& a, const Endpoint_solution& b) {
      if (a.cells.size() != b.cells.size()) {
        return false;
      }
      for (size_t i = 0; i < a.cells.size(); ++i) {
        if (a.cells[i].function != b.cells[i].function || a.cells[i].producers != b.cells[i].producers) {
          return false;
        }
      }
      return true;
    };
    for (size_t i = 0; !joint && i < pair.size(); ++i) {
      const auto      si = current.endpoints[pair[i]].state_index;
      Endpoint_result candidate;
      {
        Slice search(work, o.endpoint_work, 3 - i);
        candidate = select_endpoint(current.logic.graph,
                                    current.logic.state[si].d,
                                    current.logic.state[si].name,
                                    protected_roots,
                                    rails,
                                    options,
                                    search.work,
                                    analysis_boundary,
                                    o.pair_choices);
      }
      Slice validation(work, o.endpoint_work);
      report.exhausted |= candidate.report.exhausted;
      if (!candidate.selected || !validate_endpoint(current.logic.graph, *candidate.selected, options, validation.work)) {
        report.exhausted = true;
        break;
      }
      solutions[i].push_back(std::move(*candidate.selected));
      for (auto& choice : candidate.choices) {
        if (same(choice, solutions[i].front())) {
          continue;
        }
        if (!validate_endpoint(current.logic.graph, choice, options, validation.work)) {
          report.exhausted = true;
          break;
        }
        solutions[i].push_back(std::move(choice));
        ++report.choices;
      }
    }
    if (!joint && solutions[0].empty() && solutions[1].empty()) {
      continue;
    }
    // Every branch uses the same snapshot and full regional cost ledger. Keep
    // the original three old/best combinations first, then cross the remaining
    // bounded interface pools. No independently losing choice is published.
    std::optional<Logical_region> best;
    const auto                    trial = [&](size_t left, size_t right, uint64_t& combinations) {
      if (left > solutions[0].size() || right > solutions[1].size()) {
        return true;
      }
      if (!copy_budget(current, work)) {
        report.exhausted = true;
        return false;
      }
      ++combinations;
      auto             next = current;
      const std::array indices{left, right};
      for (size_t i = 0; i < pair.size(); ++i) {
        if (!indices[i]) {
          continue;
        }
        const auto ei             = pair[i];
        next.endpoints[ei]        = bind(current.endpoints[ei].state_index, solutions[i][indices[i] - 1]);
        next.endpoints[ei].origin = (fanout_free ? "pair-fanout-" : "pair-") + next.endpoints[ei].origin;
      }
      if (!expand(next, o.max_nodes, work)) {
        report.exhausted = true;
        return false;
      }
      const auto estimate = cost(next, o, work, domains);
      if (!estimate) {
        report.exhausted = true;
        return false;
      }
      if (estimate->total() < (best ? best->cost.total() : current.cost.total())) {
        next.cost = *estimate;
        best      = std::move(next);
      }
      return true;
    };
    if (joint) {
      Pair_windows window;
      window.status   = Status::feasible;
      window.windows  = std::move(windows);
      window.basis    = analysis_boundary.empty() ? std::vector<Id>(basis.begin(), basis.end()) : std::move(analysis_boundary);
      uint32_t common = 0;
      for (uint32_t i = 0; i < window.basis.size(); ++i) {
        const auto id = window.basis[i];
        if (std::binary_search(window.windows[0].leaves.begin(), window.windows[0].leaves.end(), id)
            && std::binary_search(window.windows[1].leaves.begin(), window.windows[1].leaves.end(), id)) {
          common |= 1U << i;
        }
      }
      if (window.basis.size() < 3 || std::popcount(common) < 2) {
        continue;
      }
      std::array<Truth_table, 2> functions;
      bool                       analyzed = true;
      {
        Slice analysis(work, o.endpoint.candidate_work, 2);
        for (size_t i = 0; i < functions.size(); ++i) {
          const auto      compact = window_function(current.logic.graph, window.windows[i], analysis.work);
          Window_function lifted;
          if (compact.status == Status::feasible) {
            lifted = lift_function(compact.table, window.windows[i].leaves, window.basis, analysis.work);
          }
          if (lifted.status != Status::feasible) {
            analyzed         = false;
            report.exhausted = true;
            break;
          }
          functions[i] = std::move(lifted.table);
        }
      }
      if (!analyzed || work.exhausted) {
        continue;
      }
      ++(joint_recode ? report.joint_recode_windows : joint_care ? report.joint_care_windows : report.joint_windows);
      uint32_t         partitions = 0;
      const std::array names{current.endpoints[pair[0]].name, current.endpoints[pair[1]].name};
      for (uint32_t mask = 1; mask < (1U << window.basis.size()) - 1; ++mask) {
        if (!work.spend()) {
          report.exhausted = true;
          break;
        }
        const auto inputs = static_cast<uint32_t>(std::popcount(mask));
        if ((mask & common) != mask || inputs < 2 || inputs > o.endpoint.gates.logical_inputs) {
          continue;
        }
        if (partitions == o.endpoint.divisor_partitions) {
          report.exhausted = true;
          break;
        }
        ++partitions;
        ++(joint_recode ? report.joint_recode_partitions : joint_care ? report.joint_care_partitions : report.joint_partitions);
        if (joint_care || joint_recode) {
          uint32_t bits      = inputs;
          uint32_t encodings = joint_recode ? std::min(o.pair_choices, bits * (bits - 1)) : 1;
          bool     available = true;
          for (uint32_t encoding = 0; available && encoding < encodings; ++encoding) {
            // The previous pool has been fully priced. Release it before
            // admitting another boundary_bytes-sized generation workspace.
            solutions = {};
            Joint_endpoint_choices candidates;
            {
              Slice generation(work, o.endpoint.candidate_work, 2);
              if (joint_recode) {
                Joint_code_change change{encoding / (bits - 1), encoding % (bits - 1)};
                change.source += change.source >= change.target;
                ++report.joint_recode_encodings;
                candidates        = recode_joint_endpoints(current.logic.graph,
                                                           window,
                                                           functions,
                                                           names,
                                                           mask,
                                                           change,
                                                           o.endpoint,
                                                           o.pair_choices,
                                                           generation.work);
                // The first proposal (bit 0 XOR bit 1) is independent of the
                // unknown width. Later proposals use the actual cofactor code,
                // which may be narrower than the original bound-variable set.
                bits              = candidates.code_bits;
                encodings         = bits < 2 ? 0 : std::min(o.pair_choices, bits * (bits - 1));
                report.exhausted |= bits > 1 && bits * (bits - 1) > o.pair_choices;
              } else {
                candidates = complete_joint_endpoints(current.logic.graph,
                                                      window,
                                                      functions,
                                                      names,
                                                      mask,
                                                      o.endpoint,
                                                      o.pair_choices,
                                                      generation.work);
              }
            }
            report.exhausted |= candidates.exhausted;
            if (joint_recode) {
              report.joint_recode_phases   += candidates.phases;
              report.joint_recode_attempts += candidates.attempts;
              report.joint_recode_retained += candidates.retained;
              report.joint_recode_bytes     = std::max(report.joint_recode_bytes, candidates.bytes);
            } else {
              report.joint_care_phases   += candidates.phases;
              report.joint_care_attempts += candidates.attempts;
              report.joint_care_retained += candidates.retained;
              report.joint_care_bytes     = std::max(report.joint_care_bytes, candidates.bytes);
            }
            if (work.exhausted) {
              available = false;
              break;
            }
            solutions = std::move(candidates.endpoints);
            for (size_t left = 0; available && left <= solutions[0].size(); ++left) {
              for (size_t right = 0; available && right <= solutions[1].size(); ++right) {
                // Baseline zero/old-only combinations were already handled.
                // A new code can improve even a zero-filled top or full image.
                if ((joint_recode && (left || right)) || (left && solutions[0][left - 1].origin == "joint-care")
                    || (right && solutions[1][right - 1].origin == "joint-care")) {
                  available = trial(left, right, joint_recode ? report.joint_recode_combinations : report.joint_care_combinations);
                }
              }
            }
          }
          if (!available) {
            break;
          }
          continue;
        }
        solutions = {};
        Joint_endpoint_result candidate;
        {
          Slice generation(work, o.endpoint.candidate_work, 2);
          candidate = synthesize_joint_endpoints(current.logic.graph, window, functions, names, mask, o.endpoint, generation.work);
        }
        report.exhausted |= candidate.exhausted;
        if (!candidate.endpoints || work.exhausted) {
          continue;
        }
        ++report.joint_candidates;
        report.joint_divisors += candidate.shared_divisors;
        for (size_t i = 0; i < solutions.size(); ++i) {
          solutions[i].clear();
          solutions[i].push_back(std::move((*candidate.endpoints)[i]));
        }
        if (!trial(1, 1, report.joint_combinations) || !trial(1, 0, report.joint_combinations)
            || !trial(0, 1, report.joint_combinations)) {
          break;
        }
      }
    } else {
      bool available = trial(1, 1, report.combinations) && trial(1, 0, report.combinations) && trial(0, 1, report.combinations);
      for (size_t left = 0; available && left <= solutions[0].size(); ++left) {
        for (size_t right = 0; available && right <= solutions[1].size(); ++right) {
          if (left > 1 || right > 1) {
            available = trial(left, right, report.choice_combinations);
          }
        }
      }
    }
    if (best) {
      // Publish only the best fully priced combination. A later trial's refusal
      // cannot erase a legal improvement already constructed on this snapshot.
      current = std::move(*best);
      ++report.wins;
      report.fanout_wins       += fanout_free;
      report.joint_wins        += mode == Pair_mode::joint;
      report.joint_care_wins   += joint_care;
      report.joint_recode_wins += joint_recode;
      auto updated              = pair_endpoints(current, domains, work, joint);
      if (!updated) {
        report.exhausted = true;
        break;
      }
      std::vector<Xsignal> changed_inputs;
      for (const auto ei : pair) {
        const auto& old_inputs = (*endpoints)[ei].inputs;
        const auto& new_inputs = (*updated)[ei].inputs;
        if (!work.spend(old_inputs.size() + new_inputs.size())) {
          report.exhausted = true;
          return;
        }
        changed_inputs.insert(changed_inputs.end(), old_inputs.begin(), old_inputs.end());
        changed_inputs.insert(changed_inputs.end(), new_inputs.begin(), new_inputs.end());
      }
      auto affected = pair_dependents(current.logic.graph, changed_inputs, *updated, o.max_nodes, work);
      if (affected.status != Status::feasible) {
        report.exhausted = true;
        break;
      }
      affected.endpoints[pair[0]] = affected.endpoints[pair[1]] = true;
      endpoints                                                 = std::move(updated);
      if (!refresh(affected.endpoints)) {
        break;
      }
    }
  }
}

void optimize_pairs(Logical_region& current, const Logical_options& options, std::span<const uint32_t> domains, Pair_report& report,
                    Budget& work) {
  auto baseline         = options;
  baseline.pair_choices = 0;
  pair_sweep(current, baseline, domains, report, work);
  // Preserve the complete greedy trajectory before exploring more interface
  // choices: an immediate local gain can change later available graph cuts.
  // Both sweeps share one finite trial/work budget and one legal incumbent.
  if (options.pair_choices && report.attempts && !work.exhausted && report.trials < options.pair_trials) {
    pair_sweep(current, options, domains, report, work);
  }
  // Preserve both existing trajectories before trying boundaries derived from
  // live outside fanout. These choices still share the original trial/work cap
  // and can only improve the fully priced incumbent.
  if (options.pair_choices && report.attempts && !work.exhausted && report.trials < options.pair_trials) {
    pair_sweep(current, options, domains, report, work, Pair_mode::fanout_free);
  }
  if (options.pair_choices && options.endpoint.clock_phases == 2 && options.endpoint.divisor_partitions && !work.exhausted
      && report.trials < options.pair_trials) {
    pair_sweep(current, options, domains, report, work, Pair_mode::joint);
  }
  if (options.pair_choices && options.endpoint.clock_phases == 2 && options.endpoint.divisor_partitions
      && options.endpoint.care_phases && !work.exhausted && report.trials < options.pair_trials) {
    pair_sweep(current, options, domains, report, work, Pair_mode::joint_care);
  }
  if (options.pair_choices && options.endpoint.clock_phases == 2 && options.endpoint.divisor_partitions && !work.exhausted
      && report.trials < options.pair_trials) {
    pair_sweep(current, options, domains, report, work, Pair_mode::joint_recode);
  }
}

}  // namespace

Logical_result synthesize_logical_region(const Xag_region& source, std::span<const uint32_t> eligible, const Logical_options& o,
                                         Budget& structural, Budget& work, std::span<const uint32_t> domains) {
  Logical_result result;
  // Stage charges are consumed deltas: admission on the structural ledger,
  // every later stage on the search ledger `work` (possibly the same budget).
  auto*          stage      = &result.report.work.admission;
  Budget*        ledger     = &structural;
  auto           checkpoint = structural.consumed;
  const auto     charge     = [&](uint64_t& next, Budget& to) {
    *stage     += ledger->consumed - checkpoint;
    ledger      = &to;
    checkpoint  = to.consumed;
    stage       = &next;
  };
  const auto finish = [&] {
    charge(*stage, *ledger);
    return std::move(result);
  };
  if (!valid_endpoint_options(o.endpoint) || !valid_residual_options(o.residual) || !o.max_nodes || !o.endpoint_work
      || eligible.size() > source.state.size() || (!domains.empty() && domains.size() != source.state.size())
      || o.pair_candidates > 4096 || o.pair_choices > 8 || !o.pair_trials || o.pair_trials > 4096 || o.pair_inputs == 0
      || o.pair_inputs > max_logical_inputs || !o.pair_work) {
    result.reason = "invalid logical-region options or eligibility";
    return finish();
  }
  // Only the structural allowance refuses a region.
  const auto refused = [&] {
    result.report.exhausted = true;
    result.status           = Status::search_exhausted;
    result.reason           = "logical synthesis admission exhausted";
    return finish();
  };
  if (source.graph.size() > o.max_nodes || source.inputs.size() > o.max_nodes || source.outputs.size() > o.max_nodes
      || source.state.size() > o.max_nodes || !structural.spend(source.graph.size() + eligible.size())) {
    return refused();
  }
  if (!valid_region(source, structural)) {
    if (structural.exhausted) {
      return refused();
    }
    result.reason = "invalid logical-region boundaries";
    return finish();
  }
  std::vector<bool> selected(source.state.size(), false);
  std::vector<Id>   rails;
  for (auto i : eligible) {
    if (i >= selected.size() || selected[i]) {
      result.reason = "invalid or duplicate eligible state index";
      return finish();
    }
    selected[i] = true;
    rails.push_back(source.state[i].q.id);
  }
  const auto copy_source = [&] {
    Logical_region r;
    r.logic.graph   = source.graph;
    r.logic.inputs  = source.inputs;
    r.logic.state   = source.state;
    r.logic.outputs = source.outputs;
    r.logic.status  = Status::feasible;
    return r;
  };
  // Structural work after the admission stage (pricing or rebuilding a
  // selection the search could not price) is still charged to that stage, even
  // when one budget is both ledgers.
  const auto structurally = [&](const auto& step) {
    const auto before = structural.consumed;
    auto       done   = step();
    const auto spent  = structural.consumed - before;

    result.report.work.admission += spent;
    if (ledger == &structural) {
      checkpoint += spent;
    }
    return done;
  };
  auto current = copy_source();
  // The minimal legal selection, built and priced on the structural ledger
  // before any search: each endpoint's identity keeps its whole D cone static.
  // Expanding it leaves the network unchanged (each identity cell is its D
  // signal), so only its bindings and cost are kept.
  for (const auto si : eligible) {
    const auto identity = identity_endpoint(current.logic.graph, source.state[si].d, source.state[si].name, o.endpoint);
    if (!validate_endpoint(current.logic.graph, identity, o.endpoint, structural)) {
      if (structural.exhausted) {
        return refused();
      }
      result.reason = "invalid identity endpoint";
      return finish();
    }
    current.endpoints.push_back(bind(si, identity));
  }
  if (!expand(current, o.max_nodes, structural)) {
    return refused();
  }
  const auto minimal = cost(current, o, structural, domains);
  if (!minimal) {
    return refused();
  }
  const auto identities = std::move(current.endpoints);
  current.endpoints.clear();
  charge(result.report.work.selection, work);
  // Endpoints whose search cannot finish keep their identity (`fell_back`, by
  // endpoint index); the rest of the region still searches. `searched` counts
  // the endpoints the search chose.
  std::vector<bool> fell_back(eligible.size(), false);
  size_t            searched = 0;
  for (size_t i = 0; i < eligible.size(); ++i) {
    const auto si = eligible[i];
    if (!work.spend()) {
      // No search credits left: every remaining endpoint keeps its identity.
      for (; i < eligible.size(); ++i) {
        Endpoint_report starved;
        starved.exhausted = true;
        starved.limits.emplace_back("endpoint search credits");
        result.report.initial.push_back(std::move(starved));
        current.endpoints.push_back(identities[i]);
        fell_back[i] = true;
      }
      result.report.exhausted = true;
      break;
    }
    std::vector<Xsignal> protected_roots;
    // Already selected endpoints contribute their real static input cones;
    // not-yet-selected endpoints still protect their complete next-state cone.
    for (const auto& output : roots(current, {}, si)) {
      protected_roots.push_back(output.signal);
    }
    Endpoint_result found;
    bool            valid = false;
    {
      Slice search(work, o.endpoint_work, eligible.size() - i + 2);
      found = select_endpoint(current.logic.graph,
                              source.state[si].d,
                              source.state[si].name,
                              protected_roots,
                              rails,
                              o.endpoint,
                              search.work);
    }  // Charge search before validating its retained incumbent.
    {
      Slice validation(work, o.endpoint_work);
      valid = found.selected && validate_endpoint(current.logic.graph, *found.selected, o.endpoint, validation.work);
    }
    result.report.initial.push_back(found.report);
    result.report.exhausted |= found.report.exhausted;
    if (!valid) {
      current.endpoints.push_back(identities[i]);
      fell_back[i]            = true;
      result.report.exhausted = true;
      continue;
    }
    current.endpoints.push_back(bind(si, *found.selected));
    ++searched;
  }
  std::optional<Logical_cost> initial;
  // With no searched endpoint the selection is the identity baseline, whose
  // network is the unchanged source: it is already priced.
  if (searched) {
    const auto expanded = expand(current, o.max_nodes, work);
    if (expanded) {
      initial = cost(current, o, work, domains);
    }
    if (!initial
        && std::all_of(current.endpoints.begin(), current.endpoints.end(), [](const auto& e) { return e.origin == "identity"; })) {
      // Every searched endpoint chose its identity, so this is the baseline
      // (identity expansions append no node): it is already priced.
      initial = minimal;
    } else if (!initial && work.exhausted) {
      // The search stopped while pricing its selection. Price it on the
      // structural ledger instead of dropping its searched endpoints; the
      // charge depends only on the region and its search credits. Expansion
      // is idempotent (structural hashing), so a partial one resumes.
      initial = structurally([&]() -> std::optional<Logical_cost> {
        if (!expanded && !expand(current, o.max_nodes, structural)) {
          return {};
        }
        return cost(current, o, structural, domains);
      });
      if (structural.exhausted) {
        return refused();
      }
    }
  }
  if (initial) {
    current.cost = *initial;
  } else {
    if (searched) {
      // The selection exceeds the node limits: publish the identity baseline,
      // rebuilt from the source (a failed expansion may have appended nodes).
      // Like any copy, the rebuild is charged before allocation, here to the
      // structural ledger (the first copy is part of the admission charge).
      uint64_t size = source.graph.size() + source.inputs.size() + source.state.size() + source.outputs.size();
      for (const auto& e : identities) {
        for (const auto& c : e.cells) {
          size += c.formula.nodes.size() + c.inputs.size();
        }
      }
      if (!structurally([&] { return structural.spend(size); })) {
        return refused();
      }
      current           = copy_source();
      current.endpoints = identities;
      fell_back.assign(eligible.size(), true);
      result.report.exhausted = true;
    }
    current.cost = *minimal;
  }
  result.report.before = result.report.after_pairs = result.report.after_residual = result.report.after = current.cost;
  result.region                                                                                         = std::move(current);
  charge(result.report.work.pairs, work);
  {
    Slice slice(work, o.pair_work, 4);
    optimize_pairs(*result.region, o, domains, result.report.pairs, slice.work);
    result.report.pairs.work   = slice.work.consumed;
    result.report.exhausted   |= result.report.pairs.exhausted;
    result.report.after_pairs = result.report.after_residual = result.region->cost;
  }
  charge(result.report.work.residual, work);
  if (o.optimize_residual) {
    Residual_trial trial;
    {
      Slice slice(work, unlimited_work, 2);
      trial = residual_trial(*result.region, o, slice.work, true, domains);
    }
    result.report.residual   = trial.report;
    result.report.exhausted |= trial.exhausted;
    if (!trial.error.empty()) {
      result.region.reset();
      result.reason = trial.error;
      return finish();
    }
    if (trial.region && trial.region->cost.total() <= result.region->cost.total()) {
      result.region                   = std::move(trial.region);
      result.report.residual_accepted = true;
      result.report.after_residual    = result.region->cost;
    } else {
      // A residual AND/XOR win can lose after explicit inversion accounting.
      // The current implementation remains intact; no binding is half-rebased.
      trial.affected.assign(result.region->endpoints.size(), false);
    }
    if (o.feedback && std::any_of(trial.affected.begin(), trial.affected.end(), [](bool b) { return b; })) {
      charge(result.report.work.feedback, work);
      result.report.feedback_rounds = 1;
      for (size_t ei = 0; ei < trial.affected.size(); ++ei) {
        if (!trial.affected[ei]) {
          continue;
        }
        if (!work.spend()) {
          result.report.exhausted = true;
          break;
        }
        ++result.report.feedback_attempts;
        const auto&          baseline = *result.region;
        const auto           si       = baseline.endpoints[ei].state_index;
        std::vector<Xsignal> protected_roots;
        for (auto output : roots(baseline, ei)) {
          protected_roots.push_back(output.signal);
        }
        Endpoint_result candidate;
        {
          Slice search(work, o.endpoint_work, trial.affected.size() - ei + 2);
          candidate = select_endpoint(baseline.logic.graph,
                                      baseline.logic.state[si].d,
                                      baseline.logic.state[si].name,
                                      protected_roots,
                                      free_rails(baseline),
                                      o.endpoint,
                                      search.work);
        }
        Slice slice(work, o.endpoint_work);
        result.report.exhausted |= candidate.report.exhausted;
        if (!candidate.selected || !validate_endpoint(baseline.logic.graph, *candidate.selected, o.endpoint, slice.work)
            || !copy_budget(baseline, slice.work)) {
          result.report.exhausted = true;
          continue;
        }
        auto next          = baseline;
        next.endpoints[ei] = bind(si, *candidate.selected);
        if (!expand(next, o.max_nodes, slice.work)) {
          result.report.exhausted = true;
          continue;
        }
        auto estimate = cost(next, o, slice.work, domains);
        if (!estimate) {
          result.report.exhausted = true;
          continue;
        }
        if (estimate->total() < baseline.cost.total()) {
          next.cost     = *estimate;
          result.region = std::move(next);
          ++result.report.feedback_wins;
        }
      }
      // Refresh live residuals and compact after changing endpoint boundaries.
      // This is cleanup only, never a second rewrite/resubstitution sweep.
      charge(result.report.work.cleanup, work);
      auto cleanup             = residual_trial(*result.region, o, work, false, domains);
      result.report.exhausted |= cleanup.exhausted;
      if (!cleanup.error.empty()) {
        result.region.reset();
        result.reason = cleanup.error;
        return finish();
      }
      if (cleanup.region && cleanup.region->cost.total() <= result.region->cost.total()) {
        result.region = std::move(cleanup.region);
      }
    }
  }
  // Count the fallbacks the region still publishes as their identity: a later
  // pair or feedback win may replace one.
  for (size_t ei = 0; ei < fell_back.size(); ++ei) {
    result.report.identity_fallbacks += fell_back[ei] && result.region->endpoints[ei].origin == "identity";
  }
  result.report.after      = result.region->cost;
  result.report.exhausted |= work.exhausted;
  result.status            = Status::feasible;
  result.reason            = "selected endpoints and shared residual preserve the original state boundary";
  return finish();
}

Logical_result synthesize_logical_region(const Xag_region& source, std::span<const uint32_t> eligible, const Logical_options& o,
                                         Budget& work, std::span<const uint32_t> domains) {
  return synthesize_logical_region(source, eligible, o, work, work, domains);
}

}  // namespace livehd::usyn
