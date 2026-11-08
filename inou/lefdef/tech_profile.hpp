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
// Technology-neutral power-grid description, transcribed from a reference PDN
// script (OpenROAD pdngen) without interpretation. Layer directions, widths,
// spacing rules, vias and the standard-cell rail width/pitch (PG pins of the
// one-row masters, row height) stay in LEF/DEF; this states only the choices a
// LEF cannot: which layers carry supply, at what width/pitch/offset, and how
// they are stacked. Coordinates are microns.
//
// One periodic strap family (add_pdn_stripe without -followpins). Within one
// pitch there is one strap per supply net: the first net's centerline at
// grid origin + offset + k * pitch, the next one width + spacing above/right
// of it. The grid origin is the core lower-left corner for the core grid and
// the macro's placed lower-left corner for a macro grid. The direction is the
// LEF preferred direction of the layer.
struct Pg_stripe {
  std::string layer;
  double      width = 0, spacing = 0, pitch = 0, offset = 0;  // microns
};
// A macro grid (define_pdn_grid -macro): applies to every BLOCK macro placed
// in one of `orientations` (empty: any orientation). Its halo is the keep-out
// around the macro body within which the core grid's straps on the layers the
// macro blocks are removed. Its own straps span the macro's PG pin bounding box
// (the macro body with over_boundary) and connect to the macro PG pins and to
// the core grid through `connect`; a connection whose lower layer is a macro
// PG pin layer lands on the fixed pin.
struct Pg_macro_grid {
  std::vector<Orientation>                orientations;
  double                                  halo = 0;  // microns per side
  std::vector<Pg_stripe>                  stripes;
  std::vector<std::array<std::string, 2>> connect;
  bool                                    over_boundary = false;  // -grid_over_boundary
};
// A core ring (add_pdn_ring): a horizontal-layer and a vertical-layer loop of
// one strap per net around the core. Part of the description so strategies can
// state it; the native generator does not build rings and rejects them.
struct Pg_ring {
  std::array<std::string, 2> layers;  // horizontal, vertical
  double                     width = 0, spacing = 0, core_offset = 0;
};
// Reference PDN strategy reproduced by the native power-grid generator.
struct Pg_strategy {
  std::string                             name, reference_revision, reference_file;
  std::vector<std::string>                followpin_layers;  // rails: LEF PG-pin width at the row pitch
  std::vector<Pg_stripe>                  stripes;           // core straps, bottom-up
  std::vector<std::array<std::string, 2>> connect;           // core via stacks {lower, upper}
  std::string                             pin_layer;         // supply terminals
  std::vector<Pg_macro_grid>              macro_grids;
  std::vector<Pg_ring>                    rings;
  std::array<std::string, 2>              nets;  // power, ground
  std::array<std::vector<std::string>, 2> pin_names;
  bool                                    stripes_start_with_power = false;  // pdngen -starts_with
  std::vector<std::string>                unsupported;  // Strategy features the generator lacks; must be empty.
  // The macro grid that applies to a macro placed in this orientation, nullptr when none does; throws
  // invalid_argument when more than one does (the orientation sets must be disjoint).
  const Pg_macro_grid*                    macro_grid(Orientation) const;
};
enum class Tech_policy {
  route,       // pin access / signal routing policy (Route_policy)
  power_grid,  // native power-grid strategy (Pg_strategy)
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
  // Which policies the profile defines. A profile may state only part of the
  // process policy (a power-grid strategy without a supported routing policy).
  bool                       provides(Tech_policy) const;
};
// Every registered profile.
std::span<const Tech_profile> tech_profiles();
// Registry lookup by profile name or by its power-grid strategy name.
const Tech_profile&           tech_profile(std::string_view name);
// The profile providing `need` whose detect sites or macros the LEF has;
// nullptr when none does. A LEF that matches more than one such profile throws
// std::invalid_argument. Profiles lacking `need` are ignored, so a consumer of
// one policy never sees a profile that cannot serve it.
const Tech_profile*           detect_tech_profile(const Technology&, Tech_policy need = Tech_policy::route);
// "auto" detects the profile from the LEF; any other value is an explicit
// tech_profile() name. Unknown or undetectable technologies, and an explicit
// profile lacking `need`, throw.
const Tech_profile&           resolve_tech_profile(const Technology&, std::string_view requested = "auto",
                                                   Tech_policy need = Tech_policy::route);
}  // namespace livehd::physical
