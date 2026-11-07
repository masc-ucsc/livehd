// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace livehd::placement {
// Dead space left by macro planning: free core area that standard cells cannot
// use well, measured on a raster of the core minus the macro bodies (plus their
// halo). All coordinates are DBU.
struct Dead_space_rect {
  int64_t xl = 0, yl = 0, xh = 0, yh = 0;
};
struct Dead_space_limits {
  int64_t core_xl = 0, core_yl = 0, core_xh = 0, core_yh = 0;
  int64_t pitch     = 1;  // square raster cell; a cell is blocked when its center lies inside an obstacle
  // A free gap no wider than the channel plus one pitch is a routing channel: free, never dead, and too narrow to
  // connect two free regions.
  int64_t channel_x = 0, channel_y = 0;
  // Free runs narrower than this (along x / along y) and wider than a channel are notches or slivers.
  int64_t narrow_x = 0, narrow_y = 0;
  // A free region whose open part (runs at least `narrow` both ways) holds less than this share of the free area is
  // a nearly enclosed pocket: it reaches the rest only through notches or channels. The largest region is kept.
  double  pocket_fraction = 0.25;
  // A connected free island (channels included) smaller than this area (DBU^2) is dead.
  int64_t min_area        = 0;
};
enum class Dead_kind : uint8_t { notch, pocket, island };
struct Dead_space_component {
  int64_t                      area = 0;  // DBU^2
  Dead_space_rect              bbox;
  Dead_kind                    kind = Dead_kind::notch;  // the kind of most of its cells
  std::vector<Dead_space_rect> rects;                    // raster outline (row runs merged), when requested
};
struct Dead_space_report {
  int64_t                           pitch  = 0;
  int                               grid_x = 0, grid_y = 0;
  int64_t                           free_area = 0, dead_area = 0;  // DBU^2; channels count as free
  int64_t                           notch_area = 0, pocket_area = 0, island_area = 0;
  // Dead area plus the non-channel area of kept regions other than the largest: free area stranded outside the
  // main region. The collapse pass minimizes it.
  int64_t                           stranded_area = 0;
  std::vector<Dead_space_component> components;  // largest first
};
// Classify the free core cells: a free cell in a channel is free; otherwise it
// is dead when its horizontal or vertical free run is narrower than `narrow`
// (notch/sliver), when its region is a pocket, or when its island is small.
// Dead cells are grouped into 4-connected components.
Dead_space_report measure_dead_space(const std::vector<Dead_space_rect>& obstacles, const Dead_space_limits& limits,
                                     bool outlines = false);

// One rectangle seen by the collapse pass (body, DBU). Non-macros are fixed
// obstacles that keep `halo` clearance; two macros keep `space`. Macros with
// the same nonzero `group` move together.
struct Dead_space_object {
  int64_t  xl = 0, yl = 0, xh = 0, yh = 0;
  bool     movable = false;
  bool     macro   = true;
  uint64_t group   = 0;
};
struct Dead_space_collapse_limits {
  Dead_space_limits metric;  // obstacles are bodies grown by metric_halo
  int64_t           metric_halo_x = 0, metric_halo_y = 0;
  int64_t           halo_x = 0, halo_y = 0;    // legality: a moved macro keeps this padded box inside the core
  int64_t           space_x = 0, space_y = 0;  // legality: macro-to-macro clear spacing
  int64_t           grid            = 1;       // moves are multiples of this
  int               max_moves       = 64;
  int               max_components  = 8;  // dead components examined per round, largest first
  int64_t           min_gain        = 0;  // dead area (DBU^2) a move must remove
  double            wl_move_budget  = 0;  // largest wirelength growth one move may add (cost units)
  double            wl_total_budget = 0;  // largest summed growth of all accepted moves
};
// Wirelength change if `members` (object indexes) translate by (dx, dy).
using Dead_space_cost = std::function<double(const std::vector<size_t>& members, int64_t dx, int64_t dy)>;
struct Dead_space_collapse_report {
  Dead_space_report before, after;
  int64_t           moves = 0, moved = 0, distance = 0;  // accepted group moves, macros moved, summed member DBU
  double            wl_delta   = 0;
  int64_t           candidates = 0;  // evaluated candidate moves
};
// Greedy collapse. Each round measures the raster and evaluates
// (1) translations: for every movable macro bordering one of the
//     `max_components` largest dead components, the macro, its aligned
//     column/row, or the block slice behind it moves into the component, as
//     far as legality allows or just across the dead depth;
// (2) relocations: a single macro on the rim of the main region jumps into a
//     corner of one of those components that it fits, packed against the walls.
// The candidate that removes the most stranded area (Dead_space_report::
// stranded_area; at least `min_gain`, within the wirelength budgets; ties
// prefer a translation toward the nearest core edge, then less wirelength) is
// applied. Stops when nothing improves or after `max_moves`. Moves keep every
// macro inside the core with its halo and every macro pair `space` apart.
Dead_space_collapse_report collapse_dead_space(std::vector<Dead_space_object>& objects, const Dead_space_collapse_limits& limits,
                                               const Dead_space_cost& cost);

// User controls of `synth.place.macro.dead_space` / physical_repair "macro_dead_space".
struct Dead_space_options {
  bool   collapse        = true;   // false: report the metric only
  double narrow_um       = -1;     // notch/sliver width; < 0: one tenth of the core extent per axis
  double pocket_fraction = 0.25;   // pocket: open region below this share of the free area
  double island_cells    = 200;    // island: free component holding fewer than this many average standard cells
  double hpwl_total      = 0.02;   // summed macro-net HPWL growth budget of all moves, fraction of the initial HPWL
  double hpwl_move       = 0.005;  // HPWL growth budget of a single move, same fraction
  int    max_moves       = 64;
  int    max_cells_axis  = 512;  // raster cells per axis at most; the pitch is a whole number of rows
  bool   operator==(const Dead_space_options&) const = default;
};
// "off" -> nullopt; "metric"; "auto"; or comma separated tokens "metric",
// "collapse" and key=value with keys narrow (microns or auto), pocket, cells,
// hpwl, move_hpwl, moves, grid. Throws std::invalid_argument on bad text.
std::optional<Dead_space_options> parse_dead_space_options(std::string_view text);
}  // namespace livehd::placement
