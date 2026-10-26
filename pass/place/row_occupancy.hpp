// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Row occupancy model shared by terminal legalization and the multi-row
// anchor packer.

// Free sites of one placement row segment. `free` maps the first site of each
// free gap to one past its last; sites count `step` DBU from x.
struct Row_gaps {
  size_t                     source;  // index into Floorplan::rows
  int64_t                    x, y, step, height, count;
  std::map<int64_t, int64_t> free;
};
// DBU rectangle of sites [lo, hi) of a row.
inline physical::Rect gap_rect(const Row_gaps& r, int64_t lo, int64_t hi) {
  return {double(r.x + lo * r.step), double(r.y), double(r.x + hi * r.step), double(r.y + r.height)};
}
// Hull of every free gap.
physical::Rect        gaps_extent(const std::vector<Row_gaps>&);
// Remove sites [lo, hi) from a gap map.
void                  subtract_gaps(std::map<int64_t, int64_t>& gaps, int64_t lo, int64_t hi);
// The frame's rows sorted by (y, x), each with one gap of the complete sites
// inside the die; rows without one are dropped. Throws std::invalid_argument,
// prefixed by owner, for unknown or non-contiguous sites, no usable row and
// overlapping rows.
std::vector<Row_gaps> row_gaps(const physical::Technology&, const physical::Floorplan&, std::string_view owner);
// Remove every site the rectangle overlaps from rows sorted by y.
void                  reserve_gaps(std::vector<Row_gaps>& rows, const physical::Rect&);

// Fixed WELLTAP or SPACER: physical infrastructure, not logic.
bool fixed_physical_cell(const physical::Component&, const physical::Macro&);
// How the model treats a frame component. Soft placement masks are ignored.
// Non-CORE masters (hard masks included) and fixed physical cells are
// obstacles that remove row capacity. Other fixed CORE cells keep their sites
// and consume density; the remaining CORE cells are movable.
enum class Frame_role : uint8_t { ignored, obstacle, fixed_logic, movable };
Frame_role     frame_role(const physical::Technology&, const physical::Component&);
// Sites an obstacle reserves: its footprint, plus the macro halo around a
// non-CORE master that is not a placement mask.
physical::Rect obstacle_rect(const physical::Technology&, const physical::Component&, int64_t halo_x_dbu, int64_t halo_y_dbu);
// Sites a fixed CORE cell reserves: its footprint widened on both sides by
// padding sites of its master's SITE. Throws std::invalid_argument without one.
physical::Rect padded_rect(const physical::Technology&, const physical::Component&, unsigned padding_sites);
}  // namespace livehd::placement
