// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "xag_balance.hpp"

#include <algorithm>
#include <cstdint>
#include <queue>
#include <unordered_map>

namespace livehd::usyn {
Balance_result balance_xag(const Xag& source, std::span<const Xsignal> outputs, Budget& work, uint32_t max_nodes,
                           uint32_t group_limit, uint32_t duplication_limit) {
  Balance_result result;
  if (!max_nodes || group_limit < 2 || duplication_limit > 1024 || source.size() > max_nodes || outputs.size() > max_nodes) {
    return result;
  }
  if (!work.spend(4 * source.size() + outputs.size())) {
    result.status = Status::search_exhausted;
    return result;
  }
  std::vector<uint64_t> refs(source.size());
  std::vector<bool>     live(source.size());
  std::vector<bool>     protected_root(source.size());
  for (auto output : outputs) {
    if (output.id >= source.size()) {
      return result;
    }
    live[output.id]           = true;
    protected_root[output.id] = true;
    ++refs[output.id];
  }
  for (size_t end = source.size(); end > 0; --end) {
    const auto  id = static_cast<Id>(end - 1);
    const auto& n  = source.node(id);
    if (!live[id] || n.kind == Xag::Kind::source || n.kind == Xag::Kind::constant) {
      continue;
    }
    for (auto input : n.inputs) {
      live[input.id] = true;
      ++refs[input.id];
    }
  }
  // Phase 1, consumers first: expand only group roots that something needs (an
  // output or another root's leaf). A node absorbed into its consumer's group
  // is never rebuilt as a dead root of its own, so a single-fanout chain costs
  // one traversal instead of one group per chain node.
  struct Group {
    std::vector<Xsignal> leaves;  // source-graph signals, translated in phase 2
    bool                 phase = false;
  };
  std::vector<bool>  needed(source.size());
  std::vector<Group> groups(source.size());
  for (auto output : outputs) {
    needed[output.id] = true;
  }
  for (size_t end = source.size(); end > 1; --end) {
    const auto  id   = static_cast<Id>(end - 1);
    const auto& node = source.node(id);
    if (!needed[id] || node.kind == Xag::Kind::source || node.kind == Xag::Kind::constant) {
      continue;
    }
    if (!work.spend()) {
      result.status = Status::search_exhausted;
      return result;
    }
    auto&                                     group = groups[id];
    std::vector<std::pair<Xsignal, uint32_t>> pending{
        {node.inputs[0], 1},
        {node.inputs[1], 1}
    };
    while (!pending.empty()) {
      if (!work.spend() || pending.size() + group.leaves.size() > max_nodes) {
        result.status = Status::search_exhausted;
        return result;
      }
      auto [signal, distance] = pending.back();
      pending.pop_back();
      if (node.kind == Xag::Kind::xor_gate) {
        group.phase     ^= signal.inverted;
        signal.inverted  = false;
      }
      const auto& child       = source.node(signal.id);
      uint32_t    duplication = 0;
      if ((protected_root[id] || refs[id] > 1) && refs[signal.id] > 1 && !protected_root[signal.id]
          && child.level + distance == node.level && child.kind == node.kind && !signal.inverted
          && result.duplications < duplication_limit) {
        std::vector<Id> cone{signal.id};
        while (!cone.empty() && duplication <= 4) {
          if (!work.spend()) {
            result.status = Status::search_exhausted;
            return result;
          }
          const auto gate = cone.back();
          cone.pop_back();
          ++duplication;
          for (auto input : source.node(gate).inputs) {
            if ((!input.inverted || node.kind == Xag::Kind::xor_gate) && source.node(input.id).kind == node.kind
                && refs[input.id] == 1) {
              cone.push_back(input.id);
            }
          }
        }
        if (!cone.empty() || duplication > 4 || duplication > duplication_limit - result.duplications) {
          duplication = 0;
        }
      }
      const bool duplicate = duplication != 0;
      if (child.kind == node.kind && !signal.inverted && (refs[signal.id] == 1 || duplicate)
          && group.leaves.size() + pending.size() + 2 <= group_limit) {
        result.duplications += duplication;
        pending.push_back({child.inputs[0], distance + 1});
        pending.push_back({child.inputs[1], distance + 1});
      } else {
        group.leaves.push_back(signal);
        needed[signal.id] = true;
      }
    }
  }
  // Phase 2, in source order: rebuild every needed root from its translated
  // leaves (lower ids, so already built) by arrival-time pairing.
  Xag                  candidate;
  std::vector<Xsignal> map(source.size());
  const auto           translated = [&](Xsignal signal) { return signal.inverted ? ~map[signal.id] : map[signal.id]; };
  for (Id id = 1; id < source.size(); ++id) {
    if (!work.spend()) {
      result.status = Status::search_exhausted;
      return result;
    }
    const auto& node = source.node(id);
    if (node.kind == Xag::Kind::source) {
      map[id] = candidate.input(source.input_names()[node.source_index]);
      continue;
    }
    if (!needed[id]) {
      continue;
    }
    auto& group = groups[id];
    if (candidate.size() + group.leaves.size() > max_nodes) {
      result.status = Status::search_exhausted;
      return result;
    }
    std::vector<Xsignal> leaves;
    leaves.reserve(group.leaves.size());
    for (auto leaf : group.leaves) {
      leaves.push_back(translated(leaf));
    }
    const auto phase = group.phase;
    std::vector<Xsignal>{}.swap(group.leaves);
    std::sort(leaves.begin(), leaves.end());
    std::vector<Xsignal> terms;
    bool                 contradiction = false;
    for (size_t i = 0; i < leaves.size();) {
      auto j = i + 1;
      while (j < leaves.size() && leaves[j] == leaves[i]) {
        ++j;
      }
      if (node.kind == Xag::Kind::and_gate) {
        if (!terms.empty() && terms.back().id == leaves[i].id) {
          contradiction = true;
          break;
        }
        terms.push_back(leaves[i]);
      } else if ((j - i) & 1) {
        terms.push_back(leaves[i]);
      }
      i = j;
    }
    const auto later = [&](Xsignal a, Xsignal b) {
      const auto al = candidate.node(a.id).level, bl = candidate.node(b.id).level;
      return al == bl ? a > b : al > bl;
    };
    std::priority_queue<Xsignal, std::vector<Xsignal>, decltype(later)> heap(later, std::move(terms));
    if (contradiction) {
      map[id] = candidate.constant(false);
    } else {
      while (heap.size() > 1) {
        if (!work.spend() || candidate.size() >= max_nodes) {
          result.status = Status::search_exhausted;
          return result;
        }
        const auto a = heap.top();
        heap.pop();
        const auto b = heap.top();
        heap.pop();
        heap.push(node.kind == Xag::Kind::and_gate ? candidate.land(a, b) : candidate.lxor(a, b));
      }
      auto root = heap.empty() ? candidate.constant(node.kind == Xag::Kind::and_gate) : heap.top();
      map[id]   = phase ? ~root : root;
    }
    if (leaves.size() > 2) {
      ++result.groups;
    }
  }
  for (auto output : outputs) {
    result.outputs.push_back(translated(output));
  }
  result.graph  = std::move(candidate);
  result.status = Status::feasible;
  return result;
}

std::optional<Mux_match> match_mux(const Xag& graph, Id id) {
  if (id >= graph.size()) {
    return std::nullopt;
  }
  const auto& x = graph.node(id);
  if (x.kind == Xag::Kind::and_gate) {
    // x = ~A & ~B with A = s & t, B = ~s & f (one variable in opposite phases):
    // ~x = A | B = s ? t : f, so x is the complemented mux.
    const auto a = x.inputs[0], b = x.inputs[1];
    if (!a.inverted || !b.inverted || graph.node(a.id).kind != Xag::Kind::and_gate
        || graph.node(b.id).kind != Xag::Kind::and_gate) {
      return std::nullopt;
    }
    const auto& an = graph.node(a.id);
    const auto& bn = graph.node(b.id);
    for (int ai = 0; ai < 2; ++ai) {
      for (int bi = 0; bi < 2; ++bi) {
        const auto s = an.inputs[ai], ns = bn.inputs[bi];
        if (s.id == ns.id && s.inverted != ns.inverted && s.id != 0) {
          return Mux_match{s, an.inputs[1 - ai], bn.inputs[1 - bi], true, 1, 1};
        }
      }
    }
    return std::nullopt;
  }
  if (x.kind != Xag::Kind::xor_gate) {
    return std::nullopt;
  }
  // x = p ^ q, q = s & w, w = u ^ v with u the same node as p. As values:
  // W = P ^ T for T = v ^ (w, u, p edge phases), so x = (s ? T : P) ^ q.inverted.
  for (int qi = 0; qi < 2; ++qi) {
    const auto  p = x.inputs[1 - qi], q = x.inputs[qi];
    const auto& qn = graph.node(q.id);
    if (qn.kind != Xag::Kind::and_gate) {
      continue;
    }
    for (int wi = 0; wi < 2; ++wi) {
      const auto  s = qn.inputs[1 - wi], w = qn.inputs[wi];
      const auto& wn = graph.node(w.id);
      if (wn.kind != Xag::Kind::xor_gate) {
        continue;
      }
      for (int ui = 0; ui < 2; ++ui) {
        const auto u = wn.inputs[ui], v = wn.inputs[1 - ui];
        if (u.id != p.id || s.id == 0) {
          continue;
        }
        Xsignal t   = v;
        t.inverted ^= w.inverted ^ u.inverted ^ p.inverted;
        return Mux_match{s, t, p, q.inverted};
      }
    }
  }
  return std::nullopt;
}

Mux_balance_result balance_mux_chains(const Xag& source, std::span<const Xsignal> outputs, Budget& work, uint32_t max_nodes,
                                      uint32_t min_arms, uint32_t chain_limit) {
  Mux_balance_result result;
  if (!max_nodes || min_arms < 2 || chain_limit < min_arms || source.size() > max_nodes || outputs.size() > max_nodes) {
    return result;
  }
  if (!work.spend(4 * source.size() + outputs.size())) {
    result.status = Status::search_exhausted;
    return result;
  }
  std::vector<uint64_t> refs(source.size());
  std::vector<bool>     live(source.size()), protected_root(source.size());
  for (auto output : outputs) {
    if (output.id >= source.size()) {
      return result;
    }
    live[output.id]           = true;
    protected_root[output.id] = true;
    ++refs[output.id];
  }
  for (size_t end = source.size(); end > 0; --end) {
    const auto  id = static_cast<Id>(end - 1);
    const auto& n  = source.node(id);
    if (!live[id] || n.kind == Xag::Kind::source || n.kind == Xag::Kind::constant) {
      continue;
    }
    for (auto input : n.inputs) {
      live[input.id] = true;
      ++refs[input.id];
    }
  }
  // A nested mux continues the chain only when this chain is its sole reader,
  // including its internal AND and inner XOR: anything else keeps it alive and
  // the rebuilt tree would pay for its logic twice. `uses` is how many edges of
  // the parent mux itself read the arm: f ^ (s & (t ^ f)) reads its else arm
  // twice (outer and inner XOR) and its then arm once.
  const auto owned_mux = [&](Xsignal arm, uint64_t uses) -> std::optional<Mux_match> {
    if (arm.id == 0 || refs[arm.id] != uses || protected_root[arm.id]) {
      return std::nullopt;
    }
    auto m = match_mux(source, arm.id);
    if (!m) {
      return std::nullopt;
    }
    // Both forms keep exactly two internal gates per mux; each must be private.
    const auto& x = source.node(arm.id);
    for (auto in : x.inputs) {
      if (source.node(in.id).kind == Xag::Kind::and_gate) {
        if (refs[in.id] != 1) {
          return std::nullopt;
        }
        if (x.kind == Xag::Kind::xor_gate) {
          for (auto w : source.node(in.id).inputs) {
            if (source.node(w.id).kind == Xag::Kind::xor_gate && refs[w.id] != 1) {
              return std::nullopt;
            }
          }
        }
      }
    }
    if (arm.inverted) {  // ~(s ? t : f) = s ? ~t : ~f
      m->inverted = !m->inverted;
    }
    if (m->inverted) {
      m->when_true  = ~m->when_true;
      m->when_false = ~m->when_false;
      m->inverted   = false;
    }
    return m;
  };
  struct Chain {
    std::vector<std::pair<Xsignal, Xsignal>> arms;  // (condition, value), highest priority first
    Xsignal                                  fallback;
    bool                                     inverted = false;
  };
  std::vector<bool>             needed(source.size());
  std::unordered_map<Id, Chain> chains;  // lookup only; never iterated
  for (auto output : outputs) {
    needed[output.id] = true;
  }
  // Phase 1, consumers first: a needed mux root collects its chain; absorbed
  // nested muxes are never needed on their own (their only reader is the chain).
  for (size_t end = source.size(); end > 1; --end) {
    const auto  id   = static_cast<Id>(end - 1);
    const auto& node = source.node(id);
    if (!needed[id] || node.kind == Xag::Kind::source || node.kind == Xag::Kind::constant) {
      continue;
    }
    if (!work.spend()) {
      result.status = Status::search_exhausted;
      return result;
    }
    Chain chain;
    if (auto root = match_mux(source, id)) {
      chain.inverted = root->inverted;
      auto current   = *root;
      for (;;) {
        if (!work.spend()) {
          result.status = Status::search_exhausted;
          return result;
        }
        if (chain.arms.size() + 1 >= chain_limit) {
          chain.arms.push_back({current.select, current.when_true});
          chain.fallback = current.when_false;
          break;
        }
        if (auto next = owned_mux(current.when_false, current.else_reads)) {
          chain.arms.push_back({current.select, current.when_true});
          current = *next;
          continue;
        }
        if (auto next = owned_mux(current.when_true, current.then_reads)) {  // s ? m : f == ~s ? f : m
          chain.arms.push_back({~current.select, current.when_false});
          current = *next;
          continue;
        }
        chain.arms.push_back({current.select, current.when_true});
        chain.fallback = current.when_false;
        break;
      }
    }
    if (chain.arms.size() >= min_arms) {
      for (const auto& [c, v] : chain.arms) {
        needed[c.id] = needed[v.id] = true;
      }
      needed[chain.fallback.id]  = true;
      result.arms               += chain.arms.size();
      ++result.chains;
      chains.emplace(id, std::move(chain));
    } else {
      for (auto input : node.inputs) {
        needed[input.id] = true;
      }
    }
  }
  // Phase 2, in source order: copy every needed gate, and rebuild each chain
  // root as an order-preserving tree over its (already translated) arms.
  Xag                  candidate;
  std::vector<Xsignal> map(source.size());
  const auto           translated = [&](Xsignal signal) { return signal.inverted ? ~map[signal.id] : map[signal.id]; };
  const auto           level      = [&](Xsignal signal) { return candidate.node(signal.id).level; };
  // Rebuilt muxes use the AND/OR form: three AND gates and two levels from any
  // operand, where Xag::mux's XOR form is three levels deep on the else arm and
  // pays an XOR. Constant and equal arms still fold through land/lor.
  const auto           mux2       = [&](Xsignal s, Xsignal t, Xsignal f) {
    if (t == f) {
      return t;
    }
    return candidate.lor(candidate.land(s, t), candidate.land(~s, f));
  };
  for (Id id = 1; id < source.size(); ++id) {
    if (!work.spend() || candidate.size() >= max_nodes) {
      result.status = Status::search_exhausted;
      return result;
    }
    const auto& node = source.node(id);
    if (node.kind == Xag::Kind::source) {
      map[id] = candidate.input(source.input_names()[node.source_index]);
      continue;
    }
    if (!needed[id]) {
      continue;
    }
    auto found = chains.find(id);
    if (found == chains.end()) {
      const auto a = translated(node.inputs[0]), b = translated(node.inputs[1]);
      map[id] = node.kind == Xag::Kind::and_gate ? candidate.land(a, b) : candidate.lxor(a, b);
      continue;
    }
    struct Item {
      Xsignal  condition, value;
      uint32_t arrival;
    };
    std::vector<Item> items;
    for (const auto& [c, v] : found->second.arms) {
      const auto tc = translated(c), tv = translated(v);
      items.push_back({tc, tv, std::max(level(tc), level(tv))});
    }
    // Greedy alphabetic pairing: merge the adjacent pair that is ready first.
    while (items.size() > 1) {
      size_t   best = 0;
      uint32_t when = UINT32_MAX;
      for (size_t i = 0; i + 1 < items.size(); ++i) {
        if (!work.spend()) {
          result.status = Status::search_exhausted;
          return result;
        }
        const auto ready = std::max(items[i].arrival, items[i + 1].arrival);
        if (ready < when) {
          when = ready;
          best = i;
        }
      }
      if (candidate.size() + 8 > max_nodes) {
        result.status = Status::search_exhausted;
        return result;
      }
      const auto& hi = items[best];
      const auto& lo = items[best + 1];
      const auto  c  = candidate.lor(hi.condition, lo.condition);
      const auto  v  = mux2(hi.condition, hi.value, lo.value);
      items[best]    = {c, v, std::max(level(c), level(v))};
      items.erase(items.begin() + static_cast<std::ptrdiff_t>(best) + 1);
    }
    if (!work.spend(3) || candidate.size() + 3 > max_nodes) {
      result.status = Status::search_exhausted;
      return result;
    }
    const auto root = mux2(items[0].condition, items[0].value, translated(found->second.fallback));
    map[id]         = found->second.inverted ? ~root : root;
  }
  for (auto output : outputs) {
    result.outputs.push_back(translated(output));
  }
  result.graph  = std::move(candidate);
  result.status = Status::feasible;
  return result;
}
}  // namespace livehd::usyn
