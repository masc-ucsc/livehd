// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "inou/lefdef/pin_access.hpp"

namespace livehd::placement {
// Technology policy only. Dimensions, track locations, vias and DRC remain in
// LEF/Floorplan; electrical characterization remains in Route_technology_rc.
using Signal_route_profile = physical::Route_policy;
// The technology profile's routing policy. Signal layers stop at the first one
// the technology lacks. Routing algorithms and callers must not infer a process
// from layer spelling; tests may still supply other layer/cut names directly.
Signal_route_profile signal_route_profile(const physical::Tech_profile&, const physical::Technology&);
}  // namespace livehd::placement
