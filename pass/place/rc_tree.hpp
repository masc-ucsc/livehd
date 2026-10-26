// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <vector>

#include "core/physical_rc.hpp"

namespace livehd::placement::late {
struct Pi_model {
  double near_cap_ff = 0, far_cap_ff = 0, resistance_ohm = 0;
};
struct Rc_tree_state {
  std::array<double, 2>              total_cap_ff{};
  std::array<Pi_model, 2>            pi;
  std::vector<std::array<double, 2>> elmore_ps;
};
// Analyze a connected resistor tree rooted at the selected driver node.
// When provided, node_cap_ff is the TOTAL node capacitance (wire plus actual
// pin loads once), separately for rise/fall. Otherwise use wire-only Rc_node
// capacitances. The pi model matches the first three input-admittance moments;
// Elmore delays remain per sink and are not replaced by the pi approximation.
Rc_tree_state analyze_rc_tree(const physical::Rc_net& net, unsigned driver,
                              const std::vector<std::array<double, 2>>& node_cap_ff = {});
}  // namespace livehd::placement::late
