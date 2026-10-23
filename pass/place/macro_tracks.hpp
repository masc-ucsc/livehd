// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>

#include "inou/lefdef/physical.hpp"
namespace livehd::placement {
// Cached periodic feasibility, independent of absolute origin, core bounds and
// obstacles. Every original member keeps its standalone supported-pin profile.
// The technology and floorplan must outlive the serially invoked checker.
std::function<bool(const std::vector<physical::Component>&)> make_rigid_macro_track_checker(const physical::Technology&,
                                                                                            const physical::Floorplan&);
// Adjust movable BLOCK origins before row packing. Explicit FIXED/LOCKED objects
// are immutable and diagnosed when their signal pins miss known routing tracks.
void legalize_macro_tracks(const physical::Technology& tech, const physical::Floorplan& layout,
                           std::vector<physical::Component>& cells, int64_t halo_x, int64_t halo_y, bool move_macros);
// Diagnostic group ids describe rigid translations only during macro_plan.
// Each group is profiled/placed as one union rectangle with all member pins.
void legalize_rigid_macro_tracks(const physical::Technology&, const physical::Floorplan&, std::vector<physical::Component>&,
                                 const std::vector<uint64_t>& group_ids, int64_t halo_x, int64_t halo_y, bool move_macros);
}  // namespace livehd::placement
