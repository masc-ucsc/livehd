// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string_view>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Literal make_tracks layer -x_offset/-x_pitch/-y_offset/-y_pitch data in um.
// Never executes Tcl. Replaces tracks atomically; origins are relative to DIEAREA.
void apply_routing_tracks(const physical::Technology&, physical::Floorplan&, std::string_view);
}  // namespace livehd::placement
