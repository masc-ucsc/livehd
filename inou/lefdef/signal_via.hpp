// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>

#include "routing_occupancy.hpp"
#include "routing_rules.hpp"

namespace livehd::physical {
// Validate a single-cut LEF via between adjacent routing layers, enclosed in
// two complete rectangular route landings (global DEF DBU). Landings include
// the wire extensions needed for area/minsize rules. Checks existing metal
// and cuts; callers must also check against other new vias of the trial net.
// Supports single-cut LEF58 classes, all-DEFAULT spacing tables and the modeled
// enclosure forms. Other qualified rules fail closed.
Rule_check check_signal_via(const Technology&, const Floorplan&, const Signal_metal_rules&, const Routing_occupancy&,
                            const Route_via&, int64_t x, int64_t y, const std::array<Shape, 2>& landings,
                            const std::string& own_net);
}  // namespace livehd::physical
