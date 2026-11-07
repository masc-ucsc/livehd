// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <vector>

namespace livehd::placement {
// One rectangle seen by the macro edge snap, in DBU. `body` is the physical
// macro body; obstacles that are not macros (`macro=false`) keep only `halo`
// clearance from a macro body, two macros keep `space` (max(channel, 2*halo)).
struct Edge_object {
  int64_t xl = 0, yl = 0, xh = 0, yh = 0;
  bool    movable = false;
  bool    macro   = true;
};
struct Edge_snap_limits {
  int64_t core_xl = 0, core_yl = 0, core_xh = 0, core_yh = 0;
  int64_t halo_x = 0, halo_y = 0;        // per-side macro halo; the padded box stays in the core
  int64_t space_x = 0, space_y = 0;      // macro-to-macro clear spacing per axis
  int64_t max_gap_x = 0, max_gap_y = 0;  // largest sliver that is collapsed; 0 disables that axis
  int64_t grid = 1;                      // origin grid (manufacturing grid), relative to the core corner
};
struct Edge_snap_report {
  int64_t moved    = 0;  // macros displaced on at least one axis
  int64_t moves    = 0;  // axis moves
  int64_t distance = 0;  // total displacement, DBU
};
// Slide movable macros toward their nearest core edge, first along x, then
// along y. Each macro is assigned to the half of the core holding its center
// and, nearest first, slides toward that edge until it meets the core edge
// (minus its halo) or the spacing of an object in its path. It moves only when
// that object is the core edge or another macro already anchored to the edge,
// and the clear sliver it removes is positive and at most `max_gap`. Moving
// toward the edge never brings two macros closer than `space`; objects outside
// its path are unaffected. A macro that already touches its anchor anchors the
// next one. The result keeps every pairwise clearance that held before.
Edge_snap_report snap_macros_to_edges(std::vector<Edge_object>& objects, const Edge_snap_limits& limits);
}  // namespace livehd::placement
