// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "signal_route_profile.hpp"

namespace livehd::placement {
Signal_route_profile signal_route_profile(const physical::Tech_profile& tech_profile, const physical::Technology& tech) {
  Signal_route_profile profile{tech_profile.access, tech_profile.branch_layer, tech_profile.branch_cut, {}};
  for (const auto& name : tech_profile.signal_layers) {
    if (!tech.layers.contains(name)) {
      break;
    }
    profile.signal_layers.push_back(name);
  }
  return profile;
}
}  // namespace livehd::placement
