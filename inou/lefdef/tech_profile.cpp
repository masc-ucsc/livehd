// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tech_profile.hpp"

#include <algorithm>
#include <stdexcept>

namespace livehd::physical {
namespace {
// ASAP7 7.5-track R library, following the pinned ORFS platform scripts.
Tech_profile asap7() {
  Tech_profile p;
  p.name                = "asap7";
  p.detect_sites        = {"asap7sc7p5t"};
  p.detect_macros       = {"TAPCELL_ASAP7_75t_R"};
  p.route.access        = {"M1", "M2", "V1"};
  p.route.branch_layer  = "M3";
  p.route.branch_cut    = "V2";
  p.route.signal_layers = {"M2", "M3", "M4", "M5", "M6", "M7"};
  p.io_pin_layers       = {"M4", "M5"};
  p.tap_master          = "TAPCELL_ASAP7_75t_R";
  p.tap_distance        = 25;
  p.tap_row_halo        = 2;
  p.hold_buffer         = "BUFx2_ASAP7_75t_R";
  auto& pg              = p.pg;
  pg.name               = "pg_asap7";
  pg.reference_revision = "fb65d5b487d2afd443fc1892330f622c776a57ac";
  pg.reference_file     = "flow/platforms/asap7/openRoad/pdn/grid_strategy-M1-M2-M5-M6.tcl";
  pg.followpin_layers   = {"M1", "M2"};
  pg.stripes            = {
      {"M5", .120, .072, 5.4, .300},
      {"M6", .288, .096, 5.4, .513}
  };
  pg.connect     = {{{"M1", "M2"}}, {{"M2", "M5"}}, {{"M5", "M6"}}};
  pg.pin_layer   = "M6";
  // CORE_macro_grid_1 (R0 R180 MX MY) and CORE_macro_grid_2 (R90 R270 MXR90 MYR90): halo 2, no straps.
  pg.macro_grids = {
      {{Orientation::N, Orientation::S, Orientation::FS, Orientation::FN}, 2, {}, {{{"M4", "M5"}}}},
      {{Orientation::W, Orientation::E, Orientation::FE, Orientation::FW}, 2, {}, {{{"M4", "M5"}}}},
  };
  pg.nets      = {"VDD", "VSS"};
  pg.pin_names = {
      {{"VDD", "VDDPE", "VDDCE"}, {"VSS", "VSSE"}}
  };
  return p;
}
// NanGate45 (FreePDK45), following the pinned ORFS platform scripts. LiveHD has
// no supported routing policy for this stack (metal1 pins are horizontal), so
// the profile provides only the power grid and handoff policy; routing consumers
// keep their LEF-stack fallback (Tech_policy::route never detects it).
Tech_profile nangate45() {
  Tech_profile p;
  p.name                = "nangate45";
  p.detect_sites        = {"FreePDK45_38x28_10R_NP_162NW_34O"};
  p.io_pin_layers       = {"metal5", "metal6"};  // IO_PLACER_H / IO_PLACER_V
  p.tap_master          = "TAPCELL_X1";
  p.tap_distance        = 120;
  p.tap_row_halo        = 2;
  p.hold_buffer         = "BUF_X1";
  auto& pg              = p.pg;
  pg.name               = "pg_nangate45";
  pg.reference_revision = "fb65d5b487d2afd443fc1892330f622c776a57ac";
  pg.reference_file     = "flow/platforms/nangate45/grid_strategy-M1-M4-M7.tcl";
  pg.followpin_layers   = {"metal1"};
  // pdngen spacing defaults to pitch / nets - width when the script omits it.
  pg.stripes            = {
      {"metal4", .48, 27.52, 56, 2},
      {"metal7", 1.4,  13.6, 30, 2}
  };
  pg.connect     = {{{"metal1", "metal4"}}, {{"metal4", "metal7"}}};
  pg.pin_layer   = "metal7";
  pg.macro_grids = {
      {{Orientation::N, Orientation::S, Orientation::FS, Orientation::FN},
       2, {{"metal5", .93, 4.07, 10, 2}, {"metal6", .93, 4.07, 10, 2}},
       {{{"metal4", "metal5"}}, {{"metal5", "metal6"}}, {{"metal6", "metal7"}}}},
      {{Orientation::W, Orientation::E, Orientation::FE, Orientation::FW},
       2,                              {{"metal6", .93, 19.07, 40, 2}},
       {{{"metal4", "metal6"}}, {{"metal6", "metal7"}}}                        },
  };
  pg.nets      = {"VDD", "VSS"};
  pg.pin_names = {
      {{"VDD", "VDDPE", "VDDCE"}, {"VSS", "VSSE"}}
  };
  return p;
}
const std::vector<Tech_profile>& registry() {
  static const std::vector<Tech_profile> profiles{asap7(), nangate45()};
  return profiles;
}
std::string known_profiles() {
  std::string names;
  for (const auto& p : registry()) {
    names += (names.empty() ? "" : ", ") + p.name;
  }
  return names;
}
}  // namespace
const Pg_macro_grid* Pg_strategy::macro_grid(Orientation orientation) const {
  const Pg_macro_grid* found = nullptr;
  for (const auto& grid : macro_grids) {
    if (grid.orientations.empty()
        || std::find(grid.orientations.begin(), grid.orientations.end(), orientation) != grid.orientations.end()) {
      if (found) {
        // pdngen applies one instance grid per macro: overlapping orientation sets have no defined winner.
        throw std::invalid_argument("power-grid strategy " + name + " has more than one macro grid for an orientation");
      }
      found = &grid;
    }
  }
  return found;
}
bool Tech_profile::provides(Tech_policy need) const {
  switch (need) {
    case Tech_policy::route     : return !route.access.pin.empty();
    case Tech_policy::power_grid: return !pg.name.empty();
  }
  return false;
}
std::span<const Tech_profile> tech_profiles() { return registry(); }
const Tech_profile&           tech_profile(std::string_view name) {
  for (const auto& p : registry()) {
    if (name == p.name || (!p.pg.name.empty() && name == p.pg.name)) {
      return p;
    }
  }
  throw std::invalid_argument("unknown technology profile: " + std::string(name) + " (known: " + known_profiles() + ")");
}
const Tech_profile* detect_tech_profile(const Technology& tech, Tech_policy need) {
  const Tech_profile* found = nullptr;
  for (const auto& p : registry()) {
    if (!p.provides(need)) {
      continue;
    }
    const auto site  = [&](const auto& name) { return tech.sites.contains(name); };
    const auto macro = [&](const auto& name) { return tech.macros.contains(name); };
    if (std::none_of(p.detect_sites.begin(), p.detect_sites.end(), site)
        && std::none_of(p.detect_macros.begin(), p.detect_macros.end(), macro)) {
      continue;
    }
    if (found) {
      throw std::invalid_argument("ambiguous technology profile: LEF matches " + found->name + " and " + p.name);
    }
    found = &p;
  }
  return found;
}
const Tech_profile& resolve_tech_profile(const Technology& tech, std::string_view requested, Tech_policy need) {
  if (requested != "auto") {
    const auto& p = tech_profile(requested);
    if (!p.provides(need)) {
      throw std::invalid_argument("technology profile " + p.name + " has no "
                                  + (need == Tech_policy::route ? "routing policy" : "native power-grid strategy"));
    }
    return p;
  }
  if (const auto* found = detect_tech_profile(tech, need)) {
    return *found;
  }
  throw std::invalid_argument("cannot detect a supported technology profile from the LEF (known: " + known_profiles() + ")");
}
}  // namespace livehd::physical
