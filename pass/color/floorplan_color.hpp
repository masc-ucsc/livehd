// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace livehd::color {
// Read-only physical graph, after wiring aliases have been resolved. Edges are
// directed DATA arcs only: the caller removes clock/reset/constant paths using
// technology metadata. State bridges are explicit; no macro is traversable.
enum class Floorplan_kind { logic, state, macro, barrier };
struct Floorplan_node {
  std::string      name;
  Floorplan_kind   kind = Floorplan_kind::logic;
  double           area = 0;
  // Optional sorted net identities for macro address/data inputs. Unlike node
  // adjacency, these distinguish separate output pins of the same driver.
  std::vector<int> input_nets{};
};
struct Floorplan_color {
  std::string      name;
  std::vector<int> members;
  double           area   = 0;
  int              macros = 0;
};
struct Floorplan_coloring {
  std::vector<int>              owner;  // -1 remains uncolored, including boundary registers
  std::vector<Floorplan_color>  colors;
  // Sorted distinct colors reaching an uncolored cell through exactly one
  // boundary register. The second register is excluded. Eligibility is applied
  // later, before dividing area among the remaining recipients.
  std::vector<std::vector<int>> recipients;
  std::vector<int>              merges_per_round;
  int                           initial_memory_groups = 0;
  int                           truncated_cones       = 0;
};
struct Floorplan_color_options {
  bool             memory_first   = false;
  double           max_logic_area = 0;  // required positive for memory_first; macros excluded
  // Optional deepest selected hierarchy candidate per physical node (-1=root).
  // Split graph colors into local portions before computing area reservations.
  // Frozen repair membership is already scoped and must not be split again.
  std::vector<int> hierarchy_scope{};
};
// During in-place FP repair, preserved supplies stable color names and current
// per-node owners (new buffers inherit their physical origin). Areas, members
// and local reservations are rebuilt; original macro groups cannot split/merge
// as buffers are inserted or removed midway through an optimizer trajectory.
Floorplan_coloring floorplan_color(const std::vector<Floorplan_node>& nodes, const std::vector<std::pair<int, int>>& data_edges,
                                   const Floorplan_coloring* preserved = nullptr, Floorplan_color_options options = {});
}  // namespace livehd::color
