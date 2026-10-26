// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "domino_reference.hpp"
#include "inou/lefdef/physical.hpp"
#include "inou/lefdef/row_access.hpp"
#include "inou/lefdef/tech_profile.hpp"
#include "physical_emission.hpp"

namespace livehd::domplace {
// Provisional two-row LEF abstracts for domino masters (2d-domplace C) and the
// transformed pin-access check that F repeats on placed frames.
//
struct Geometry_inputs {
  const physical::Technology*     tech = nullptr;
  std::string                     inverter;        // named reference inverter: A_NOT, core SITE, PG rail pattern
  physical::Pin_access_layers     access;          // vertical pin metal, horizontal escape, cut
  std::string                     vertical_layer;  // stand-in vertical-access check layer
  physical::Row_access_convention row_access;
};
// The route policy of the Tech_profile the LEF identifies. A technology
// without a registered profile uses the bottom of its LEF routing stack: pin
// metal = routing layer 0, escape = layer 1, their cut = the cut layer of a LEF
// via joining the two, branch = layer 2. `source` records the choice
// ("tech-profile:<name>" or "lef-stack"). A stack too short to derive throws.
physical::Route_policy              domino_route_policy(const physical::Technology&, std::string* source);
// From a Tech_profile route policy: access and branch_layer (the stand-in
// vertical-access layer). Technologies without a profile pass a Route_policy.
Geometry_inputs geometry_inputs(const physical::Technology&, std::string inverter, const physical::Route_policy&,
                                physical::Row_access_convention = {});
// Template t0: local row h has the access of core row h, so t0 fits N on the
// bottom core row (and FS on odd rows of generated N/FS frames).
std::array<physical::Row_access, 2> template_halves(const physical::Row_access_convention&);
struct Domino_variant_geometry {
  std::string                         name;
  double                              x = 0, a_not_um2 = 0, a_register_um2 = 0;
  double                              a_estimate_um2  = 0;  // A_domino(s)
  double                              a_footprint_um2 = 0;  // width * height
  // DBU. width = site_round_up(max(w_area, w_pin)); w_area is already whole sites.
  int64_t                             w_area = 0, w_pin = 0, width = 0, height = 0;
  physical::Macro                     macro;
  std::array<physical::Row_access, 2> halves{};  // template t0
};
// CLASS CORE on the inverter's SITE, SIZE width BY 2H, SYMMETRY X Y, ORIGIN 0 0.
// Signal pins (rails, CLK with USE CLOCK, Q) are vertical strips on the access
// pin layer, one vertical-layer track slot each, with one rectangle per local
// row clear of the rail bands; shape 0 lies in the horizontal-access row and
// is the placement RC point. Slots cover every site x-phase of the vertical
// track lattice. w_pin = (pins + 1) * slot pitch, the slot pitch being the pin
// pitch, or more when the strip plus spacing needs it. PG pins repeat the
// inverter's edge rails at 0, H and 2H (bottom polarity at 0 and 2H) over the
// full width; rail parity is not restricted (provisional model assumption).
// A_domino(s) = x * A_NOT + A_register(s) with x = (N_PDN + aux) / 2 as in
// domino_area, here on the LEF footprint (placement) basis: A_NOT is the
// reference inverter's footprint and A_register(s) the footprint of
// reference.registers[variant], charged once. Variants share the PDN term.
// width_override (DBU, whole sites) keeps the layout unchanged so a narrow
// width can be refused by check_domino_access. Invalid inputs, including a
// reference inverter other than in.inverter, throw.
Domino_variant_geometry generate_domino_macro(const usyn::Physical_cell_interface& cell, const usyn::Gate_formula::Metrics& pdn,
                                              const Domino_reference& reference, size_t variant, const Geometry_inputs& in,
                                              std::optional<int64_t> width_override = {});
struct Access_case {
  size_t                start_row     = 0;  // core row offset; parity per the convention
  physical::Orientation orientation   = physical::Orientation::N;
  bool                  compatible    = false;  // local rows match the convention
  bool                  ok            = false;  // compatible and no failure
  bool                  pg_rows_match = false;  // rails match generated N/FS rows of the inverter (report only)
};
struct Access_check {
  bool                     ok = false;   // no failure and nothing unsupported
  std::vector<std::string> failures;     // each names the pin (or PG), start row, orientation and x-phase
  std::vector<std::string> unsupported;  // pin-access engine refusals of technology rules: unverified, not failed
  std::vector<Access_case> cases;
  size_t                   x_phases = 0;
};
// Header (CORE, SITE, 2H, SYMMETRY X Y) and outline checks, then every start
// row of both parities (and every escape-track y-phase) x orientations N, FS,
// FN, S x every site/track x-phase. On rows whose access matches the template,
// each signal pin needs horizontal access (enumerate_pin_access, filtered to
// that row) or the stand-in vertical rule (a strip in that row holding a
// vertical_layer wire on a track, at least one escape pitch long), and PG
// rails must lie on all three row edges. Tracks come from the LEF pitch and
// offset with the first core row and site at the lattice origin.
Access_check check_domino_access(const Geometry_inputs&, const physical::Macro&, const std::array<physical::Row_access, 2>& halves);
}  // namespace livehd::domplace
