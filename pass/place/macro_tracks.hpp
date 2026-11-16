// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "inou/lefdef/physical.hpp"
#include "power_grid_rules.hpp"
namespace livehd::placement {
// The origins at which a macro's signal pins land on routing tracks, as `legalize_macro_tracks` demands of a FIXED
// macro: per axis the lower-left origin must be phases[axis][i] + k * period[axis] for some i and integer k (pins that
// need either axis are not separated, so a macro with such pins is judged by its single-axis pins). Plain data: it
// keeps no reference to the technology or the floorplan. nullopt: the macro has no track-constrained signal pins (no
// track data for their layers), so any origin is accepted.
struct Macro_track_lattice {
  int64_t              period[2] = {1, 1};
  std::vector<int64_t> phases[2];
  unsigned             pins = 0;  // signal pins that constrain the origin
};
std::optional<Macro_track_lattice> macro_track_lattice(const physical::Technology&, const physical::Floorplan&, const std::string& master,
                                                       physical::Orientation);
// Cached periodic feasibility, independent of absolute origin, core bounds and
// obstacles. Every original member keeps its standalone supported-pin profile.
// The technology and floorplan must outlive the serially invoked checker.
std::function<bool(const std::vector<physical::Component>&)> make_rigid_macro_track_checker(const physical::Technology&,
                                                                                            const physical::Floorplan&);
// Adjust movable BLOCK origins before row packing. Only a complete macro frame may spend spare space on perimeter IO channels. Explicit FIXED/LOCKED objects
// are immutable and diagnosed when their signal pins miss known routing tracks.
// A trapped corner with signal pins on both outward faces may open a buffer channel by shifting movable neighbors on a common track/site period.
// A dense signal bank on a top/bottom face can similarly reserve complete rows for downstream repair.
// Halos keep CORE cells out; two non-CORE bodies keep max(channel, 2 * halo) of clear
// spacing per axis (DBU). Moved origins never land closer than that to another macro.
// With `pdn`, movable macro origins also land where the power-grid strap lattice admits their edges
// (Pg_edge_lattice::allows against the row core). When no such frame exists, the alignment is retried without
// the lattice and *pdn_aligned (when given) reports false; fixed macros are never checked against it.
void legalize_macro_tracks(const physical::Technology& tech, const physical::Floorplan& layout,
                           std::vector<physical::Component>& cells, int64_t halo_x, int64_t halo_y, bool move_macros,
                           int64_t channel_x = 0, int64_t channel_y = 0, const Pg_macro_alignment* pdn = nullptr,
                           bool* pdn_aligned = nullptr, bool complete_macro_frame = true);
// Diagnostic group ids describe rigid translations only during macro_plan.
// Each group is profiled/placed as one union rectangle with all member pins; with `pdn` a group position is
// lattice-aligned only when every member macro's edges are admitted (not just the union rectangle's).
void legalize_rigid_macro_tracks(const physical::Technology&, const physical::Floorplan&, std::vector<physical::Component>&,
                                 const std::vector<uint64_t>& group_ids, int64_t halo_x, int64_t halo_y, bool move_macros,
                                 int64_t channel_x = 0, int64_t channel_y = 0, const Pg_macro_alignment* pdn = nullptr,
                                 bool* pdn_aligned = nullptr);
}  // namespace livehd::placement
