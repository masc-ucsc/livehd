// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>

#include "dreamplace/native/floorplan.hpp"
#include "dreamplace/native/placement.hpp"
#include "pass/color/floorplan_color.hpp"

namespace livehd::placement {
// Append full synthesis colors with a movable macro and sufficient physical
// area. Existing hierarchy candidates own their colors, including members
// outside the candidate's hierarchy. Repulsion is restricted to candidates
// with the same selected hierarchy parent; snap/type families stay separate.
void add_color_centroids(DreamPlace::native::Design& design, double minimum_equivalent_cells);

// Deepest selected structural candidate per node; -1 denotes the root scope.
std::vector<int> centroid_node_scopes(const DreamPlace::native::Design& design, const std::vector<std::string>& paths);

// Build color leaves and disjoint ownership at each hierarchy level. On an FP
// repair, retain group identities/eligibility and recompute members and budgets.
void placement_color_centroids(DreamPlace::native::Design& design, const std::vector<std::string>& paths,
                               const color::Floorplan_coloring& coloring, double minimum, bool frozen);

// Only the leading hierarchy candidates participate in centroid snapping.
// Unique color leaves preserve their offsets relative to a snapped parent;
// root-level color targets retain their exact post-floorplan positions.
DreamPlace::native::FloorplanSnap snap_centroid_targets(const std::vector<DreamPlace::native::FloorplanCircle>& circles,
                                                        size_t hierarchy_count, const DreamPlace::native::Layout& layout,
                                                        const DreamPlace::native::FloorplanOptions& options);
}  // namespace livehd::placement
