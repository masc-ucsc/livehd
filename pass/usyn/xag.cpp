// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_set>

#include "function.hpp"

namespace livehd::usyn {

Xag::Xag() : nodes(1) {}

size_t Xag::Hash::operator()(const Key& k) const {
  size_t     h   = static_cast<size_t>(k.kind);
  const auto mix = [&](size_t v) { h ^= v + 0x9e3779b9U + (h << 6) + (h >> 2); };
  mix(k.a.id);
  mix(k.a.inverted);
  mix(k.b.id);
  mix(k.b.inverted);
  return h;
}

void Xag::check(Xsignal s) const {
  if (s.id >= nodes.size()) {
    throw std::invalid_argument("XAG signal is not in this graph");
  }
}

Xsignal Xag::input(std::string name) {
  if (nodes.size() >= std::numeric_limits<Id>::max()) {
    throw std::length_error("XAG node limit");
  }
  const auto id = static_cast<Id>(nodes.size());
  Node       n;
  n.kind         = Kind::source;
  n.source_index = static_cast<uint32_t>(names.size());
  nodes.push_back(n);
  names.push_back(std::move(name));
  return {id};
}

Xsignal Xag::intern(Kind kind, Xsignal a, Xsignal b) {
  if (b < a) {
    std::swap(a, b);
  }
  Key key{kind, a, b};
  if (auto it = index.find(key); it != index.end()) {
    return {it->second};
  }
  if (nodes.size() >= std::numeric_limits<Id>::max()) {
    throw std::length_error("XAG node limit");
  }
  const auto id = static_cast<Id>(nodes.size());
  Node       n;
  n.kind   = kind;
  n.inputs = {a, b};
  n.level  = 1 + std::max(nodes[a.id].level, nodes[b.id].level);
  nodes.push_back(n);
  ++nodes[a.id].fanouts;
  ++nodes[b.id].fanouts;
  index.emplace(key, id);
  return {id};
}

Xsignal Xag::land(Xsignal a, Xsignal b) {
  check(a);
  check(b);
  if (a.id == 0) {
    return a.inverted ? b : a;
  }
  if (b.id == 0) {
    return b.inverted ? a : b;
  }
  if (a.id == b.id) {
    return a.inverted == b.inverted ? a : constant(false);
  }
  return intern(Kind::and_gate, a, b);
}

Xsignal Xag::lxor(Xsignal a, Xsignal b) {
  check(a);
  check(b);
  const bool inverted = a.inverted != b.inverted;
  a.inverted = b.inverted = false;
  if (a.id == b.id) {
    return constant(inverted);
  }
  if (a.id == 0 || b.id == 0) {
    return {a.id ? a.id : b.id, inverted};
  }
  auto s     = intern(Kind::xor_gate, a, b);
  s.inverted = inverted;
  return s;
}

Xsignal Xag::mux(Xsignal s, Xsignal t, Xsignal f) {
  check(s);
  check(t);
  check(f);
  if (s.id == 0) {
    return s.inverted ? t : f;
  }
  if (t == f) {
    return t;
  }
  if (t == ~f) {
    return lxor(s, f);
  }
  if (t.id == 0) {
    return t.inverted ? lor(s, f) : land(~s, f);
  }
  if (f.id == 0) {
    return f.inverted ? lor(~s, t) : land(s, t);
  }
  // XOR form retains the datapath's XOR structure and the equality shortcut.
  return lxor(f, land(s, lxor(t, f)));
}

namespace {
bool valid_limits(const Window_limits& limits) {
  return limits.inputs > 0 && limits.inputs <= max_logical_inputs && limits.nodes > 0;
}

Xag_window collect(const Xag& g, Xsignal root, std::span<const Id> leaves, const Window_limits& limits, Budget& work, bool whole) {
  Xag_window out;
  out.root = root;
  if (!valid_limits(limits) || root.id >= g.size() || leaves.size() > limits.inputs) {
    out.reason = "invalid window request";
    return out;
  }
  std::unordered_set<Id> boundary;
  for (auto id : leaves) {
    if (id == 0 || id >= g.size() || !boundary.insert(id).second) {
      out.reason = "invalid or duplicate window leaf";
      return out;
    }
  }
  out.leaves.assign(leaves.begin(), leaves.end());
  std::unordered_set<Id> visited;
  std::vector<Id>        todo{root.id};
  out.status = Status::search_exhausted;
  while (!todo.empty()) {
    if (!work.spend()) {
      out.reason = "window work budget";
      return out;
    }
    const auto id = todo.back();
    todo.pop_back();
    if (id == 0 || visited.contains(id)) {
      continue;
    }
    if (visited.size() >= limits.nodes) {
      out.reason = "window node budget";
      return out;
    }
    visited.insert(id);
    if (boundary.contains(id)) {
      continue;
    }
    const auto& n = g.node(id);
    if (n.kind == Xag::Kind::source) {
      if (!whole) {
        out.status = Status::invalid;
        out.reason = "cut does not cover a source path";
        return out;
      }
      if (out.leaves.size() >= limits.inputs) {
        out.reason = "whole-cone analysis support budget";
        return out;
      }
      out.leaves.push_back(id);
    } else {
      out.interior.push_back(id);
      todo.push_back(n.inputs[0].id);
      todo.push_back(n.inputs[1].id);
    }
  }
  if (std::any_of(leaves.begin(), leaves.end(), [&](auto id) { return !visited.contains(id); })) {
    out.status = Status::invalid;
    out.reason = "unreachable cut leaf";
    return out;
  }
  if (whole) {
    std::sort(out.leaves.begin(), out.leaves.end());
  }
  std::sort(out.interior.begin(), out.interior.end());
  out.whole_cone
      = whole || std::all_of(out.leaves.begin(), out.leaves.end(), [&](auto id) { return g.node(id).kind == Xag::Kind::source; });
  out.status = Status::feasible;
  return out;
}
}  // namespace

Xag_window collect_window(const Xag& g, Xsignal root, std::span<const Id> leaves, const Window_limits& limits, Budget& work) {
  return collect(g, root, leaves, limits, work, false);
}

Xag_window whole_cone(const Xag& g, Xsignal root, const Window_limits& limits, Budget& work) {
  return collect(g, root, {}, limits, work, true);
}

Xag_window collect_subwindow(const Xag& g, Xsignal root, std::span<const Id> basis, const Window_limits& limits, Budget& work) {
  Xag_window out;
  out.root = root;
  if (!valid_limits(limits) || basis.size() > limits.inputs || root.id >= g.size()) {
    out.reason = "invalid divisor basis";
    return out;
  }
  std::unordered_set<Id> stops;
  for (auto id : basis) {
    if (!id || id >= g.size() || !stops.insert(id).second) {
      out.reason = "invalid or duplicate divisor basis signal";
      return out;
    }
  }
  std::unordered_set<Id> visited;
  std::vector<Id>        todo{root.id}, leaves;
  while (!todo.empty()) {
    if (!work.spend()) {
      out.status = Status::search_exhausted;
      out.reason = "divisor traversal budget";
      return out;
    }
    const auto id = todo.back();
    todo.pop_back();
    if (!id || visited.contains(id)) {
      continue;
    }
    if (visited.size() >= limits.nodes) {
      out.status = Status::search_exhausted;
      out.reason = "divisor traversal node budget";
      return out;
    }
    visited.insert(id);
    if (stops.contains(id)) {
      leaves.push_back(id);
      continue;
    }
    const auto& node = g.node(id);
    if (node.kind == Xag::Kind::source) {
      out.reason = "divisor depends on a source outside its basis";
      return out;
    }
    todo.push_back(node.inputs[0].id);
    todo.push_back(node.inputs[1].id);
  }
  std::sort(leaves.begin(), leaves.end());
  return collect_window(g, root, leaves, limits, work);
}

Window_function lift_function(const Truth_table& table, std::span<const Id> inputs, std::span<const Id> basis, Budget& work) {
  Window_function out;
  if (!valid_truth_table(table) || inputs.size() != table.inputs || basis.size() > max_logical_inputs) {
    return out;
  }
  std::unordered_set<Id> unique;
  for (auto id : basis) {
    if (!id || !unique.insert(id).second) {
      return out;
    }
  }
  unique.clear();
  std::vector<uint32_t> positions;
  for (auto id : inputs) {
    const auto found = std::find(basis.begin(), basis.end(), id);
    if (!unique.insert(id).second || found == basis.end()) {
      return out;
    }
    positions.push_back(static_cast<uint32_t>(found - basis.begin()));
  }
  out.status = Status::search_exhausted;
  out.table  = Truth_table(static_cast<uint32_t>(basis.size()));
  for (uint32_t x = 0; x < (1U << basis.size()); ++x) {
    if (!work.spend(positions.size() + 1)) {
      return out;
    }
    uint32_t input = 0;
    for (uint32_t j = 0; j < positions.size(); ++j) {
      input |= ((x >> positions[j]) & 1) << j;
    }
    out.table.set(x, table.get(input));
  }
  out.status = Status::feasible;
  return out;
}

Window_function basis_function(const Xag& g, Xsignal root, std::span<const Id> basis, const Window_limits& limits, Budget& work) {
  const auto window = collect_subwindow(g, root, basis, limits, work);
  if (window.status != Status::feasible) {
    Window_function out;
    out.status = window.status;
    return out;
  }
  auto function = window_function(g, window, work);
  if (function.status != Status::feasible) {
    return function;
  }
  return lift_function(function.table, window.leaves, basis, work);
}

Xag_window grow_window(const Xag& g, Xsignal root, const Window_limits& limits, Budget& work) {
  Xag_window failure;
  failure.root = root;
  if (!valid_limits(limits) || root.id >= g.size()) {
    failure.reason = "invalid window request";
    return failure;
  }
  if (root.id == 0) {
    return collect_window(g, root, {}, limits, work);
  }
  std::vector<Id>        leaves{root.id};
  std::unordered_set<Id> seen{root.id};
  while (true) {
    size_t   best      = leaves.size();
    uint32_t best_cost = 3;
    for (size_t i = 0; i < leaves.size(); ++i) {
      if (!work.spend()) {
        failure.status = Status::search_exhausted;
        failure.reason = "window growth work budget";
        return failure;
      }
      const auto& n = g.node(leaves[i]);
      if (n.kind == Xag::Kind::source) {
        continue;
      }
      uint32_t cost = 0;
      for (auto s : n.inputs) {
        cost += s.id != 0 && !seen.contains(s.id);
      }
      if (leaves.size() - 1 + cost > limits.inputs || seen.size() + cost > limits.nodes) {
        continue;
      }
      if (cost < best_cost || (cost == best_cost && leaves[i] > leaves[best])) {
        best      = i;
        best_cost = cost;
      }
    }
    if (best == leaves.size()) {
      break;
    }
    const auto id = leaves[best];
    leaves.erase(leaves.begin() + static_cast<ptrdiff_t>(best));
    for (auto s : g.node(id).inputs) {
      if (s.id && seen.insert(s.id).second) {
        leaves.push_back(s.id);
      }
    }
  }
  std::sort(leaves.begin(), leaves.end());
  return collect_window(g, root, leaves, limits, work);
}

Priority_windows priority_windows(const Xag& g, Xsignal root, const Window_limits& limits, Budget& work, uint32_t max_cuts) {
  Priority_windows result;
  if (!valid_limits(limits) || root.id >= g.size() || !max_cuts || max_cuts > 32) {
    return result;
  }
  auto seed = grow_window(g, root, limits, work);
  if (seed.status != Status::feasible) {
    result.status    = seed.status;
    result.exhausted = seed.status == Status::search_exhausted;
    return result;
  }
  result.windows.push_back(seed);
  if (max_cuts == 1 || root.id == 0 || g.node(root.id).kind == Xag::Kind::source) {
    result.status = Status::feasible;
    return result;
  }
  std::vector<Id> initial;
  for (auto input : g.node(root.id).inputs) {
    if (input.id) {
      initial.push_back(input.id);
    }
  }
  std::sort(initial.begin(), initial.end());
  initial.erase(std::unique(initial.begin(), initial.end()), initial.end());
  std::vector<std::vector<Id>> frontiers{initial};
  std::set<std::vector<Id>>    seen{initial};
  std::vector<Xag_window>      alternatives;
  for (size_t cursor = 0; cursor < frontiers.size(); ++cursor) {
    if (!work.spend(frontiers[cursor].size() + 1)) {
      result.exhausted = true;
      break;
    }
    const auto basis = frontiers[cursor];  // expanding may reallocate the queue
    if (basis != seed.leaves) {
      auto window = collect_window(g, root, basis, limits, work);
      if (window.status == Status::feasible) {
        alternatives.push_back(std::move(window));
      } else if (window.status == Status::search_exhausted) {
        result.exhausted = true;
      }
    }
    if (work.exhausted) {
      break;
    }
    for (size_t i = 0; i < basis.size(); ++i) {
      const auto& node = g.node(basis[i]);
      if (node.kind == Xag::Kind::source) {
        continue;
      }
      auto next = basis;
      next.erase(next.begin() + static_cast<ptrdiff_t>(i));
      for (auto input : node.inputs) {
        if (input.id) {
          next.push_back(input.id);
        }
      }
      std::sort(next.begin(), next.end());
      next.erase(std::unique(next.begin(), next.end()), next.end());
      if (next.size() > limits.inputs || seen.contains(next)) {
        continue;
      }
      if (frontiers.size() >= 128) {
        result.exhausted = true;
        continue;
      }
      seen.insert(next);
      frontiers.push_back(std::move(next));
    }
  }
  std::sort(alternatives.begin(), alternatives.end(), [](const auto& a, const auto& b) {
    if (a.interior.size() != b.interior.size()) {
      return a.interior.size() > b.interior.size();
    }
    if (a.leaves.size() != b.leaves.size()) {
      return a.leaves.size() < b.leaves.size();
    }
    return a.leaves < b.leaves;
  });
  for (auto& window : alternatives) {
    if (result.windows.size() >= max_cuts) {
      break;
    }
    result.windows.push_back(std::move(window));
  }
  result.status = Status::feasible;
  return result;
}

Window_function window_function(const Xag& g, const Xag_window& w, Budget& work) {
  Window_function out;
  if (w.status != Status::feasible || w.leaves.size() > max_logical_inputs
      || w.interior.size() + w.leaves.size() >= std::numeric_limits<uint32_t>::max()) {
    return out;
  }
  // Validate instead of indexing arbitrary serialized node/leaf vectors.
  const Window_limits limits{std::max(1U, static_cast<uint32_t>(w.leaves.size())),
                             std::max(1U, static_cast<uint32_t>(w.interior.size() + w.leaves.size()))};
  const auto          checked = collect_window(g, w.root, w.leaves, limits, work);
  if (checked.status != Status::feasible || checked.interior != w.interior) {
    out.status = checked.status == Status::search_exhausted ? checked.status : Status::invalid;
    return out;
  }
  out.table = Truth_table(static_cast<uint32_t>(w.leaves.size()));
  std::unordered_map<Id, uint32_t> position{
      {0, 0}
  };
  for (auto id : w.leaves) {
    position.emplace(id, static_cast<uint32_t>(position.size()));
  }
  for (auto id : w.interior) {
    position.emplace(id, static_cast<uint32_t>(position.size()));
  }
  std::vector<uint64_t>             values(position.size());
  constexpr std::array<uint64_t, 6> variable{0xaaaaaaaaaaaaaaaaULL,
                                             0xccccccccccccccccULL,
                                             0xf0f0f0f0f0f0f0f0ULL,
                                             0xff00ff00ff00ff00ULL,
                                             0xffff0000ffff0000ULL,
                                             0xffffffff00000000ULL};
  for (size_t word = 0; word < out.table.words.size(); ++word) {
    if (!work.spend(values.size())) {
      out.status = Status::search_exhausted;
      return out;
    }
    for (uint32_t j = 0; j < w.leaves.size(); ++j) {
      values[j + 1] = j < 6 ? variable[j] : ((word >> (j - 6)) & 1) ? ~uint64_t{0} : 0;
    }
    const auto read = [&](Xsignal s) {
      const auto value = values[position.at(s.id)];
      return s.inverted ? ~value : value;
    };
    for (auto id : w.interior) {
      const auto& n = g.node(id);
      const auto  a = read(n.inputs[0]), b = read(n.inputs[1]);
      values[position.at(id)] = n.kind == Xag::Kind::and_gate ? a & b : a ^ b;
    }
    out.table.words[word] = read(w.root);
  }
  const auto bits = 1U << out.table.inputs;
  if (bits < 64) {
    out.table.words[0] &= (uint64_t{1} << bits) - 1;
  }
  out.status = Status::feasible;
  return out;
}

}  // namespace livehd::usyn
