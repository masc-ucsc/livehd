// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "dreamplace/native/placement.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Minimum-cost strictly increasing slot assignment. Infinity forbids a slot.
// Returns an empty vector if no feasible assignment exists.
std::vector<int> ordered_pin_slots(size_t lanes, size_t slots, const std::function<double(size_t, size_t)>& cost);
struct Pin_adjust_stats {
  unsigned moved = 0, reversals = 0;
  double   travel = 0, before = 0, after = 0;
};
// buses[i] comes from the graph port declaration, bits[i] from its bit lane.
// Coordinates and spacing are DBU. Initial assignment respects bus order;
// later adjustments preserve that circular order or reverse the whole bus.
Pin_adjust_stats adjust_fp_pins(DreamPlace::native::Design& design, physical::Floorplan& floorplan,
                                const std::vector<std::string>& buses, const std::vector<int>& bits, double pitch,
                                double step_fraction, double improvement, bool initialize);
}  // namespace livehd::placement
