// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "core/timing_whatif.hpp"
#include "gui_session.hpp"

namespace livehd::placement::gui {
// Macro floorplan checks at DEF precision. Soft blockages and standard cells
// do not constrain macro-to-macro spacing during global placement.
struct Geometry_issue {
  std::string cause;
  int         node, other = -1;
};
std::string                 floorplan_findings(const Scene&, const Frame&);
Grid                        occupancy_grid(const Scene&, const Frame&, int bins);
std::string                 grid_findings(const Scene&, const Frame&, const std::string& kind, const Grid&);
std::string                 macro_signature(const Scene&, const Frame&);
std::string                 endpoint_evidence(const Scene&, const Frame&, timing::Whatif&);
void                        build_floorplan_outline(Scene&);
double                      usable_area(const Box&, const std::vector<Box>& exclusions);
std::vector<Geometry_issue> macro_geometry(const Scene&, const Frame&);
}  // namespace livehd::placement::gui
