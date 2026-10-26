// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "checkpoint.hpp"
#include "physical_sites.hpp"

namespace livehd::placement::buffering {
struct Producer_settings {
  size_t                 max_nets = 32, max_fanout = 4, max_option_vertices = 16;
  double                 max_seconds = 5;
  // Explicit technology horizontal R/C and vertical R/C per micron.
  std::array<double, 4>  directional_rc{};
  Physical_site_settings sites;
  Geometry_limits        geometry;
  Route_limits           route;
  Site_option_settings   options;
  Tree_search_settings   search;
};
struct Producer_net {
  std::string owner, reason;
  size_t      candidates = 0, options = 0, assignments = 0;
};
struct Producer_result {
  bool                      supported_api = false, budget_exhausted = false;
  Checkpoint_problem        problem;
  size_t                    critical_nets = 0, window_nets = 0, outside_nets = 0, omitted_nets = 0, missing_contexts = 0;
  std::vector<Producer_net> nets;
  double                    demand_seconds = 0, sites_seconds = 0, search_seconds = 0, seconds = 0;
};
// Native in-memory bounded producer for actual fresh critical/window owners and
// previously managed owners. Rebuilds original-row capacity from CURRENT exact
// instance/master metadata and fixed physical-only masks, preserving protected
// reservations. Occupied movable snapshots include at least one DBU exclusion
// margin for rounded RC component poses. Continuous terminal positions attach to the integer visibility
// seed with explicit rectilinear RC; they are never silently snapped.
//
// Uses one obstacle-avoiding shortest-path topology and finite site/cell search;
// input_search_complete is ALWAYS false. Unsupported source contexts/fanout and
// exhausted work remain unmet. No graph edit or whole-graph timing acceptance.
// Routing exclusions are explicit CLOSED masks; no layer/crossing capacity or
// over-macro RC is inferred. The caller selects fixed-only virtual or full
// occupancy inventories explicitly and supplies real physical buffer models.
Producer_result generate_checkpoint(const Checkpoint_input&, const liberty::timing::Library&, const late::Driver_options&,
                                    const physical::Technology&, const physical::Floorplan& original,
                                    const std::vector<Site_rect>& extra_placement_masks,
                                    const std::vector<Site_rect>& routing_exclusions, const std::vector<Physical_buffer_model>&,
                                    const Producer_settings&);
}  // namespace livehd::placement::buffering
