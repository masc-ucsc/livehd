// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "physical.hpp"

namespace livehd::physical {
// The pin-access algorithm supports vertical pin metal and a horizontal escape,
// connected by one cut layer.
struct Pin_access_layers {
  std::string pin, escape, cut;
};
// Routing policy. The current pattern/access algorithms require horizontal
// escape metal and vertical branch metal; unsupported directions/rules fail closed.
struct Route_policy {
  Pin_access_layers        access;
  std::string              branch_layer, branch_cut;  // vertical branch above the escape
  std::vector<std::string> signal_layers;             // bottom-up allowed trunk/maze metals
  bool                     allows(const std::string& layer) const {
    return std::find(signal_layers.begin(), signal_layers.end(), layer) != signal_layers.end();
  }
};
struct Pg_stripe {
  std::string layer;
  double      width = 0, spacing = 0, pitch = 0, offset = 0;  // microns
};
// Reference PDN strategy reproduced by the native power-grid generator.
struct Pg_strategy {
  std::string                             name, reference_revision, reference_file;
  std::vector<std::string>                followpin_layers;
  std::vector<Pg_stripe>                  stripes;
  std::vector<std::array<std::string, 2>> connect;
  std::string                             pin_layer;
  double                                  macro_halo = 0;  // microns
  std::vector<std::array<std::string, 2>> macro_connect;
  std::array<std::string, 2>              nets;  // power, ground
  std::array<std::vector<std::string>, 2> pin_names;
  bool                                    stripes_start_with_power = false;
  std::vector<std::string>                unsupported;  // Strategy features the generator lacks; must be empty.
};
// Process policy that LEF does not state: which layers, masters and reference
// strategies the physical flow uses. Geometry, tracks, vias and DRC remain in
// LEF/DEF; electrical characterization remains in Route_technology_rc. All
// technology-specific names live in the registry in tech_profile.cpp; callers
// take a resolved profile and never infer a process from layer or cell names.
struct Tech_profile {
  std::string                name;
  // Auto detection: any of these LEF sites or macros identifies the profile.
  std::vector<std::string>   detect_sites, detect_macros;
  Route_policy               route;
  std::array<std::string, 2> io_pin_layers;     // horizontal, vertical
  std::string                tap_master;        // endcap and well-tap master
  double                     tap_distance = 0;  // microns, OpenROAD tapcell -distance
  double                     tap_row_halo = 0;  // microns, row-cut halo around macros
  std::string                hold_buffer;       // default Liberty HOLD_BUF cell
  Pg_strategy                pg;
};
// Every registered profile.
std::span<const Tech_profile> tech_profiles();
// Registry lookup by profile name or by its power-grid strategy name.
const Tech_profile&           tech_profile(std::string_view name);
// The profile whose detect sites or macros the LEF has; nullptr when none
// does. A LEF that matches more than one profile throws std::invalid_argument.
const Tech_profile*           detect_tech_profile(const Technology&);
// "auto" detects the profile from the LEF; any other value is an explicit
// tech_profile() name. Unknown or undetectable technologies throw.
const Tech_profile&           resolve_tech_profile(const Technology&, std::string_view requested = "auto");
}  // namespace livehd::physical
