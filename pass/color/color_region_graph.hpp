// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The REGION GRAPH: one vertex per region, one weighted edge per adjacent pair,
// plus the union-find merge that keeps a fold O(neighbours) instead of
// O(nodes).
//
// Two producers share it and must keep sharing it, because they share the merge:
//   * the size window (color_size.cpp) builds it FROM a graph -- vertices are
//     the connected components of equal-id nodes, weights are synthesis GE, and
//     edge weights are the driver bits crossing the boundary;
//   * cones coloring (color_synth_cones.cpp) INJECTS the vertices it already
//     computed -- one per cone color, weighted in predicted AIG size, adjacency
//     from the walk's shared-sub-cone pair map. It has no components to find and
//     no node membership to carry, so re-deriving either from the graph would be
//     a second whole-def walk that answers a question it already answered.
//
// Region ids are DENSE (0..n-1). The graph-built form mints them in
// body().nodes(forward) first-encounter order; the injected form takes the
// caller's order. Either way every loop below is deterministic without sorting
// a hash map.

#include <cstdint>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "color_common.hpp"
#include "hhds/graph.hpp"

namespace livehd::color {

constexpr int NO_REGION = -1;

class Region_graph {
public:
  // Build from a coloring of `g`. See color_size.cpp for the component /
  // weight / crossing-bit rules and what `name_weight` tilts.
  Region_graph(hhds::Graph* g, const Node2Id& node2id, int name_weight = 1);

  // Inject a region graph the caller already computed. `weights` sizes it;
  // `adjacency` must be SYMMETRIC (both directions present) and self-loop free,
  // exactly what merge() maintains. `members` may be empty when the caller has
  // no per-region node list to carry -- members() is then empty for every
  // region and region_of() returns NO_REGION, which is sound because such a
  // caller resolves nodes through its own owner array instead.
  Region_graph(std::vector<uint64_t> weights, std::vector<std::vector<hhds::Node_class>> members,
               std::vector<absl::flat_hash_map<int, uint64_t>> adjacency);

  [[nodiscard]] size_t   size() const { return weight_.size(); }
  [[nodiscard]] bool     alive(int r) const { return alive_[r]; }
  [[nodiscard]] uint64_t weight(int r) const { return weight_[r]; }

  [[nodiscard]] int find(int r) {
    while (rep_[r] != r) {
      rep_[r] = rep_[rep_[r]];  // path halving; iterative by mandate (color_common.hpp)
      r       = rep_[r];
    }
    return r;
  }

  [[nodiscard]] const absl::flat_hash_map<int, uint64_t>& neighbours(int r) const { return adj_[r]; }
  [[nodiscard]] const std::vector<hhds::Node_class>&      members(int r) const { return members_[r]; }

  // Fold one region into the other; the LARGER-degree side survives (tie:
  // smaller id) so the fold iterates the smaller neighbour map -- see the
  // definition for why. Returns the surviving region id.
  int merge(int a, int b);

  // Which of `a`/`b` merge() would DISSOLVE, under the same survivor rule.
  // Exists so a caller driving merges from a global priority queue can snapshot
  // exactly the adjacency a merge is about to rewrite (the dissolved side's)
  // without paying an O(hub-degree) scan of the survivor's.
  [[nodiscard]] int dissolved_side(int a, int b);

  // Region of `n`, resolved through the union-find.
  [[nodiscard]] int region_of(const hhds::Node_class& n) {
    auto it = node2region_.find(n);
    return it == node2region_.end() ? NO_REGION : find(it->second);
  }

private:
  std::vector<uint64_t>                           weight_;
  std::vector<bool>                               alive_;
  std::vector<int>                                rep_;
  std::vector<absl::flat_hash_map<int, uint64_t>> adj_;      // region -> neighbour -> crossing weight
  std::vector<std::vector<hhds::Node_class>>      members_;  // forward_class order
  absl::flat_hash_map<hhds::Node_class, int>      node2region_;
};

}  // namespace livehd::color
