// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "core/physical_rc.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::physical {
// Import extracted signal-net RC trees relative to the same DEF. Coupling is
// grounded at each local plate with the OpenSTA default reduction factor of 1.
Parasitics clock_report_spef(const std::string& path, const Floorplan& fp, const Technology& tech,
                             const std::set<std::string>* selected_nets = nullptr);
}  // namespace livehd::physical
