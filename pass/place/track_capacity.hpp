// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cmath>
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
// Standard-cell pin access the detailed router sustains, in pins per um^2:
// `factor` * sqrt(tracks/um of the first horizontal * first vertical routing
// layer above the lowest one (the cell-pin layer)). 0.3 is calibrated on asap7
// (HighTide litepci: DRT violations start above ~9 cell pins/um^2, the formula
// gives 8.3). 0 when the LEF lacks either direction above the pin layer, or (auto) when those pitches exceed
// `max_pitch_um`: older nodes (sky130, nangate45) route their cells' pin density at ordinary placement densities,
// and this factor is not calibrated for them.
inline double lef_pin_capacity_um2(const physical::Technology& tech, double factor = .3, double max_pitch_um = .05) {
  double horizontal = 0, vertical = 0;
  for (size_t i = 1; i < tech.routing_layers.size() && (horizontal == 0 || vertical == 0); ++i) {
    const auto found = tech.layers.find(tech.routing_layers[i]);
    if (found == tech.layers.end()) {
      continue;
    }
    const auto& layer = found->second;
    if (horizontal == 0 && layer.direction == "HORIZONTAL" && layer.pitch_y > 0) {
      horizontal = 1 / layer.pitch_y;
    }
    if (vertical == 0 && layer.direction == "VERTICAL" && layer.pitch_x > 0) {
      vertical = 1 / layer.pitch_x;
    }
  }
  if (horizontal <= 0 || vertical <= 0 || 1 / horizontal > max_pitch_um || 1 / vertical > max_pitch_um) {
    return 0;
  }
  return factor * std::sqrt(horizontal * vertical);
}
}  // namespace livehd::placement
