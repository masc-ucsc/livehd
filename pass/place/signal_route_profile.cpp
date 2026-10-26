// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "signal_route_profile.hpp"

namespace livehd::placement {
Signal_route_profile asap7_signal_route_profile(const physical::Technology& tech) {
  Signal_route_profile profile{physical::asap7_pin_access_layers(), "M3", "V2", {}};
  for (size_t number = 2; number <= 7; ++number) {
    const auto name = "M" + std::to_string(number);
    if (!tech.layers.contains(name)) {
      break;
    }
    profile.signal_layers.push_back(name);
  }
  return profile;
}
}  // namespace livehd::placement
