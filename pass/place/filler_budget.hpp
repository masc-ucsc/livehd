// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "dreamplace/native/placement.hpp"
namespace livehd::placement {
struct Filler_budget {
  double fraction = 0, available_area = 0, requested_area = 0, nonmacro_area = 0;
};
// Diagnostics for the native area/utilization policy. No absolute particle
// target or change to particle dimensions: fraction scales available density
// charge after macro/fixed occupancy. Particle counts are solver outputs.
Filler_budget relative_filler_budget(const DreamPlace::native::Design&, const DreamPlace::native::Options&);
}  // namespace livehd::placement
