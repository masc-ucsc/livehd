// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Returns a transaction-private terminal list. Movable signal/clock IOs retain
// their nearest edge and order, get minimum-area rectangles on literal preferred
// tracks, and avoid existing metal/PG/pin spacing. Fixed/locked and supply pins
// remain byte-identical. Call before final RC estimation, with the actual PG.
std::vector<physical::Terminal> handoff_pins(const physical::Technology&, const physical::Floorplan&,
                                             const std::string& horizontal_layer, const std::string& vertical_layer);
}  // namespace livehd::placement
