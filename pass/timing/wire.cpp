// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "wire.hpp"

#include <algorithm>

namespace livehd::timing::fast {

bool Elmore::solve(std::span<const float> cap_ff, std::span<const Wire_edge> edges, uint32_t root) {
  const size_t n = cap_ff.size();
  root_          = root;
  load_.assign(n, 0.0f);
  delay_.assign(n, 0.0f);
  ldelay_.assign(n, 0.0f);
  beta_.assign(n, 0.0f);
  impulse_.assign(n, 0.0f);
  if (n == 0) {
    return true;
  }
  if (root >= n || edges.size() + 1 != n) {
    return false;  // a connected tree of n nodes has n-1 edges
  }
  // Adjacency in CSR form, one entry per edge end.
  start_.assign(n + 1, 0);
  for (const auto& e : edges) {
    if (e.from >= n || e.to >= n) {
      return false;
    }
    ++start_[e.from + 1];
    ++start_[e.to + 1];
  }
  for (size_t i = 0; i < n; ++i) {
    start_[i + 1] += start_[i];
  }
  adjacent_edge_.resize(start_[n]);
  {
    // Fill with a moving cursor kept in order_ (reused as scratch before the walk).
    order_.assign(start_.begin(), start_.end() - 1);
    for (uint32_t i = 0; i < edges.size(); ++i) {
      adjacent_edge_[order_[edges[i].from]++] = i;
      adjacent_edge_[order_[edges[i].to]++]   = i;
    }
  }
  // Breadth-first from the root: parents come before children in order_. The edge a node was reached
  // by is skipped when scanning it; any other edge to a node already reached is a cycle.
  constexpr uint32_t unseen = ~0u;
  parent_.assign(n, unseen);
  parent_res_.assign(n, 0.0f);
  parent_edge_.assign(n, unseen);
  order_.clear();
  order_.push_back(root);
  parent_[root] = root;
  for (size_t head = 0; head < order_.size(); ++head) {
    const uint32_t node = order_[head];
    for (uint32_t k = start_[node]; k < start_[node + 1]; ++k) {
      const uint32_t index = adjacent_edge_[k];
      if (index == parent_edge_[node]) {
        continue;
      }
      const auto&    e     = edges[index];
      const uint32_t other = e.from == node ? e.to : e.from;
      if (parent_[other] != unseen) {
        return false;  // reached twice: a cycle
      }
      parent_[other]      = node;
      parent_res_[other]  = e.resistance_ohm;
      parent_edge_[other] = index;
      order_.push_back(other);
    }
  }
  if (order_.size() != n) {
    return false;
  }
  constexpr float to_kohm = 1e-3f;  // Ω * fF -> ps needs the resistance in kΩ
  // Upstream to downstream: loads, bottom-up.
  for (size_t i = n; i-- > 0;) {
    const uint32_t node  = order_[i];
    load_[node]         += cap_ff[node];
    if (node != root) {
      load_[parent_[node]] += load_[node];
    }
  }
  // Top-down: delay; then the load-weighted delay bottom-up; then beta top-down.
  for (size_t i = 1; i < n; ++i) {
    const uint32_t node = order_[i];
    delay_[node]        = delay_[parent_[node]] + parent_res_[node] * to_kohm * load_[node];
  }
  for (size_t i = n; i-- > 0;) {
    const uint32_t node  = order_[i];
    ldelay_[node]       += cap_ff[node] * delay_[node];
    if (node != root) {
      ldelay_[parent_[node]] += ldelay_[node];
    }
  }
  for (size_t i = 1; i < n; ++i) {
    const uint32_t node = order_[i];
    beta_[node]         = beta_[parent_[node]] + parent_res_[node] * to_kohm * ldelay_[node];
  }
  for (size_t i = 0; i < n; ++i) {
    impulse_[i] = std::max(0.0f, 2.0f * beta_[i] - delay_[i] * delay_[i]);
  }
  return true;
}

}  // namespace livehd::timing::fast
