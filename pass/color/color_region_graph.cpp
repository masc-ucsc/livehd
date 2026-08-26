// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_region_graph.hpp"

#include <algorithm>

#include "node_util.hpp"

namespace livehd::color {

namespace {

using livehd::graph_util::bits_of;

// Would pass_partition give this crossing driver a stable (recompile-invariant)
// port name? Mirrors wire_name: a graph input, a user-named pin, or a named
// master node (register/instance). Anything else falls to <op>_<nid>.
[[nodiscard]] bool crossing_is_nameable(const hhds::Pin_class& d) {
  namespace gu = livehd::graph_util;
  return gu::is_graph_input_pin(d) || !gu::pin_name_of(d).empty() || gu::has_name(d.get_master_node());
}

}  // namespace

// The window weighs what ABC will BLAST, not what the region touches: a Sub
// counts ~1 (synthesis_ge_weight). With Sub port bits in the weight,
// a 2-node glue+instance region "weighs" thousands of GE, dodges the min floor
// forever, and XSCore ends up with tens of thousands of zero-logic regions the
// mapper pays call overhead for.
Region_graph::Region_graph(hhds::Graph* g, const Node2Id& node2id, int name_weight) {
  // 1. Components: two same-id nodes joined by a direct edge are one region. This
  //    is split_continuous's rule -- a color that is two disjoint clouds is two
  //    regions to pass.partition, so it must be two vertices here too.
  Union_find uf;
  for (auto n : g->body().nodes(hhds::Node_order::forward)) {
    auto it = node2id.find(n);
    if (it == node2id.end()) {
      continue;
    }
    uf.find(n);  // present even when isolated
    for (const auto& e : n.out_edges()) {
      auto snode = e.sink.get_master_node();
      auto sit   = node2id.find(snode);
      if (sit != node2id.end() && sit->second == it->second) {
        uf.merge(n, snode);
      }
    }
  }

  // 2. Mint dense ids in body().nodes(hhds::Node_order::forward) first-encounter order.
  absl::flat_hash_map<hhds::Node_class, int> root2region;
  for (auto n : g->body().nodes(hhds::Node_order::forward)) {
    if (!node2id.contains(n)) {
      continue;
    }
    auto root = uf.find(n);
    auto it   = root2region.find(root);
    if (it == root2region.end()) {
      it = root2region.emplace(root, static_cast<int>(weight_.size())).first;
      weight_.emplace_back(0);
      alive_.emplace_back(true);
      rep_.emplace_back(static_cast<int>(rep_.size()));
      adj_.emplace_back();
      members_.emplace_back();
    }
    const int r      = it->second;
    node2region_[n]  = r;
    weight_[r]      += synthesis_ge_weight(n);
    members_[r].emplace_back(n);
  }

  // 3. Edges: weight = total driver bits crossing the boundary. Bits, not edge
  //    count -- a 64-bit bus binds two regions far more tightly than a 1-bit
  //    enable, and cutting it costs 64 ports.
  for (auto n : g->body().nodes(hhds::Node_order::forward)) {
    auto it = node2region_.find(n);
    if (it == node2region_.end()) {
      continue;
    }
    const int r = it->second;
    for (const auto& e : n.out_edges()) {
      auto sit = node2region_.find(e.sink.get_master_node());
      if (sit == node2region_.end() || sit->second == r) {
        continue;
      }
      uint64_t bits = static_cast<uint64_t>(std::max(bits_of(e.driver), 1));
      // Name-weight tilt: an anonymous crossing (would mint `<op>_<nid>`) binds
      // name_weight x tighter, so the window prefers to swallow it; a nameable
      // crossing keeps its plain weight and is likelier to survive as a boundary.
      if (name_weight > 1 && !crossing_is_nameable(e.driver)) {
        bits *= static_cast<uint64_t>(name_weight);
      }
      adj_[r][sit->second] += bits;
      adj_[sit->second][r] += bits;
    }
  }
}

Region_graph::Region_graph(std::vector<uint64_t> weights, std::vector<std::vector<hhds::Node_class>> members,
                           std::vector<absl::flat_hash_map<int, uint64_t>> adjacency)
    : weight_(std::move(weights)), adj_(std::move(adjacency)), members_(std::move(members)) {
  alive_.assign(weight_.size(), true);
  rep_.resize(weight_.size());
  for (size_t r = 0; r < rep_.size(); ++r) {
    rep_[r] = static_cast<int>(r);
  }
  adj_.resize(weight_.size());
  members_.resize(weight_.size());
}

// Survivor is the LARGER-degree side (tie: smaller id): folding iterates the
// dissolved side's neighbour map, so the merge costs the SMALLER degree. The old
// smaller-id rule iterated a hub's whole adjacency (a reset/clock cone touches
// tens of thousands of regions) on every second merge into it -- quadratic the
// moment the window makes hub-adjacent singletons mergeable. Still deterministic
// (degree and id are functions of the input graph), and the caller-visible ids
// only feed the final forward_class renumber anyway.
//
// ONE definition, called by BOTH merge() and dissolved_side(): a caller driving
// merges from a priority queue snapshots the dissolved side's adjacency and then
// re-pushes only those pairs, so a survivor rule that disagreed between the two
// would silently DROP every still-valid pair of the side it guessed wrong --
// merges the algorithm was supposed to make, missing, with nothing to observe it.
// A comment cannot hold that invariant; a shared expression does.
int Region_graph::dissolved_side(int a, int b) {
  a = find(a);
  b = find(b);
  if (a == b) {
    return a;
  }
  return (adj_[b].size() > adj_[a].size() || (adj_[b].size() == adj_[a].size() && b < a)) ? a : b;
}

int Region_graph::merge(int a, int b) {
  a = find(a);
  b = find(b);
  if (a == b) {
    return a;
  }
  if (dissolved_side(a, b) == a) {
    std::swap(a, b);  // a survives, b dissolves
  }
  weight_[a] += weight_[b];
  // Append the SHORTER member list (the vectors are freely swappable: nothing
  // after a merge relies on members order beyond determinism, and split_large's
  // MFFC only ever sees pre-merge regions).
  if (members_[b].size() > members_[a].size()) {
    std::swap(members_[a], members_[b]);
  }
  members_[a].insert(members_[a].end(), members_[b].begin(), members_[b].end());
  members_[b].clear();
  members_[b].shrink_to_fit();

  adj_[a].erase(b);
  for (const auto& [nb, w] : adj_[b]) {
    if (nb == a) {
      continue;
    }
    adj_[a][nb] += w;
    adj_[nb].erase(b);
    adj_[nb][a] += w;
  }
  adj_[b].clear();
  alive_[b] = false;
  rep_[b]   = a;
  return a;
}

}  // namespace livehd::color
