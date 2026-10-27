// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/physical_rc.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
struct Wire_rc {
  double      horizontal_r = 0, horizontal_c = 0, vertical_r = 0, vertical_c = 0;
  std::string source;
};
// Horizontal R, horizontal C, vertical R, vertical C per micron: the layout of
// Rc_policy::per_um and the buffering producer's directional_rc.
inline std::array<double, 4> per_um(const Wire_rc& wire) {
  return {wire.horizontal_r, wire.horizontal_c, wire.vertical_r, wire.vertical_c};
}
struct Layer_rc {
  double r = -1, c = -1;  // ohm/um and fF/um; negative means uncharacterized
};
struct Route_technology_rc {
  std::map<std::string, Layer_rc> layers;
  std::map<std::string, double>   cut_resistance_ohm;
  std::map<std::string, double>   via_resistance_ohm;
  std::string                     source;
};
// Physical routing uses each layer's characterization and via resistance;
// a generic set_wire_rc -signal estimate never substitutes for missing layers.
// LEF fixed-via resistances are retained; set_layer_rc -via names cut layers.
Route_technology_rc technology_route_rc(const physical::Technology&, const std::string& liberty, const std::string& rc_file);
// Ohm/um and fF/um. Explicit R,C override technology data. Technology files
// contain literal set_wire_rc/set_layer_rc data, optionally set_cmd_units;
// commands are parsed as data and never executed. Unspecified units come from
// the first Liberty library header, matching its technology command units.
Wire_rc             technology_wire_rc(const physical::Technology& tech, const std::string& liberty, const std::string& rc_file,
                                       double resistance = -1, double capacitance = -1, double rc_pessimism = 1,
                                       std::optional<std::string_view> liberty_text = std::nullopt);
// One net's driver-aware Steiner RC from pin labels and positions in DBU.
// The driver label must occur in pins, in any position. Above 1000
// distinct locations it joins spatial FLUTE groups, retains an acyclic
// resistance-weighted spanning tree, and sets *approximate.
physical::Rc_net    steiner_rc(std::string driver, const std::vector<std::pair<std::string, std::pair<double, double>>>& pins,
                               const Wire_rc& wire, double dbu, bool* approximate = nullptr);
}  // namespace livehd::placement
