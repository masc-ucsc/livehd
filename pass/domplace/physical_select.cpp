// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_select.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "domino_cell.hpp"
#include "physical_eligibility.hpp"

namespace livehd::domplace {
namespace {
using usyn::Budget;
using usyn::Status;
using usyn::Truth_table;
using usyn::Xag;
using usyn::Xsignal;

constexpr uint32_t kUnowned = std::numeric_limits<uint32_t>::max() - 1;
constexpr uint32_t kShared  = std::numeric_limits<uint32_t>::max();

// A candidate's physical function: the frozen cell's function with constant
// inputs cofactored away, inputs that share a producer merged, and inputs
// outside the support dropped. Variable i reads inputs[i].
struct Physical_function {
  Status               status = Status::invalid;
  std::vector<Xsignal> inputs;
  Truth_table          table;
  bool                 rewritten = false;  // the variables differ from the frozen cell's
};

Physical_function physical_function(const usyn::Frozen_cell& cell, Budget& work) {
  Physical_function result;
  struct Binding {
    bool     constant = false, value = false;
    uint32_t variable = 0;
    bool     inverted = false;
  };
  const auto           n = static_cast<uint32_t>(cell.inputs.size());
  std::vector<Binding> bindings(n);
  std::vector<Xsignal> merged;
  for (uint32_t i = 0; i < n; ++i) {
    const auto& ref = cell.inputs[i];
    if (ref.space != usyn::Endpoint_ref::Space::native) {
      return result;  // one-phase cells read native signals only
    }
    if (ref.index == 0) {
      bindings[i]      = {true, ref.inverted, 0, false};
      result.rewritten = true;
      continue;
    }
    const auto it = std::find_if(merged.begin(), merged.end(), [&](Xsignal s) { return s.id == ref.index; });
    if (it != merged.end()) {
      bindings[i]      = {false, false, static_cast<uint32_t>(it - merged.begin()), it->inverted != ref.inverted};
      result.rewritten = true;
      continue;
    }
    bindings[i] = {false, false, static_cast<uint32_t>(merged.size()), false};
    merged.push_back({ref.index, ref.inverted});
  }
  const auto m = static_cast<uint32_t>(merged.size());
  if (!work.spend(uint64_t{1} << m)) {
    result.status = Status::search_exhausted;
    return result;
  }
  Truth_table table(m);
  for (uint32_t y = 0; y < (1U << m); ++y) {
    uint32_t x = 0;
    for (uint32_t i = 0; i < n; ++i) {
      const auto& b  = bindings[i];
      x             |= uint32_t{b.constant ? b.value : (((y >> b.variable) & 1U) != 0) != b.inverted} << i;
    }
    table.set(y, cell.function.get(x));
  }
  const auto analysis = usyn::analyze_function(table, work);
  if (analysis.status != Status::feasible) {
    result.status = work.exhausted ? Status::search_exhausted : Status::invalid;
    return result;
  }
  result.status = Status::feasible;
  if (analysis.support == (1U << m) - 1) {
    result.inputs = std::move(merged);
    result.table  = std::move(table);
    return result;
  }
  // A variable outside the support may take any value; read it as zero.
  result.rewritten = true;
  std::vector<uint32_t> kept;
  for (uint32_t v = 0; v < m; ++v) {
    if (analysis.support & (1U << v)) {
      kept.push_back(v);
      result.inputs.push_back(merged[v]);
    }
  }
  const auto k = static_cast<uint32_t>(kept.size());
  if (!work.spend(uint64_t{1} << k)) {
    result.status = Status::search_exhausted;
    return result;
  }
  result.table = Truth_table(k);
  for (uint32_t z = 0; z < (1U << k); ++z) {
    uint32_t y = 0;
    for (uint32_t j = 0; j < k; ++j) {
      y |= ((z >> j) & 1U) << kept[j];
    }
    result.table.set(z, table.get(y));
  }
  return result;
}

// f with variable v fixed at `value`, then restricted to its support. The
// variables keep their order; `rewritten` is set.
Physical_function fix_variable(const Physical_function& f, uint32_t v, bool value, Budget& work) {
  Physical_function result;
  const auto        m = static_cast<uint32_t>(f.inputs.size());
  if (!work.spend(uint64_t{1} << m)) {
    result.status = Status::search_exhausted;
    return result;
  }
  Truth_table          fixed(m - 1);
  std::vector<Xsignal> inputs;
  for (uint32_t k = 0; k < m; ++k) {
    if (k != v) {
      inputs.push_back(f.inputs[k]);
    }
  }
  for (uint32_t y = 0; y < (1U << (m - 1)); ++y) {
    const auto low  = y & ((1U << v) - 1);
    const auto high = (y >> v) << (v + 1);
    fixed.set(y, f.table.get(low | high | (uint32_t{value} << v)));
  }
  const auto analysis = usyn::analyze_function(fixed, work);
  if (analysis.status != Status::feasible) {
    result.status = work.exhausted ? Status::search_exhausted : Status::invalid;
    return result;
  }
  result.status    = Status::feasible;
  result.rewritten = true;
  std::vector<uint32_t> kept;
  for (uint32_t k = 0; k + 1 < m; ++k) {
    if (analysis.support & (1U << k)) {
      kept.push_back(k);
      result.inputs.push_back(inputs[k]);
    }
  }
  result.table = Truth_table(static_cast<uint32_t>(kept.size()));
  for (uint32_t z = 0; z < (1U << kept.size()); ++z) {
    uint32_t y = 0;
    for (uint32_t j = 0; j < kept.size(); ++j) {
      y |= ((z >> j) & 1U) << kept[j];
    }
    result.table.set(z, fixed.get(y));
  }
  return result;
}

// U22: the data function without the storage's reset. A synchronous reset's
// net must be a direct input (else reset-absorbed) and load `value` while
// asserted (else reset-shape); an asynchronous one (no value) is stripped when
// present. The result is the function while it is not asserted. nullopt with
// the retention reason in `why`.
std::optional<Physical_function> strip_reset(const Physical_function& f, Xsignal net, bool asserted, std::optional<bool> value,
                                             Reason* why, Budget& work) {
  const auto it = std::find_if(f.inputs.begin(), f.inputs.end(), [&](Xsignal s) { return s.id == net.id; });
  if (it == f.inputs.end()) {
    if (!value) {
      return f;  // an asynchronous reset outside D: nothing to strip
    }
    *why = Reason::reset_absorbed;
    return std::nullopt;
  }
  const auto v      = static_cast<uint32_t>(it - f.inputs.begin());
  const bool active = asserted != (it->inverted != net.inverted);  // the variable's value while asserted
  for (uint32_t y = 0; value && y < (1U << f.inputs.size()); ++y) {
    if ((((y >> v) & 1U) != 0) == active && f.table.get(y) != *value) {
      *why = Reason::reset_shape;
      return std::nullopt;
    }
  }
  return fix_variable(f, v, !active, work);
}

// Whether the hold stays inside the PDN under one enable rail: Q is a direct
// input and some other input (the gate's enable, or its conjunction with the
// register's own enable) makes the function Q itself (next = en ? f : Q).
bool holds_under_enable(const Physical_function& f, Xsignal q) {
  const auto vq = std::find_if(f.inputs.begin(), f.inputs.end(), [&](Xsignal s) { return s.id == q.id; });
  if (vq == f.inputs.end()) {
    return false;
  }
  const auto m        = static_cast<uint32_t>(f.inputs.size());
  const auto qi       = static_cast<uint32_t>(vq - f.inputs.begin());
  const bool inverted = vq->inverted != q.inverted;
  for (uint32_t v = 0; v < m; ++v) {
    for (uint32_t b = 0; b < 2 && v != qi; ++b) {
      bool hold = true;
      for (uint32_t y = 0; y < (1U << m) && hold; ++y) {
        hold = ((y >> v) & 1U) != b || f.table.get(y) == ((((y >> qi) & 1U) != 0) != inverted);
      }
      if (hold) {
        return true;
      }
    }
  }
  return false;
}

// Which root reaches each native node: one state's D (its index), kShared when
// an output or two states reach it, kUnowned when none does. A node changes
// owner at most twice, so the walk is linear in the graph.
std::optional<std::vector<uint32_t>> ownership(const usyn::Endpoint_netlist& net, Budget& work) {
  std::vector<uint32_t>                      owner(net.native.size(), kUnowned);
  std::vector<std::pair<usyn::Id, uint32_t>> stack;
  const auto                                 walk = [&](usyn::Id root, uint32_t label) {
    stack.assign(1, {root, label});
    while (!stack.empty()) {
      const auto [id, reach] = stack.back();
      stack.pop_back();
      if (!work.spend()) {
        return false;
      }
      auto& o = owner.at(id);
      if (o == reach || o == kShared) {
        continue;
      }
      o                = o == kUnowned ? reach : kShared;
      const auto& node = net.native.node(id);
      if (node.kind == Xag::Kind::and_gate || node.kind == Xag::Kind::xor_gate) {
        stack.emplace_back(node.inputs[0].id, o);
        stack.emplace_back(node.inputs[1].id, o);
      }
    }
    return true;
  };
  for (const auto& output : net.outputs) {
    if (!walk(output.signal.id, kShared)) {
      return std::nullopt;
    }
  }
  for (uint32_t i = 0; i < net.state.size(); ++i) {
    if (!walk(net.state[i].reference_d.id, i)) {
      return std::nullopt;
    }
  }
  return owner;
}

// The Liberty estimate of the logic that only state `i` keeps alive above its
// rail cut: what the CMOS implementation spends on D that a domino stage
// replaces. The rail producers stay alive after conversion, so a node in their
// cones (structural hashing can share one with D above the cut) is not saved.
// Copied into a small XAG whose leaves are the cut, kept and shared nodes.
usyn::Native_estimate exclusive_cone(const usyn::Endpoint_netlist& net, const usyn::Frozen_cell& cell,
                                     std::span<const Xsignal> producers, uint32_t i, const std::vector<uint32_t>& owner,
                                     const usyn::Native_cost_model& model, Budget& work,
                                     std::optional<usyn::Id> enable_one = std::nullopt) {
  usyn::Native_estimate        result;
  std::unordered_set<usyn::Id> cut, kept, seen;
  std::vector<usyn::Id>        stack, nodes;
  for (const auto& input : cell.inputs) {
    cut.insert(input.index);
  }
  // Another root's node is never counted, nor is anything below it, so the
  // marking stops there and stays linear.
  for (const auto producer : producers) {
    stack.push_back(producer.id);
  }
  while (!stack.empty()) {
    const auto id = stack.back();
    stack.pop_back();
    if (!work.spend()) {
      result.status = Status::search_exhausted;
      return result;
    }
    if (owner.at(id) != i || !kept.insert(id).second) {
      continue;
    }
    const auto& node = net.native.node(id);
    if (node.kind == Xag::Kind::and_gate || node.kind == Xag::Kind::xor_gate) {
      stack.push_back(node.inputs[0].id);
      stack.push_back(node.inputs[1].id);
    }
  }
  stack.push_back(net.state[i].reference_d.id);
  while (!stack.empty()) {
    const auto id = stack.back();
    stack.pop_back();
    if (!work.spend()) {
      result.status = Status::search_exhausted;
      return result;
    }
    const auto& node = net.native.node(id);
    if (cut.contains(id) || kept.contains(id) || owner.at(id) != i || !seen.insert(id).second
        || (node.kind != Xag::Kind::and_gate && node.kind != Xag::Kind::xor_gate)) {
      continue;
    }
    nodes.push_back(id);
    stack.push_back(node.inputs[0].id);
    stack.push_back(node.inputs[1].id);
  }
  if (nodes.empty()) {
    result.status = Status::feasible;  // D is a cut signal or shared: only the register is saved
    return result;
  }
  std::sort(nodes.begin(), nodes.end());  // XAG ids are topological
  Xag                                   small;
  std::unordered_map<usyn::Id, Xsignal> mapped{
      {0, small.constant(false)}
  };
  if (enable_one) {
    mapped.emplace(*enable_one, small.constant(true));  // a retained folded register drops the redundant enable
  }
  const auto signal = [&](Xsignal s) {
    auto it = mapped.find(s.id);
    if (it == mapped.end()) {
      it = mapped.emplace(s.id, small.input("n" + std::to_string(s.id))).first;
    }
    return s.inverted ? ~it->second : it->second;
  };
  for (const auto id : nodes) {
    const auto& node = net.native.node(id);
    const auto  a = signal(node.inputs[0]), b = signal(node.inputs[1]);
    mapped[id] = node.kind == Xag::Kind::and_gate ? small.land(a, b) : small.lxor(a, b);
  }
  const std::array outputs{signal(net.state[i].reference_d)};
  return usyn::estimate_native_cost(small, outputs, model, work, /*gate_objective=*/false);
}
}  // namespace

std::optional<usyn::Physical_emission> select_physical(const usyn::Stateful_region& region,
                                                       std::span<const std::string> input_names, const Select_options& options,
                                                       const Select_context& context, usyn::Budget& work,
                                                       std::span<const usyn::Physical_hint> gating) {
  if (!region.frozen) {
    return std::nullopt;
  }
  const auto& net = *region.frozen;
  // Never relabel a two-phase result: a different clock_phases is a different
  // logical cache identity, so the caller re-selects from the source.
  if (net.clock_phases != 1
      || std::any_of(net.cells.begin(), net.cells.end(), [](const auto& c) { return c.kind == usyn::Endpoint_kind::Domino; })) {
    return std::nullopt;
  }
  if (!context.reference || context.reference->registers.empty()
      || (options.policy == Select_options::Policy::area && !context.cmos)) {
    throw std::invalid_argument("physical selection needs a reference and, for select=area, a CMOS cost model");
  }
  const auto& reference = *context.reference;
  const auto  rows      = physical_eligibility(region, input_names, options.clock, gating);
  if (rows.size() != net.state.size()) {
    return std::nullopt;
  }
  usyn::Work_slice                     slice(work, options.work);
  auto&                                budget = slice.work;
  usyn::Physical_emission              emission;
  std::map<std::string, uint32_t>      cells;
  std::optional<std::vector<uint32_t>> owner;
  bool                                 exhausted = false;
  for (const auto& row : rows) {
    const auto i           = row.state;
    const auto eligibility = row.reason;
    auto&      state       = emission.states.emplace_back();
    state.state            = i;
    state.gated_clock      = row.gated_clock;
    state.gate_enable      = row.gate_enable;
    const auto retain      = [&](Reason reason) { state.reason = reason_code(reason); };
    // Exhaustion is sticky: this and every later candidate keeps CMOS.
    const auto out_of_work = [&] {
      state             = {};
      state.state       = i;
      state.gated_clock = row.gated_clock;
      state.gate_enable = row.gate_enable;
      state.eligible    = true;
      exhausted      = true;
      retain(Reason::selection_budget);
    };
    if (eligibility != Reason::candidate) {
      retain(eligibility);
      continue;
    }
    state.eligible = true;
    if (exhausted) {
      out_of_work();
      continue;
    }
    // Recheck the DominoLatch as expand_endpoint_netlist does.
    const auto latch = net.state[i].domino_latch;
    if (!latch || *latch >= net.cells.size() || net.cells[*latch].kind != usyn::Endpoint_kind::DominoLatch
        || net.cells[*latch].state != i) {
      return std::nullopt;
    }
    const auto& cell    = net.cells[*latch];
    const auto  metrics = cell.formula.metrics();
    if (!metrics || *metrics != cell.metrics) {
      return std::nullopt;
    }
    const auto table = cell.formula.evaluate_table(static_cast<uint32_t>(cell.inputs.size()), budget);
    if (!table && budget.exhausted) {
      out_of_work();
      continue;
    }
    if (!table || *table != cell.function) {
      return std::nullopt;
    }
    auto function = physical_function(cell, budget);
    if (function.status == Status::search_exhausted) {
      out_of_work();
      continue;
    }
    if (function.status != Status::feasible) {
      return std::nullopt;
    }
    // U22: the reset leaves the PDN for the storage's RST pin. A synchronous
    // one must load its value while asserted; an asynchronous one is outside
    // D, and any reset term left in it is a don't-care the storage overrides.
    if (row.reset) {
      const auto& reset = *row.reset;
      const auto  rst   = reset.source == usyn::Physical_reset::Source::input   ? net.inputs.at(reset.index)
                          : reset.source == usyn::Physical_reset::Source::state ? net.state.at(reset.index).q
                                                                                : net.outputs.at(reset.index).signal;
      Reason      why   = Reason::reset_shape;
      auto        data  = strip_reset(function,
                              rst,
                              !reset.active_low,
                              reset.async ? std::nullopt : std::optional<bool>{reset.value},
                              &why,
                              budget);
      if (budget.exhausted) {
        out_of_work();
        continue;
      }
      if (!data || data->status != Status::feasible) {
        retain(why);
        continue;
      }
      function = std::move(*data);
    }
    if (function.inputs.empty()) {
      retain(Reason::constant_endpoint);
      continue;
    }
    // A folded gate: the hold must stay in the PDN under one enable rail.
    // The selection cut keeps en * f + !en * Q within the stack and input
    // caps (excess f moves upstream, as for any register); when not even the
    // hold fits, the register stays CMOS.
    if (row.clock_root && !holds_under_enable(function, net.state[i].q)) {
      retain(Reason::enable_stack);
      continue;
    }
    if (!function.rewritten && !cell.formula.output_inverted) {
      state.formula = cell.formula;
    } else {
      auto limits     = options.functions;
      limits.output   = usyn::Output_polarity::positive;
      const auto gate = usyn::synthesize_gate(function.table, net.gates, budget, limits);
      if (!gate.formula) {
        if (budget.exhausted) {
          out_of_work();
        } else {
          // A cap of this one search (cubes, formula nodes, irredundancy) is
          // no illegality proof, and says nothing about later states.
          retain(gate.search_exhausted ? Reason::polarity_search : Reason::polarity_illegal);
        }
        continue;
      }
      state.formula = gate.formula;
    }
    state.metrics      = *state.formula->metrics();
    const auto support = static_cast<uint32_t>(function.inputs.size());
    for (const auto rail : usyn::gate_rails(state.metrics, support)) {
      auto producer      = function.inputs[rail.input];
      producer.inverted ^= rail.negative;
      state.rails.push_back({rail.input, rail.negative, producer});
      // No free complement in the physical ledger, a converted Q included.
      state.a_rail_not += producer.inverted ? reference.inverter.area : 0.0;
    }
    state.a_domino = domino_area(state.metrics, reference, 0, row.reset.has_value()).a_domino;
    if (state.metrics.transistors <= 1 && !options.identity) {
      retain(Reason::identity_endpoint);
      continue;
    }
    auto decision = Reason::domino_all;
    if (options.policy == Select_options::Policy::area) {
      if (!owner) {
        owner = ownership(net, budget);
      }
      // A folded register's CMOS keeps its gated clock, not the redundant enable.
      const auto enable_one = row.clock_root && row.gate_enable && *row.gate_enable < net.inputs.size()
                                  ? std::optional<usyn::Id>{net.inputs[*row.gate_enable].id}
                                  : std::nullopt;
      const auto cone       = owner ? exclusive_cone(net, cell, function.inputs, i, *owner, *context.cmos, budget, enable_one)
                                    : usyn::Native_estimate{};
      if (!owner || cone.status == Status::search_exhausted || budget.exhausted) {
        out_of_work();
        continue;
      }
      if (cone.status != Status::feasible) {
        retain(Reason::cmos_estimate);
        continue;
      }
      // An asynchronous reset needs a reset register in CMOS too.
      state.a_cmos = context.cmos_register_area + (row.reset && row.reset->async ? reference.reset_area : 0.0) + cone.area;
      // U25: at equal size the domino wins (its logic stays inside one cell,
      // so less wiring to route). A tie is equality within kAreaTieUm2 of the
      // threshold, which is the CMOS estimate itself at margin 0 (the factor
      // is exactly 1); a margin raises the threshold, and a tie with the
      // raised threshold converts too.
      const double domino    = state.a_domino + state.a_rail_not;
      const double threshold = *state.a_cmos * ((100.0 + options.area_margin_pct) / 100.0);
      if (domino > threshold + kAreaTieUm2) {
        retain(Reason::area);
        continue;
      }
      decision = domino < threshold - kAreaTieUm2 ? Reason::domino_area : Reason::domino_tie;
    }
    const auto master = make_domino_master(*state.formula,
                                           support,
                                           budget,
                                           row.reset ? std::optional<Domino_reset>{{row.reset->async,
                                                                                    row.reset->active_low,
                                                                                    row.reset->value}}
                                                     : std::nullopt);
    if (!master) {
      out_of_work();
      continue;
    }
    auto declared          = domino_interface(*master, 0);  // sizing is disabled: variant 0
    const auto [it, novel] = cells.try_emplace(declared.name, static_cast<uint32_t>(emission.cells.size()));
    if (novel) {
      emission.cells.push_back(std::move(declared));
    }
    state.cell       = it->second;
    state.decision   = usyn::Physical_decision::domino;
    state.clock_root = row.clock_root;
    state.reset      = row.reset;
    retain(decision);
  }
  return emission;
}

}  // namespace livehd::domplace
