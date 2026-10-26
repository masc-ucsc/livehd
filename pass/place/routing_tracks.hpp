// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string_view>
#include <utility>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Literal make_tracks layer -x_offset/-x_pitch/-y_offset/-y_pitch data in um.
// Never executes Tcl. Replaces tracks atomically; origins are relative to DIEAREA.
void                                   apply_routing_tracks(const physical::Technology&, physical::Floorplan&, std::string_view);
// Automatic perimeter anchors use actual tracks when available, retaining the
// site-pitch spread only on axes without imported track data.
std::vector<std::pair<double, double>> io_perimeter_slots(const physical::Technology&, const physical::Floorplan&,
                                                          std::string_view layer, double pitch,
                                                          std::string_view vertical_layer = {});
// Extend movable edge-pin shapes inward to reach the normal-axis track. Fixed
// imported pin shapes and coordinates are immutable.
void                                   shape_io_perimeter_pins(physical::Floorplan&);
// LEF routing tracks per DBU, {horizontal, vertical}, summed over the routing
// layers' preferred directions. A direction without a positive pitch is zero.
std::pair<double, double>              lef_track_capacity(const physical::Technology&);
}  // namespace livehd::placement
