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
  // faces with signal pins: left 1, right 2, bottom 4, top 8 (sliver snap keeps a channel between two pinned faces)
  unsigned pin_faces = 0;
  // routing-track lattice of the origin per axis (x 0, y 1): phases[a][i] + k * period[a]; period 0 = any origin
  int64_t              period[2] = {0, 0};
  std::vector<int64_t> phases[2];
};
struct Edge_snap_limits {
  int64_t core_xl = 0, core_yl = 0, core_xh = 0, core_yh = 0;
  int64_t halo_x = 0, halo_y = 0;        // per-side macro halo; the padded box stays in the core
  int64_t space_x = 0, space_y = 0;      // macro-to-macro clear spacing per axis
  int64_t max_gap_x = 0, max_gap_y = 0;  // largest sliver that is collapsed; 0 disables that axis
  int64_t grid = 1;                      // origin grid (manufacturing grid), relative to the core corner
  // Sliver snap: also close, toward the macro's core edge, any clear sliver narrower than the clearance it faces
  // (a body gap under 2 * space to a macro, under 2 * halo to the core edge), anchored to the edge or not, unless both
  // facing faces carry signal pins. Such a sliver is too narrow for cells and only makes a routing bottleneck.
  bool slivers = false;
  // Clearance to the core edge for a macro face that carries signal pins (0: the halo, as for any face). A
  // downstream flow that cuts rows near macros (ORFS tapcell) leaves only clearance - row_halo of row there, and the
  // hold/repair buffers those pins need pile up in it; a pinned face keeps a full channel instead, and a macro that
  // sits closer is pushed back when the far side has room.
  int64_t pin_edge_x = 0, pin_edge_y = 0;
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
// next one. With `slivers`, narrower slivers also close without an edge anchor.
// Moved origins land on the origin grid and on the macro's track lattice (the
// sliver may then stay open by less than one track period). The result keeps
// every pairwise clearance that held before.
Edge_snap_report snap_macros_to_edges(std::vector<Edge_object>& objects, const Edge_snap_limits& limits);
}  // namespace livehd::placement
