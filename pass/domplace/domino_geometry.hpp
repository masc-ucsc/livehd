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
// One PG edge rail of the reference inverter in rail-local DBU: y about the
// row edge, xl as the overhang before x = 0 and xh as the overhang past the
// width.
struct Domino_edge_rail {
  std::string                  pin, use;
  std::vector<physical::Shape> shapes;
};
// The reference inverter's LEF geometry, resolved once: A_NOT on the LEF
// basis, the core SITE and the PG rail pattern the abstracts repeat.
struct Domino_footprint {
  std::string                     inverter;  // the LEF macro it was resolved from
  std::string                     site;
  int64_t                         site_width = 0, row_height = 0, grid = 1, area = 0;  // DBU, area in DBU^2
  std::array<Domino_edge_rail, 2> rails;                                               // bottom (y = 0) and top (y = H) edges
  int64_t                         rail_reach = 0;                                      // farthest rail extent from its edge
};
struct Geometry_inputs {
  const physical::Technology*     tech = nullptr;
  Domino_footprint                footprint;
  physical::Pin_access_layers     access;          // vertical pin metal, horizontal escape, cut
  std::string                     vertical_layer;  // stand-in vertical-access check layer
  physical::Row_access_convention row_access;
};
// The route policy of the Tech_profile the LEF identifies. A technology
// without a registered profile uses the bottom of its LEF routing stack: pin
// metal = routing layer 0, escape = layer 1, their cut = the cut layer of a LEF
// via joining the two, branch = layer 2. `source` records the choice
// ("tech-profile:<name>" or "lef-stack"). A LEF matching two profiles or a
// stack too short to derive throws.
physical::Route_policy              domino_route_policy(const physical::Technology&, std::string* source);
// The named reference inverter's footprint (it must be a one-row CORE cell
// on a LEF SITE with distinct PG rails on both row edges, else this throws),
// with the route policy's access and branch_layer (the stand-in
// vertical-access layer).
Geometry_inputs geometry_inputs(const physical::Technology&, const std::string& inverter, const physical::Route_policy&,
                                physical::Row_access_convention = {});
// Row access of the local rows: local row h has the access of core row h, so
// a template fits N on even core rows and FS on odd rows of generated N/FS
// frames. Both rail templates share it.
std::array<physical::Row_access, 2> template_halves(const physical::Row_access_convention&);
// PG rail templates. Rows alternate N/FS, so the rail on a row edge alternates
// between the reference inverter's bottom and top rail, and a two-row cell
// spans three edges: outer, inner, outer. Template t0 puts the inverter's
// bottom rail (rails[0]) on its outer edges and fits starts on rows oriented
// like the inverter (N, FN); t1 puts its top rail there and fits flipped
// starts (FS, S). The PG pin names bottom to top (y = 0, H, 2H); a template
// other than 0 or 1 throws.
std::array<std::string, 3>          template_rails(const Domino_footprint&, uint32_t rail_template);
struct Domino_variant_geometry {
  std::string                         name;
  double                              x = 0, a_not_um2 = 0, a_register_um2 = 0;
  double                              a_reset_um2     = 0;  // U22: the storage reset's footprint term (0 without RST)
  double                              a_estimate_um2  = 0;  // A_domino(s)
  double                              a_footprint_um2 = 0;  // width * height
  // DBU. width = site_round_up(max(w_area, w_pin)); w_area is already whole sites.
  int64_t                             w_area = 0, w_pin = 0, width = 0, height = 0;
  physical::Macro                     macro;
  std::array<physical::Row_access, 2> halves{};
  uint32_t                            rail_template = 0;  // the _t<k> geometry identity
  std::array<std::string, 3>          pg_rails;           // PG pin at y = 0, H, 2H (template_rails)
};
// CLASS CORE on the inverter's SITE, SIZE width BY 2H, SYMMETRY X Y, ORIGIN 0 0.
// Signal pins (rails, CLK with USE CLOCK, Q) are vertical strips on the access
// pin layer, one vertical-layer track slot each, with one rectangle per local
// row clear of the rail bands; shape 0 lies in the horizontal-access row and
// is the placement RC point. Slots cover every site x-phase of the vertical
// track lattice in all four orientations (N, FS, FN, S). w_pin = (pins + 1) * slot pitch, the slot pitch being the pin
// pitch, or more when the strip plus spacing needs it. PG pins repeat the
// inverter's edge rails at 0, H and 2H over the full width in the rail stack
// of `rail_template` (template_rails): t0 and t1 differ only there, so one
// fits each start-row parity with the same function, pins and drive.
// A_domino(s) = x * A_NOT + A_register(s) with 2x = domino_proxy_devices, as
// in domino_area, here on the LEF footprint (placement) basis: A_NOT is the
// reference inverter's footprint and A_register(s) the footprint of
// reference.registers[variant], charged once. Variants share the PDN term.
// width_override (DBU, whole sites) keeps the layout unchanged so a narrow
// width can be refused by check_domino_access. Invalid inputs, including a
// reference inverter other than the footprint's, throw.
Domino_variant_geometry generate_domino_macro(const usyn::Physical_cell_interface& cell, const usyn::Gate_formula::Metrics& pdn,
                                              const Domino_reference& reference, size_t variant, const Geometry_inputs& in,
                                              std::optional<int64_t> width_override = {}, uint32_t rail_template = 0);
struct Access_case {
  size_t                start_row     = 0;  // core row offset; parity per the convention
  physical::Orientation orientation   = physical::Orientation::N;
  bool                  compatible    = false;  // local rows match the convention
  bool                  ok            = false;  // compatible and no failure
  bool                  pg_rows_match = false;  // rails match the generated N/FS rows (else not a placement: no pin check)
};
struct Access_check {
  bool                     ok = false;   // no failure and nothing unsupported
  std::vector<std::string> failures;     // each names the pin (or PG), start row, orientation and x-phase
  std::vector<std::string> unsupported;  // pin-access engine refusals of technology rules: unverified, not failed
  std::vector<Access_case> cases;
  size_t                   x_phases = 0;
  std::array<bool, 2>      parity{};  // some case ok on even / odd start rows
};
// Header (CORE, SITE, 2H, SYMMETRY X Y) and outline checks, then every start
// row of both parities (and every escape-track y-phase) x orientations N, FS,
// FN, S x every site/track x-phase. On rows whose access matches the template,
// PG rails must lie on all three row edges; where they also have the polarity
// of the generated N/FS rows (row 0 N), each signal pin needs horizontal
// access (enumerate_pin_access, filtered to that row) or the stand-in vertical
// rule (a strip in that row holding a vertical_layer wire on a track, at least
// one escape pitch long). A template whose rails fit no start fails; each rail
// template fits one parity, so the bundle checks that t0 and t1 together
// cover both. Tracks come from the LEF pitch and
// offset with the first core row and site at the lattice origin. `scratch` is
// a caller-owned copy of the technology that the check adds its master to;
// one copy serves every call.
Access_check check_domino_access(const Geometry_inputs&, const physical::Macro&, const std::array<physical::Row_access, 2>& halves,
                                 physical::Technology& scratch);
}  // namespace livehd::domplace
