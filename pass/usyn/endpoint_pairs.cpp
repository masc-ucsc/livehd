// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "endpoint_pairs.hpp"

#include <algorithm>
#include <bit>
#include <set>

namespace livehd::usyn {
namespace {
struct Labels {
  std::array<uint32_t, 2> endpoint{};
  uint8_t                 size = 0;
  void                    add(uint32_t id) {
    if (size == 3 || (size > 0 && endpoint[0] == id) || (size > 1 && endpoint[1] == id)) {
      return;
    }
    if (size < 2) {
      endpoint[size] = id;
    }
    ++size;
    if (size == 2 && endpoint[0] > endpoint[1]) {
      std::swap(endpoint[0], endpoint[1]);
    }
  }
  void merge(const Labels& other) {
    if (other.size == 3) {
      size = 3;
    } else {
      for (uint8_t i = 0; i < other.size; ++i) {
        add(other.endpoint[i]);
      }
    }
  }
};
}  // namespace

Endpoint_pairs find_endpoint_pairs(const Xag& graph, std::span<const Pair_endpoint> endpoints, uint32_t max_pairs,
                                   uint32_t max_nodes, Budget& work, bool shared_source_pairs) {
  Endpoint_pairs result;
  if (!max_nodes || !max_pairs || max_pairs > 4096) {
    return result;
  }
  const auto exhausted = [&] {
    result.status = Status::search_exhausted;
    result.pairs.clear();
    return std::move(result);
  };
  if (graph.size() > max_nodes || endpoints.size() > max_nodes || !work.spend(graph.size() + endpoints.size())) {
    return exhausted();
  }
  std::vector<Labels> labels(graph.size());
  for (uint32_t i = 0; i < endpoints.size(); ++i) {
    for (const auto input : endpoints[i].inputs) {
      if (!work.spend()) {
        return exhausted();
      }
      if (input.id >= graph.size()) {
        return result;
      }
      labels[input.id].add(i);
    }
  }
  std::set<std::array<uint32_t, 2>> seen;
  std::set<std::array<uint32_t, 2>> source_hits;
  for (size_t id = graph.size(); id-- > 1;) {
    if (!work.spend(1 + (shared_source_pairs ? 4 : 1) * std::bit_width(max_pairs))) {
      return exhausted();
    }
    const auto& node = graph.node(static_cast<Id>(id));
    if (node.kind == Xag::Kind::source) {
      const auto& l = labels[id];
      if (shared_source_pairs && l.size == 2 && !seen.contains(l.endpoint)) {
        const auto a = l.endpoint[0], b = l.endpoint[1];
        if (endpoints[a].domain == unknown_clock_domain || endpoints[a].domain != endpoints[b].domain) {
          ++result.domain_skips;
        } else if (auto hit = source_hits.find(l.endpoint); hit != source_hits.end()) {
          // Two distinct independent sources shared by exactly this pair can
          // support a new divisor even when factoring left no common gate.
          source_hits.erase(hit);
          if (seen.size() == max_pairs) {
            result.capped = true;
          } else {
            seen.insert(l.endpoint);
            result.pairs.push_back(l.endpoint);
            ++result.source_pairs;
          }
        } else if (source_hits.size() == max_pairs) {
          result.capped = true;
        } else {
          source_hits.insert(l.endpoint);
        }
      }
      continue;
    }
    const auto& l = labels[id];
    if (l.size == 3) {
      ++result.more_than_two;
    } else if (l.size == 2) {
      const auto a = l.endpoint[0], b = l.endpoint[1];
      if (endpoints[a].domain == unknown_clock_domain || endpoints[a].domain != endpoints[b].domain) {
        ++result.domain_skips;
      } else {
        ++result.shared_nodes;
        if (!seen.contains(l.endpoint)) {
          if (seen.size() == max_pairs) {
            result.capped = true;
          } else {
            seen.insert(l.endpoint);
            result.pairs.push_back(l.endpoint);
          }
        }
      }
    }
    for (const auto input : node.inputs) {
      labels[input.id].merge(l);
    }
  }
  result.status = Status::feasible;
  return result;
}

Pair_windows grow_pair_windows(const Xag& graph, std::array<Xsignal, 2> roots, const Window_limits& endpoint_limits,
                               uint32_t joint_inputs, Budget& work) {
  Pair_windows result;
  if (!joint_inputs || joint_inputs > max_logical_inputs || !endpoint_limits.inputs || endpoint_limits.inputs > max_logical_inputs
      || !endpoint_limits.nodes || roots[0].id >= graph.size() || roots[1].id >= graph.size()) {
    result.reason = "invalid pair window request";
    return result;
  }
  const auto exhausted = [&] {
    if (result.status != Status::feasible) {
      result.status = Status::search_exhausted;
    }
    result.exhausted = true;
    result.reason    = "pair window growth budget";
    return std::move(result);
  };
  std::vector<Id> boundary;
  for (auto root : roots) {
    if (root.id && std::find(boundary.begin(), boundary.end(), root.id) == boundary.end()) {
      boundary.push_back(root.id);
    }
  }
  std::sort(boundary.begin(), boundary.end());
  const auto admit = [&](std::span<const Id> leaves) -> std::optional<Pair_windows> {
    if (leaves.size() > joint_inputs || !work.spend(8 * (leaves.size() + 1))) {
      return {};
    }
    Pair_windows        candidate;
    uint64_t            nodes = 0;
    const Window_limits limits{joint_inputs, endpoint_limits.nodes};
    for (size_t i = 0; i < roots.size(); ++i) {
      candidate.windows[i]  = collect_subwindow(graph, roots[i], leaves, limits, work);
      const auto& window    = candidate.windows[i];
      nodes                += window.interior.size();
      if (window.status != Status::feasible || window.leaves.size() > endpoint_limits.inputs || nodes > endpoint_limits.nodes) {
        return {};
      }
    }
    for (const auto& window : candidate.windows) {
      candidate.basis.insert(candidate.basis.end(), window.leaves.begin(), window.leaves.end());
    }
    std::sort(candidate.basis.begin(), candidate.basis.end());
    candidate.basis.erase(std::unique(candidate.basis.begin(), candidate.basis.end()), candidate.basis.end());
    candidate.status = Status::feasible;
    return candidate;
  };
  auto initial = admit(boundary);
  if (!initial) {
    return exhausted();
  }
  result = std::move(*initial);
  // Every accepted move expands one boundary node. Try fewer newly exposed
  // signals first, then stable reverse-topological order. At most 16 proposals
  // per round, with each traversal charged to the caller's pair work budget.
  while (!work.exhausted) {
    std::optional<Pair_windows> best;
    Id                          best_id = 0;
    for (auto id : result.basis) {
      if (!work.spend(result.basis.size() + 3)) {
        return exhausted();
      }
      const auto& node = graph.node(id);
      if (node.kind == Xag::Kind::source) {
        continue;
      }
      auto leaves = result.basis;
      leaves.erase(std::lower_bound(leaves.begin(), leaves.end(), id));
      for (const auto input : node.inputs) {
        if (input.id && std::find(leaves.begin(), leaves.end(), input.id) == leaves.end()) {
          leaves.push_back(input.id);
        }
      }
      if (leaves.size() > joint_inputs) {
        continue;
      }
      std::sort(leaves.begin(), leaves.end());
      auto candidate = admit(leaves);
      if (candidate
          && (!best || candidate->basis.size() < best->basis.size()
              || (candidate->basis.size() == best->basis.size() && id > best_id))) {
        best    = std::move(candidate);
        best_id = id;
      }
    }
    if (work.exhausted) {
      return exhausted();
    }
    if (!best) {
      return result;
    }
    result = std::move(*best);
  }
  return exhausted();
}

Pair_windows fanout_free_pair_windows(const Xag& graph, std::array<Xsignal, 2> roots, std::span<const Xsignal> outside_roots,
                                      const Window_limits& endpoint_limits, uint32_t joint_inputs, uint32_t max_nodes,
                                      Budget& work) {
  Pair_windows result;
  if (!joint_inputs || joint_inputs > max_logical_inputs || !endpoint_limits.inputs || endpoint_limits.inputs > max_logical_inputs
      || !endpoint_limits.nodes || !max_nodes || roots[0].id >= graph.size() || roots[1].id >= graph.size()) {
    result.reason = "invalid fanout-free pair window request";
    return result;
  }
  const auto refuse = [&](std::string reason) {
    Pair_windows refused;
    refused.status    = Status::search_exhausted;
    refused.exhausted = true;
    refused.reason    = std::move(reason);
    return refused;
  };
  if (graph.size() > max_nodes || outside_roots.size() > max_nodes || !work.spend(2 * graph.size() + outside_roots.size())) {
    return refuse("fanout-free pair traversal budget");
  }
  std::vector<uint8_t> outside(graph.size()), seen(graph.size());
  std::vector<Id>      pending;
  const auto           push = [&](Id id, std::vector<uint8_t>& visited) {
    if (id && !visited[id]) {
      visited[id] = true;
      pending.push_back(id);
    }
  };
  for (auto signal : outside_roots) {
    if (!work.spend()) {
      return refuse("fanout-free pair outside-root budget");
    }
    if (signal.id >= graph.size()) {
      result.reason = "invalid fanout-free pair outside root";
      return result;
    }
    push(signal.id, outside);
  }
  while (!pending.empty()) {
    if (!work.spend(3)) {
      return refuse("fanout-free pair outside-cone budget");
    }
    const auto id = pending.back();
    pending.pop_back();
    const auto& node = graph.node(id);
    if (node.kind != Xag::Kind::source) {
      for (auto input : node.inputs) {
        push(input.id, outside);
      }
    }
  }
  for (auto root : roots) {
    push(root.id, seen);
  }
  uint64_t interior = 0;
  while (!pending.empty()) {
    if (!work.spend(3)) {
      return refuse("fanout-free pair interior budget");
    }
    const auto id = pending.back();
    pending.pop_back();
    const auto& node = graph.node(id);
    if (outside[id] || node.kind == Xag::Kind::source) {
      result.basis.push_back(id);
      if (outside[id] && node.kind != Xag::Kind::source) {
        result.outside_boundary.push_back(id);
      }
      if (result.basis.size() > joint_inputs) {
        return refuse("fanout-free pair joint support limit");
      }
    } else {
      if (++interior > endpoint_limits.nodes) {
        return refuse("fanout-free pair node limit");
      }
      for (auto input : node.inputs) {
        push(input.id, seen);
      }
    }
  }
  if (!work.spend(8 * (result.basis.size() + 1))) {
    return refuse("fanout-free pair basis budget");
  }
  std::sort(result.basis.begin(), result.basis.end());
  std::sort(result.outside_boundary.begin(), result.outside_boundary.end());
  uint64_t nodes = 0;
  for (size_t i = 0; i < roots.size(); ++i) {
    result.windows[i]  = collect_subwindow(graph, roots[i], result.basis, {joint_inputs, endpoint_limits.nodes}, work);
    nodes             += result.windows[i].interior.size();
    if (result.windows[i].status != Status::feasible || result.windows[i].leaves.size() > endpoint_limits.inputs
        || nodes > endpoint_limits.nodes) {
      return refuse("fanout-free pair endpoint window limit");
    }
  }
  result.status = Status::feasible;
  return result;
}

Pair_dependents pair_dependents(const Xag& graph, std::span<const Xsignal> changed_inputs, std::span<const Pair_endpoint> endpoints,
                                uint32_t max_nodes, Budget& work) {
  Pair_dependents result;
  if (!max_nodes) {
    return result;
  }
  const auto exhausted = [&] {
    result.status = Status::search_exhausted;
    result.endpoints.clear();
    return std::move(result);
  };
  if (graph.size() > max_nodes || endpoints.size() > max_nodes || changed_inputs.size() > max_nodes
      || !work.spend(2 * graph.size() + endpoints.size() + changed_inputs.size())) {
    return exhausted();
  }
  std::vector<uint8_t> visited(graph.size()), affected(graph.size());
  std::vector<Xsignal> pending(changed_inputs.begin(), changed_inputs.end());
  while (!pending.empty()) {
    if (!work.spend()) {
      return exhausted();
    }
    const auto signal = pending.back();
    pending.pop_back();
    if (signal.id >= graph.size()) {
      return result;
    }
    if (signal.id == 0) {
      continue;
    }
    const auto& node = graph.node(signal.id);
    if (node.kind == Xag::Kind::source) {
      affected[signal.id] |= signal.inverted;
    } else if (!visited[signal.id]) {
      visited[signal.id] = affected[signal.id] = true;
      pending.insert(pending.end(), node.inputs.begin(), node.inputs.end());
    }
  }
  // A reader of a marked gate can change the deletion/inversion credit of a
  // later pair even when it is not itself one of the just-reselected endpoints.
  for (Id id = 1; id < graph.size(); ++id) {
    if (!work.spend()) {
      return exhausted();
    }
    const auto& node = graph.node(id);
    if (node.kind != Xag::Kind::source) {
      affected[id] |= affected[node.inputs[0].id] | affected[node.inputs[1].id];
    }
  }
  result.endpoints.resize(endpoints.size());
  for (size_t i = 0; i < endpoints.size(); ++i) {
    for (const auto input : endpoints[i].inputs) {
      if (!work.spend()) {
        return exhausted();
      }
      if (input.id >= graph.size()) {
        result.endpoints.clear();
        return result;
      }
      result.endpoints[i] |= affected[input.id];
    }
  }
  result.status = Status::feasible;
  return result;
}

Status Pair_queue::refresh(std::span<const Endpoint_pair> candidates, std::span<const uint8_t> affected, Budget& work) {
  if (!capacity || capacity > 4096 || !trial_limit || trial_limit > 4096) {
    return Status::invalid;
  }
  if (candidates.size() > capacity
      || !work.spend((pending.size() + candidates.size()) * (2 + std::bit_width(capacity) + std::bit_width(trial_limit)))) {
    stats.exhausted = true;
    return Status::search_exhausted;
  }
  for (const auto& pair : candidates) {
    if (pair[0] >= pair[1] || (!affected.empty() && pair[1] >= affected.size())) {
      return Status::invalid;
    }
  }
  const std::set<Endpoint_pair> queued(pending.begin(), pending.end()), live(candidates.begin(), candidates.end());
  if (live.size() != candidates.size()) {
    return Status::invalid;
  }
  for (const auto& pair : pending) {
    stats.stale_skips += !live.contains(pair);
  }
  pending.clear();
  for (const auto& pair : candidates) {
    const bool tried = attempted.contains(pair), waiting = queued.contains(pair);
    const bool changed = !affected.empty() && (affected[pair[0]] || affected[pair[1]]);
    if (!tried || waiting || changed) {
      stats.requeues += tried && !waiting;
      pending.push_back(pair);
    }
  }
  return Status::feasible;
}

std::optional<Endpoint_pair> Pair_queue::pop(Budget& work) {
  if (pending.empty()) {
    return {};
  }
  if (stats.trials == trial_limit || !work.spend(1 + std::bit_width(trial_limit))) {
    stats.exhausted = true;
    return {};
  }
  const auto pair = pending.front();
  pending.pop_front();
  attempted.insert(pair);
  ++stats.trials;
  return pair;
}

}  // namespace livehd::usyn
