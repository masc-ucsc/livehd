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
  p.access              = {"M1", "M2", "V1"};
  p.branch_layer        = "M3";
  p.branch_cut          = "V2";
  p.signal_layers       = {"M2", "M3", "M4", "M5", "M6", "M7"};
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
  pg.stripes            = {{"M5", .120, .072, 5.4, .300}, {"M6", .288, .096, 5.4, .513}};
  pg.connect            = {{{"M1", "M2"}}, {{"M2", "M5"}}, {{"M5", "M6"}}};
  pg.pin_layer          = "M6";
  pg.macro_halo         = 2;
  pg.macro_connect      = {{{"M4", "M5"}}};
  pg.nets               = {"VDD", "VSS"};
  pg.pin_names          = {{{"VDD", "VDDPE", "VDDCE"}, {"VSS", "VSSE"}}};
  return p;
}
const std::vector<Tech_profile>& registry() {
  static const std::vector<Tech_profile> profiles{asap7()};
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
const Tech_profile& tech_profile(std::string_view name) {
  for (const auto& p : registry()) {
    if (name == p.name || (!p.pg.name.empty() && name == p.pg.name)) {
      return p;
    }
  }
  throw std::invalid_argument("unknown technology profile: " + std::string(name) + " (known: " + known_profiles() + ")");
}
const Tech_profile& resolve_tech_profile(const Technology& tech, std::string_view requested) {
  if (requested != "auto") {
    return tech_profile(requested);
  }
  const Tech_profile* found = nullptr;
  for (const auto& p : registry()) {
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
  if (!found) {
    throw std::invalid_argument("cannot detect a supported technology profile from the LEF (known: " + known_profiles() + ")");
  }
  return *found;
}
}  // namespace livehd::physical
