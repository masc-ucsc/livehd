// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "place.hpp"

namespace livehd::placement {
// Called once after all accepted final placement/netlist repairs. Publishes
// FIXED geometry, graph placement/RC attributes, report and updated QoR. The
// outer physical cache may publish only after this function succeeds.
void preroute_placed(Result&, const Options&, const Evaluate&);
}  // namespace livehd::placement
