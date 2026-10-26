// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <utility>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// LEF routing tracks per DBU, {horizontal, vertical}, summed over the named
// layers' preferred directions. Names without a LEF layer or a positive pitch
// contribute zero (macro OBS layers may name cut or masterslice layers).
template <class Layers>
std::pair<double, double> lef_track_capacity(const physical::Technology& tech, const Layers& layers) {
  double horizontal = 0, vertical = 0;
  for (const auto& name : layers) {
    const auto found = tech.layers.find(name);
    if (found == tech.layers.end()) {
      continue;
    }
    const auto& layer = found->second;
    if (layer.direction == "HORIZONTAL" && layer.pitch_y > 0) {
      horizontal += 1 / (layer.pitch_y * tech.dbu);
    }
    if (layer.direction == "VERTICAL" && layer.pitch_x > 0) {
      vertical += 1 / (layer.pitch_x * tech.dbu);
    }
  }
  return {horizontal, vertical};
}
// Full routing capacity of the technology's routing layers.
inline std::pair<double, double> lef_track_capacity(const physical::Technology& tech) {
  return lef_track_capacity(tech, tech.routing_layers);
}
}  // namespace livehd::placement
