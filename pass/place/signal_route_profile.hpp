// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "inou/lefdef/pin_access.hpp"

namespace livehd::placement {
// Technology policy only. Dimensions, track locations, vias and DRC remain in
// LEF/Floorplan; electrical characterization remains in Route_technology_rc.
// The current pattern/access algorithms require horizontal escape metal and
// vertical branch metal. Unsupported directions/rules fail closed.
struct Signal_route_profile {
  physical::Pin_access_layers access;
  std::string                 branch_layer, branch_cut;
  std::vector<std::string>    signal_layers;  // bottom-up allowed trunk/maze metals
  bool                        allows(const std::string& layer) const {
    return std::find(signal_layers.begin(), signal_layers.end(), layer) != signal_layers.end();
  }
};
// The technology profile's routing policy. Signal layers stop at the first one
// the technology lacks. Routing algorithms and callers must not infer a process
// from layer spelling; tests may still supply other layer/cut names directly.
Signal_route_profile signal_route_profile(const physical::Tech_profile&, const physical::Technology&);
}  // namespace livehd::placement
