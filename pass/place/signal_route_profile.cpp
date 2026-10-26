// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "signal_route_profile.hpp"

namespace livehd::placement {
Signal_route_profile signal_route_profile(const physical::Tech_profile& tech_profile, const physical::Technology& tech) {
  auto       profile = tech_profile.route;
  const auto missing = std::find_if(profile.signal_layers.begin(), profile.signal_layers.end(), [&](const auto& name) {
    return !tech.layers.contains(name);
  });
  profile.signal_layers.erase(missing, profile.signal_layers.end());
  return profile;
}
}  // namespace livehd::placement
