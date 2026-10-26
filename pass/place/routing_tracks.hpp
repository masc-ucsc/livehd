// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Literal make_tracks layer -x_offset/-x_pitch/-y_offset/-y_pitch data in um.
// Never executes Tcl. Replaces tracks atomically; origins are relative to DIEAREA.
void                                   apply_routing_tracks(const physical::Technology&, physical::Floorplan&, std::string_view);
// One routing-track lattice in DBU: start + i*step for i in [0, count).
struct Track_pattern {
  int64_t start = 0, step = 0, count = 0;
};
struct Track_lattice {
  bool                       imported = false;  // some floorplan track names this layer and axis
  std::vector<Track_pattern> patterns;          // those with positive step and count
};
// Imported (DEF or make_tracks) tracks of layer on axis "X" (x positions of
// vertical wires) when x is true, else "Y".
Track_lattice                          imported_tracks(const physical::Floorplan&, std::string_view layer, bool x);
// The LEF PITCH/OFFSET lattice of a layer on the same axis, unbounded. The
// offset is zero when the LEF gives none; callers decide whether to trust it.
Track_pattern                          lef_track_pattern(const physical::Technology&, const physical::Layer&, bool x);
// Automatic perimeter anchors use actual tracks when available, retaining the
// site-pitch spread only on axes without imported track data.
std::vector<std::pair<double, double>> io_perimeter_slots(const physical::Technology&, const physical::Floorplan&,
                                                          std::string_view layer, double pitch,
                                                          std::string_view vertical_layer = {});
// Extend movable edge-pin shapes inward to reach the normal-axis track. Fixed
// imported pin shapes and coordinates are immutable.
void                                   shape_io_perimeter_pins(physical::Floorplan&);
}  // namespace livehd::placement
