// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "inou/lefdef/physical.hpp"
namespace livehd::placement {
// Adjust movable BLOCK origins before row packing. Explicit FIXED/LOCKED objects
// are immutable and diagnosed when their signal pins miss known routing tracks.
void legalize_macro_tracks(const physical::Technology& tech, const physical::Floorplan& layout,
                           std::vector<physical::Component>& cells, int64_t halo_x, int64_t halo_y, bool move_macros);
}  // namespace livehd::placement
