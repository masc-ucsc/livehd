// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "residual.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <unordered_set>

namespace livehd::usyn {
bool valid_residual_options(const Residual_options& o) {
  return o.and_cost > 0 && o.xor_cost > 0 && o.max_nodes > 0 && o.windows > 0 && o.window_nodes > 0 && o.resub_inputs > 0
         && o.resub_inputs <= 8 && o.divisors > 0 && o.divisors <= 64 && o.inserted <= 2 && o.window_work > 0 && o.stage_work > 0;
}

namespace {

// Small native template library. Match all input permutations/phases and both
// output phases on at most four variables. This is a bounded rewrite library,
// not an optimal synthesis claim for all four-input Boolean functions.
struct Step {
  uint8_t a, b;
  bool    xor_gate = false;
  bool    invert_a = false, invert_b = false;
};
struct Pattern {
  uint8_t             inputs, count;
  std::array<Step, 5> gates{};  // inputs 0..3, gate results 4,5,...
};
constexpr std::array patterns{
    Pattern{2, 1,                                                                                   {{{0, 1}}}},
    Pattern{2, 1,                                                                             {{{0, 1, true}}}},
    Pattern{3, 2,                                                                           {{{0, 1}, {4, 2}}}},
    Pattern{3, 2,                                                        {{{0, 1}, {4, 2, false, true, true}}}},
    Pattern{3, 2,                                                                     {{{0, 1}, {4, 2, true}}}},
    Pattern{3, 2,                                                                     {{{0, 1, true}, {4, 2}}}},
    Pattern{3, 2,                                                               {{{0, 1, true}, {4, 2, true}}}},
    Pattern{3, 3,                                   {{{0, 1}, {0, 2, false, true}, {4, 5, false, true, true}}}}, // mux, SOP
    Pattern{3, 3,                                                       {{{1, 2, true}, {0, 4}, {2, 5, true}}}}, // mux, XOR form
    Pattern{3, 4, {{{0, 1}, {0, 1, false, true, true}, {2, 5, false, false, true}, {4, 6, false, true, true}}}}, // majority
    Pattern{3, 4,                                         {{{0, 1, true}, {0, 2, true}, {4, 5}, {0, 6, true}}}}, // majority, XOR form
    Pattern{4, 3,                                                                   {{{0, 1}, {2, 3}, {4, 5}}}},
    Pattern{4, 3,                                                                   {{{0, 1}, {4, 2}, {5, 3}}}},
    Pattern{4, 3,                                                {{{0, 1}, {2, 3}, {4, 5, false, true, true}}}},
    Pattern{4, 3,                                                             {{{0, 1}, {2, 3}, {4, 5, true}}}},
    Pattern{4, 3,                                                       {{{0, 1, true}, {2, 3, true}, {4, 5}}}},
    Pattern{4, 3,                                                 {{{0, 1, true}, {2, 3, true}, {4, 5, true}}}},
    Pattern{4, 3,                                                 {{{0, 1, true}, {4, 2, true}, {5, 3, true}}}},
    Pattern{4, 3,                                                             {{{0, 1, true}, {2, 3}, {4, 5}}}},
    Pattern{4, 3,                                                       {{{0, 1, true}, {2, 3}, {4, 5, true}}}},
    Pattern{4, 3,                                                {{{0, 1}, {4, 2, false, true, true}, {5, 3}}}},
    Pattern{4, 3,                                                             {{{0, 1}, {4, 2, true}, {5, 3}}}},
    Pattern{4, 3,                                                             {{{0, 1}, {4, 2}, {5, 3, true}}}},
    Pattern{4, 3,                                                       {{{0, 1, true}, {4, 2}, {5, 3, true}}}},
};

uint64_t weight(Xag::Kind kind, const Residual_options& o) {
  return kind == Xag::Kind::and_gate ? o.and_cost : kind == Xag::Kind::xor_gate ? o.xor_cost : 0;
}

void limited(Residual_report& r, std::string reason) {
  r.exhausted = true;
  if (std::find(r.limits.begin(), r.limits.end(), reason) == r.limits.end()) {
    r.limits.push_back(std::move(reason));
  }
}

using Slice = Work_slice;

struct Schedule {
  std::vector<uint64_t> references;
  std::vector<Id>       order;
  uint64_t              cost = 0;
};

std::optional<Schedule> schedule(const Xag& g, std::span<const Residual_output> outputs, const Residual_options& o, Budget& work) {
  if (!work.spend(g.size() + outputs.size())) {
    return std::nullopt;
  }
  Schedule s;
  s.references.resize(g.size());
  std::vector<uint8_t> seen(g.size(), 0);
  for (const auto& output : outputs) {
    ++s.references[output.signal.id];
  }
  // A dependency-first walk of DOMINO-input cones gets first access to each
  // sweep's work budget. Other protected outputs remain part of the ledger.
  for (bool priority : {true, false}) {
    for (const auto& output : outputs) {
      if (output.endpoint_input != priority) {
        continue;
      }
      std::vector<Id> stack{output.signal.id};
      while (!stack.empty()) {
        if (!work.spend()) {
          return std::nullopt;
        }
        const auto  id = stack.back();
        const auto& n  = g.node(id);
        if (seen[id] == 2 || weight(n.kind, o) == 0) {
          stack.pop_back();
          continue;
        }
        if (seen[id] == 0) {
          seen[id] = 1;
          for (auto in : n.inputs) {
            ++s.references[in.id];
            if (seen[in.id] == 0) {
              stack.push_back(in.id);
            }
          }
        } else {
          seen[id] = 2;
          s.order.push_back(id);
          s.cost += weight(n.kind, o);
          stack.pop_back();
        }
      }
    }
  }
  return s;
}

// References include active graph edges and temporary anchors for consumers
// not rebuilt yet. Those anchors protect outside readers during a sweep.
// Transactions update only touched nodes and roll back without more admission.
class Ledger {
public:
  Ledger(const Xag& graph, const Residual_options& options, Residual_report& report) : g(graph), o(options), r(report) {}
  void begin() {
    undo.clear();
    saved_cost = cost;
  }
  bool change(Id root, int64_t amount, Budget& work) {
    refs.resize(g.size(), 0);
    std::vector<std::pair<Id, int64_t>> todo{
        {root, amount}
    };
    while (!todo.empty()) {
      if (!work.spend()) {
        return false;
      }
      const auto [id, delta] = todo.back();
      todo.pop_back();
      ++r.reference_visits;
      const auto before = refs[id];
      if (delta < 0 && static_cast<uint64_t>(-delta) > before) {
        return false;
      }
      undo.emplace_back(id, before);
      refs[id] = delta < 0 ? before - static_cast<uint64_t>(-delta) : before + static_cast<uint64_t>(delta);
      if ((before == 0) == (refs[id] == 0)) {
        continue;
      }
      const auto& n         = g.node(id);
      const auto  node_cost = weight(n.kind, o);
      if (node_cost == 0) {
        continue;
      }
      const int64_t step = refs[id] == 0 ? -1 : 1;
      cost               = step < 0 ? cost - node_cost : cost + node_cost;
      for (auto in : n.inputs) {
        todo.emplace_back(in.id, step);
      }
    }
    return true;
  }
  void rollback() {
    for (auto it = undo.rbegin(); it != undo.rend(); ++it) {
      refs[it->first] = it->second;
    }
    cost = saved_cost;
    undo.clear();
  }
  uint64_t count(Id id) const { return id < refs.size() ? refs[id] : 0; }
  uint64_t cost = 0;

private:
  const Xag&                           g;
  const Residual_options&              o;
  Residual_report&                     r;
  std::vector<uint64_t>                refs;
  std::vector<std::pair<Id, uint64_t>> undo;
  uint64_t                             saved_cost = 0;
};

std::optional<Residual_network> compact(const Xag& g, std::span<const Residual_output> outputs, const std::vector<bool>& affected,
                                        const Residual_options& o, Budget& work) {
  auto live = schedule(g, outputs, o, work);
  if (!live || !work.spend(g.size())) {
    return std::nullopt;
  }
  Residual_network     out;
  std::vector<Xsignal> map(g.size());
  for (Id id = 1; id < g.size(); ++id) {
    const auto& n = g.node(id);
    if (n.kind == Xag::Kind::source) {
      map[id] = out.graph.input(g.input_names()[n.source_index]);
    }
  }
  const auto translated = [&](Xsignal s) { return s.inverted ? ~map[s.id] : map[s.id]; };
  for (auto id : live->order) {
    if (!work.spend()) {
      return std::nullopt;
    }
    const auto& n = g.node(id);
    const auto  a = translated(n.inputs[0]), b = translated(n.inputs[1]);
    const auto  size_before = out.graph.size();
    map[id]                 = n.kind == Xag::Kind::and_gate ? out.graph.land(a, b) : out.graph.lxor(a, b);
    if (out.graph.size() != size_before) {
      out.estimated_cost += weight(n.kind, o);
    }
  }
  for (const auto& output : outputs) {
    out.outputs.push_back({translated(output.signal), output.endpoint_input});
  }
  out.affected = affected;
  return out;
}

using Bits = std::array<uint64_t, 4>;
struct Divisor {
  Xsignal signal;
  Bits    bits{};
};

class Sweep {
public:
  Sweep(const Xag& graph, std::span<const Residual_output> roots, const Residual_options& options, Residual_report& report,
        Budget& work, bool rewriting)
      : source(graph), outputs(roots), o(options), r(report), total(work), rewrite(rewriting), ledger(g, o, r) {}

  std::optional<Residual_network> run() {
    auto plan = schedule(source, outputs, o, total);
    if (!plan || !total.spend(source.size())) {
      return std::nullopt;
    }
    std::vector<Xsignal> map(source.size());
    std::vector<bool>    changed(source.size(), false);
    ledger.begin();
    if (!ledger.change(0, static_cast<int64_t>(plan->references[0]), total)) {
      return std::nullopt;
    }
    for (Id id = 1; id < source.size(); ++id) {
      const auto& node = source.node(id);
      if (node.kind == Xag::Kind::source) {
        map[id] = g.input(source.input_names()[node.source_index]);
        ledger.begin();
        if (!ledger.change(map[id].id, static_cast<int64_t>(plan->references[id]), total)) {
          return std::nullopt;
        }
      }
    }
    remaining_nodes        = plan->order.size();
    uint64_t     attempted = 0, search_work = 0;
    Credit_share sweep_cap(total, o.stage_work, rewrite ? 3 : 1);
    const auto   translated = [&](Xsignal s) { return s.inverted ? ~map[s.id] : map[s.id]; };
    for (auto id : plan->order) {
      if (!total.spend() || g.size() >= o.max_nodes) {
        return std::nullopt;
      }
      const auto& node = source.node(id);
      const auto  a = translated(node.inputs[0]), b = translated(node.inputs[1]);
      auto        root = node.kind == Xag::Kind::and_gate ? g.land(a, b) : g.lxor(a, b);
      --remaining_nodes;
      ledger.begin();
      anchors = plan->references[id];
      if (!ledger.change(root.id, static_cast<int64_t>(anchors), total) || !ledger.change(a.id, -1, total)
          || !ledger.change(b.id, -1, total)) {
        return std::nullopt;
      }
      chosen                    = root;
      depth_limit               = uint64_t{node.level} + o.depth_slack;
      // Reserve linear rebuild/compaction work, including a conservative
      // allowance for trial nodes appended by this window. Later windows can
      // be skipped without discarding a completed prefix's improvements.
      // The window slice is min(caps, (remaining - completion) / 32): nonzero
      // exactly when remaining >= completion + 32.
      const uint64_t completion = 12 * (g.size() + remaining_nodes) + 6 * outputs.size() + source.size() + 32;
      if (attempted < o.windows && sweep_cap.exceeds(search_work) && total.has(completion + 32) && !total.exhausted) {
        // Leave work for rebuilding the remaining graph and for the next
        // sweep; a local refusal never mutates the input or its boundaries.
        // The cap is min(window_work, sweep_cap - search_work).
        Slice slice(total, sweep_cap.clamp(search_work + o.window_work) - search_work, 32, completion);
        ++attempted;
        if (rewrite) {
          ++r.rewrite_windows;
          rewrite_root(root, slice.work);
        } else {
          ++r.resub_windows;
          resub_root(root, slice.work);
        }
        search_work += slice.work.consumed;
        if (slice.work.exhausted) {
          limited(r, rewrite ? "rewrite window work budget" : "resubstitution window work budget");
        }
      } else {
        limited(r, "residual sweep search budget");
      }
      map[id]     = chosen;
      changed[id] = chosen != root || changed[node.inputs[0].id] || changed[node.inputs[1].id];
    }
    std::vector<Residual_output> result_outputs;
    std::vector<bool>            affected;
    for (const auto& output : outputs) {
      result_outputs.push_back({translated(output.signal), output.endpoint_input});
      affected.push_back(changed[output.signal.id]);
    }
    return compact(g, result_outputs, affected, o, total);
  }

private:
  const Xag&                       source;
  std::span<const Residual_output> outputs;
  const Residual_options&          o;
  Residual_report&                 r;
  Budget&                          total;
  bool                             rewrite;
  Xag                              g;
  Ledger                           ledger;
  Xsignal                          chosen;
  uint64_t                         anchors = 0, remaining_nodes = 0, depth_limit = 0;

  bool room(uint32_t nodes) {
    if (g.size() + remaining_nodes + nodes > o.max_nodes) {
      limited(r, "residual candidate node budget");
      return false;
    }
    return true;
  }

  bool consider(Xsignal candidate, Budget& work) {
    ++r.candidates;
    if (candidate == chosen) {
      return false;
    }
    if (g.node(candidate.id).level > depth_limit) {
      ++r.depth_rejections;
      return false;
    }
    ledger.begin();
    const auto before = ledger.cost;
    if (!ledger.change(candidate.id, static_cast<int64_t>(anchors), work)
        || !ledger.change(chosen.id, -static_cast<int64_t>(anchors), work)) {
      ledger.rollback();
      return false;
    }
    if (ledger.cost >= before) {
      ++r.cost_rejections;
      ledger.rollback();
      return false;
    }
    chosen = candidate;
    if (rewrite) {
      ++r.rewrite_wins;
    } else {
      ++r.resub_wins;
    }
    return true;
  }

  void rewrite_root(Xsignal root, Budget& work) {
    const auto window = grow_window(g, root, {4, o.window_nodes}, work);
    if (window.status != Status::feasible) {
      limited(r, "rewrite window admission");
      return;
    }
    const auto truth = window_function(g, window, work);
    if (truth.status != Status::feasible) {
      limited(r, "rewrite truth-table budget");
      return;
    }
    const auto n      = static_cast<uint32_t>(window.leaves.size());
    const auto mask   = (1U << (1U << n)) - 1U;
    const auto target = static_cast<uint32_t>(truth.table.words[0]);
    if (target == 0 || target == mask) {
      consider(g.constant(target != 0), work);
      return;
    }
    std::array<uint32_t, 4> variables{0xAAAA, 0xCCCC, 0xF0F0, 0xFF00};
    for (uint32_t j = 0; j < n; ++j) {
      const auto value = variables[j] & mask;
      if (target == value || target == (value ^ mask)) {
        consider({window.leaves[j], target != value}, work);
      }
    }
    for (const auto& pattern : patterns) {
      if (pattern.inputs > n) {
        continue;
      }
      std::array<uint32_t, 4> perm{0, 1, 2, 3};
      do {
        for (uint32_t phase = 0; phase < (1U << pattern.inputs); ++phase) {
          if (!work.spend(pattern.count + pattern.inputs + 1)) {
            return;
          }
          std::array<uint32_t, 9> bits{};
          for (uint32_t j = 0; j < pattern.inputs; ++j) {
            bits[j] = (variables[perm[j]] & mask) ^ ((phase & (1U << j)) ? mask : 0);
          }
          for (uint32_t j = 0; j < pattern.count; ++j) {
            const auto& step = pattern.gates[j];
            const auto  a = bits[step.a] ^ (step.invert_a ? mask : 0), b = bits[step.b] ^ (step.invert_b ? mask : 0);
            bits[j + 4] = step.xor_gate ? a ^ b : a & b;
          }
          const auto value = bits[pattern.count + 3];
          if ((value != target && (value ^ mask) != target) || !room(pattern.count)) {
            continue;
          }
          std::array<Xsignal, 9> signals{};
          for (uint32_t j = 0; j < pattern.inputs; ++j) {
            signals[j] = {window.leaves[perm[j]], (phase & (1U << j)) != 0};
          }
          for (uint32_t j = 0; j < pattern.count; ++j) {
            const auto& step = pattern.gates[j];
            const auto  a    = step.invert_a ? ~signals[step.a] : signals[step.a];
            const auto  b    = step.invert_b ? ~signals[step.b] : signals[step.b];
            signals[j + 4]   = step.xor_gate ? g.lxor(a, b) : g.land(a, b);
          }
          const auto signal = signals[pattern.count + 3];
          consider(value == target ? signal : ~signal, work);
        }
      } while (std::next_permutation(perm.begin(), perm.begin() + n));
    }
  }

  void resub_root(Xsignal root, Budget& work);
};

void Sweep::resub_root(Xsignal root, Budget& work) {
  const auto window = grow_window(g, root, {o.resub_inputs, o.window_nodes}, work);
  if (window.status != Status::feasible) {
    limited(r, "resubstitution window admission");
    return;
  }
  const uint32_t points = 1U << window.leaves.size();
  const uint32_t words  = (points + 63) / 64;
  Bits           mask{};
  for (uint32_t i = 0; i < words; ++i) {
    mask[i] = points >= 64 ? ~uint64_t{0} : (uint64_t{1} << points) - 1;
  }
  const auto complement = [&](Bits bits) {
    for (uint32_t i = 0; i < words; ++i) {
      bits[i] ^= mask[i];
    }
    return bits;
  };
  const auto combine = [&](Bits a, Bits b, bool xor_gate) {
    Bits bits{};
    for (uint32_t i = 0; i < words; ++i) {
      bits[i] = xor_gate ? a[i] ^ b[i] : a[i] & b[i];
    }
    return bits;
  };
  std::unordered_map<Id, Bits> tables;
  std::unordered_set<Id>       unavailable;
  tables.emplace(0, Bits{});
  for (uint32_t j = 0; j < window.leaves.size(); ++j) {
    if (!work.spend(points)) {
      return;
    }
    Bits bits{};
    for (uint32_t x = 0; x < points; ++x) {
      bits[x / 64] |= uint64_t{(x >> j) & 1U} << (x % 64);
    }
    tables.emplace(window.leaves[j], bits);
  }
  // Each admitted divisor is a total function of this same cut basis.
  // An outside source makes it unavailable; correlated ports are never
  // assigned unrelated tables to manufacture a replacement.
  const auto evaluate = [&](Id root_id) -> std::optional<Bits> {
    std::vector<Id> todo{root_id};
    while (!todo.empty()) {
      if (!work.spend()) {
        return std::nullopt;
      }
      const auto id = todo.back();
      if (tables.contains(id) || unavailable.contains(id)) {
        todo.pop_back();
        continue;
      }
      const auto& node = g.node(id);
      if (node.kind == Xag::Kind::source || unavailable.contains(node.inputs[0].id) || unavailable.contains(node.inputs[1].id)) {
        unavailable.insert(id);
        todo.pop_back();
        continue;
      }
      if (tables.size() >= uint64_t{o.window_nodes} + 1) {
        limited(r, "resubstitution table node budget");
        return std::nullopt;
      }
      bool ready = true;
      for (auto in : node.inputs) {
        if (!tables.contains(in.id)) {
          todo.push_back(in.id);
          ready = false;
          break;
        }
      }
      if (!ready) {
        continue;
      }
      if (!work.spend(words)) {
        return std::nullopt;
      }
      auto a = tables.at(node.inputs[0].id), b = tables.at(node.inputs[1].id);
      if (node.inputs[0].inverted) {
        a = complement(a);
      }
      if (node.inputs[1].inverted) {
        b = complement(b);
      }
      tables.emplace(id, combine(a, b, node.kind == Xag::Kind::xor_gate));
      todo.pop_back();
    }
    const auto found = tables.find(root_id);
    return found == tables.end() ? std::nullopt : std::optional<Bits>{found->second};
  };
  auto target_table = evaluate(root.id);
  if (!target_table) {
    return;
  }
  const auto target = root.inverted ? complement(*target_table) : *target_table;
  if (target == Bits{} || target == mask) {
    consider(g.constant(target == mask), work);
    return;
  }
  std::vector<Id> ids(window.leaves.begin(), window.leaves.end());
  ids.insert(ids.end(), window.interior.begin(), window.interior.end());
  for (uint32_t count = 0; count < o.divisor_scan && count < root.id; ++count) {
    if (!work.spend()) {
      return;
    }
    ids.push_back(root.id - count - 1);
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  std::stable_sort(ids.begin(), ids.end(), [&](auto a, auto b) {
    // Reusing an already shared implementation is often the cheapest choice.
    const bool shared_a = ledger.count(a) > 1, shared_b = ledger.count(b) > 1;
    return shared_a != shared_b ? shared_a : g.node(a).level < g.node(b).level;
  });
  std::vector<Divisor> literals;
  for (auto id : ids) {
    if (id == 0 || id >= root.id) {
      continue;  // excludes the root and every possible transitive fanout
    }
    const auto bits = evaluate(id);
    if (!bits) {
      if (work.exhausted) {
        return;
      }
      continue;
    }
    literals.push_back({
        {id, false},
        *bits
    });
    literals.push_back({
        {id, true},
        complement(*bits)
    });
    if (literals.size() >= 2 * o.divisors) {
      break;
    }
  }
  for (const auto& literal : literals) {
    if (!work.spend(words)) {
      return;
    }
    if (literal.bits == target) {
      consider(literal.signal, work);
    }
  }
  const auto operation = [&](Xsignal a, Xsignal b, bool xor_gate) { return xor_gate ? g.lxor(a, b) : g.land(a, b); };
  // All zero/one-node possibilities precede the larger two-node forest.
  // Match truth tables before constructing nodes or touching reference counts.
  if (o.inserted >= 1) {
    for (size_t a = 0; a < literals.size(); ++a) {
      for (size_t b = a; b < literals.size(); ++b) {
        for (bool xor_gate : {false, true}) {
          if (!work.spend(words)) {
            return;
          }
          const auto bits = combine(literals[a].bits, literals[b].bits, xor_gate);
          if ((bits == target || complement(bits) == target) && room(1)) {
            const auto signal = operation(literals[a].signal, literals[b].signal, xor_gate);
            consider(bits == target ? signal : ~signal, work);
          }
        }
      }
    }
  }
  if (o.inserted >= 2) {
    for (size_t a = 0; a < literals.size(); ++a) {
      for (size_t b = a; b < literals.size(); ++b) {
        for (bool inner_xor : {false, true}) {
          if (!work.spend(words)) {
            return;
          }
          const auto inner = combine(literals[a].bits, literals[b].bits, inner_xor);
          for (bool inverse : {false, true}) {
            const auto inner_bits = inverse ? complement(inner) : inner;
            for (const auto& c : literals) {
              for (bool outer_xor : {false, true}) {
                if (!work.spend(words)) {
                  return;
                }
                const auto bits = combine(inner_bits, c.bits, outer_xor);
                if ((bits == target || complement(bits) == target) && room(2)) {
                  auto signal = operation(literals[a].signal, literals[b].signal, inner_xor);
                  if (inverse) {
                    signal = ~signal;
                  }
                  signal = operation(signal, c.signal, outer_xor);
                  consider(bits == target ? signal : ~signal, work);
                }
              }
            }
          }
        }
      }
    }
  }
}

}  // namespace

Residual_result optimize_residual(const Xag& graph, std::span<const Residual_output> outputs, const Residual_options& options,
                                  Budget& work) {
  Residual_result                 result;
  std::optional<Residual_network> current;
  uint64_t                        committed_rewrites = 0, committed_resubs = 0;
  if (!valid_residual_options(options)) {
    result.reason = "invalid residual optimization request";
    return result;
  }
  const auto refuse = [&] {
    result.status              = current ? Status::feasible : Status::search_exhausted;
    result.report.cost_after   = current ? current->estimated_cost : result.report.cost_before;
    result.network             = std::move(current);
    result.report.rewrite_wins = committed_rewrites;
    result.report.resub_wins   = committed_resubs;
    result.reason              = "residual rebuild budget; last complete network retained";
    limited(result.report, "residual rebuild budget");
    return std::move(result);
  };
  if (graph.size() > options.max_nodes || outputs.size() > options.max_nodes) {
    return refuse();
  }
  if (!work.spend(outputs.size())) {
    return refuse();
  }
  if (std::any_of(outputs.begin(), outputs.end(), [&](const auto& output) { return output.signal.id >= graph.size(); })) {
    result.reason = "invalid residual output";
    return result;
  }
  const auto before = schedule(graph, outputs, options, work);
  if (!before) {
    return refuse();
  }
  result.report.cost_before = before->cost;
  current                   = compact(graph, outputs, std::vector<bool>(outputs.size(), false), options, work);
  if (!current) {
    return refuse();
  }
  result.report.skipped = before->order.empty();
  for (bool rewriting : {true, false}) {
    if (result.report.skipped || (rewriting ? !options.rewrite : !options.resubstitute)) {
      continue;
    }
    Sweep sweep(current->graph, current->outputs, options, result.report, work, rewriting);
    auto  next = sweep.run();
    if (!next) {
      return refuse();
    }
    if (next->estimated_cost > current->estimated_cost) {
      result.reason = "residual reference accounting failed the total-cost check";
      return result;
    }
    bool depth_ok = true;
    for (size_t i = 0; i < outputs.size(); ++i) {
      depth_ok &= next->graph.node(next->outputs[i].signal.id).level
                  <= uint64_t{graph.node(outputs[i].signal.id).level} + options.depth_slack;
    }
    if (!depth_ok) {
      ++result.report.depth_rejections;
      result.report.rewrite_wins = committed_rewrites;
      result.report.resub_wins   = committed_resubs;
      continue;  // the depth allowance is global, not cumulative per sweep
    }
    for (size_t i = 0; i < next->affected.size(); ++i) {
      next->affected[i] = next->affected[i] || current->affected[i];
    }
    current            = std::move(next);
    committed_rewrites = result.report.rewrite_wins;
    committed_resubs   = result.report.resub_wins;
  }
  result.report.cost_after = current->estimated_cost;
  result.network           = std::move(current);
  result.status            = Status::feasible;
  result.reason            = "residual boundary functions preserved";
  return result;
}

}  // namespace livehd::usyn
