// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
struct Wire_rc {
  double      horizontal_r = 0, horizontal_c = 0, vertical_r = 0, vertical_c = 0;
  std::string source;
};
// Ohm/um and fF/um. Explicit R,C override technology data. Technology files
// contain literal set_wire_rc/set_layer_rc data, optionally set_cmd_units;
// commands are parsed as data and never executed. Unspecified units come from
// the first Liberty library header, matching its technology command units.
Wire_rc technology_wire_rc(const physical::Technology& tech, const std::string& liberty, const std::string& rc_file,
                           double resistance = -1, double capacitance = -1);
}  // namespace livehd::placement
