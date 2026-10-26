// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <chrono>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// All dimensions are floorplan DBU. min_row_width is normally twice the endcap
// width, before the two extra site widths required for cut segments.
std::vector<physical::Row>       cut_macro_rows(const physical::Floorplan& frame, const physical::Technology& tech, int64_t halo_x,
                                                int64_t halo_y, int64_t min_row_width);
// Generate fixed left/right endcaps for single-height horizontal rows, using
// one master for both edges (Tech_profile::tap_master). Returned components are
// reservations to insert BEFORE cell legalization. No top/bottom/corner master selection.
// Names are deterministic LiveHD names, not OpenROAD's traversal-order indices.
std::vector<physical::Component> row_endcaps(const physical::Floorplan& frame, const physical::Technology& tech,
                                             const std::string& master_name, const std::string& prefix = "LHD_ENDCAP_");
// Insert interior taps after row cuts and endcap reservations. distance is the
// OpenROAD tapcell distance in DBU (Tech_profile::tap_distance), not the
// staggered row pitch.
// Supports a single site with N/FS rows; movable cells do not obstruct taps.
std::vector<physical::Component> row_taps(const physical::Floorplan& frame, const physical::Technology& tech,
                                          const std::string& master_name, int64_t distance, const std::string& prefix = "LHD_TAP_");
struct Repeater_site_request {
  std::string name, master;
  double      center_x = 0, center_y = 0;  // desired center, floorplan DBU
};
// Reserve single-height CORE repeaters in cut N/FS rows without moving any
// existing component. Greedy nearest-center Manhattan search in request order;
// failure (including deadline) returns no partial reservations. Invalid input
// throws. Caller publishes these components only with the accepted whole tree.
std::optional<std::vector<physical::Component>> reserve_repeater_sites(const physical::Floorplan&, const physical::Technology&,
                                                                       const std::vector<Repeater_site_request>&,
                                                                       std::chrono::steady_clock::time_point deadline);
}  // namespace livehd::placement
