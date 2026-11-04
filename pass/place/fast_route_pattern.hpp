// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace livehd::placement {

enum class Pattern_route_status { complete, unresolved, work_limit };
enum class Pattern_route_mode { exhaustive, l_first };

struct Pattern_route {
  Pattern_route_status             status = Pattern_route_status::unresolved;
  std::vector<std::pair<int, int>> edges;     // Sorted, unique adjacent tile pairs, lower ID first.
  double                           cost = 0;  // Sum over unique edges, only valid for complete.
  size_t                           work = 0;
};

// Native L/Z pattern routing adapted from OpenROAD FastRoute; see the implementation
// for provenance. Tile IDs are row * cols + col. The caller supplies a connected
// topology as vertex tile IDs and branch vertex indices, with arbitrary degree
// and coincident vertices allowed. Branches are processed in caller order.
//
// edge_cost receives a canonical adjacent tile pair. It must remain stable for
// this call: a nonnegative finite value is its cost, +infinity or NaN forbids it;
// negative values are invalid. Physical H/V length and congestion belong in this
// callback, so rectangular tiles do not imply equal physical edge costs.
//
// Exhaustive mode chooses the cheapest monotone HVH/VHV path within each branch's
// bounding rectangle (including both L paths). L-first mode instead accepts the
// cheaper feasible L in O(width+height) edge visits, searching Z candidates only
// when both L paths are forbidden. It may miss a cheaper Z when an L is feasible.
// Edges already used by this net cost zero
// when choosing later branches, but forbidden edges are never traversed. This is
// greedy across branches, not an optimal multi-terminal routing algorithm. A
// connected result can contain cycles, and is not a detailed-route legal witness.
//
// Work counts input vertices/branches, grid-edge evaluations, candidate evaluations
// and emitted path steps; buffers and traversals are bounded by work_limit. Zero
// permits no work. On unresolved or work_limit, edges are empty and cost is zero:
// callers may fall back to maze routing without undoing reservations. Unresolved
// means these patterns/topology did not connect, not that the grid is disconnected.
// Invalid dimensions, vertex/branch indices, callback or negative cost throw.
Pattern_route fast_route_pattern(int cols, int rows, std::span<const int> vertices,
                                 std::span<const std::pair<size_t, size_t>> branches,
                                 const std::function<double(int, int)>& edge_cost, size_t work_limit,
                                 Pattern_route_mode mode = Pattern_route_mode::exhaustive);

}  // namespace livehd::placement
