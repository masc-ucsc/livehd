// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <limits>

#include "geometry.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement::buffering {
struct Physical_site_settings {
  // Macro halo applies only to non-CORE component bodies; the margin applies
  // to every occupancy/placement mask. Extra masks are already halo-expanded.
  int64_t macro_halo_dbu = 0, legalization_margin_dbu = 0;
  // true for materialization snapshots; false is an explicitly virtual fixed-
  // occupancy inventory and cannot certify placement against movable cells.
  bool    occupy_movable = true;
};
struct Physical_sites {
  int64_t                                     dbu = 0;
  Legal_sites                                 legal;
  std::map<std::string, physical::Row>        rows;
  std::map<int64_t, std::vector<std::string>> rows_by_y;
  size_t                                      occupied_components = 0, skipped_movable = 0, placement_masks = 0;
  bool                                        occupies_movable = false;
};
// Uses ORIGINAL rows, current component poses and all placement blockages.
// The caller supplies any physical-only occupancy absent from this Floorplan.
// Non-N/FS rows, unplaced required bodies, unknown/virtual masters, stale moving
// blockage associations, unit/coordinate overflow and overlapping rows throw.
// Soft blockages are conservatively excluded. No routing capacity is inferred.
Physical_sites build_physical_sites(const physical::Technology&, const physical::Floorplan&,
                                    const std::vector<Site_rect>& extra_placement_masks = {}, const Physical_site_settings& = {});
struct Physical_buffer_model {
  Cell_ports  ports;
  std::string site;
  int64_t     width_dbu = 0, height_dbu = 0;
  // N orientation, same first-shape centroid/origin convention as native RC.
  Point       input_offset_dbu, output_offset_dbu;
  bool        mirror_x = false;
};
struct Physical_model_result {
  std::optional<Physical_buffer_model> model;
  std::string                          reason;
};
// A real single-input noninverting NLDM buffer with physical CORE/SITE/pins.
// Unsupported cells are explicit, not a guessed footprint/pin/capacitance.
Physical_model_result physical_buffer_model(const physical::Technology&, const liberty::timing::Library&, const Cell_ports&);
struct Site_option_settings {
  int64_t max_attachment_dbu   = 0;
  size_t  max_candidate_checks = 100000, max_options_per_vertex = 64;
  size_t  max_options_per_model_per_vertex = std::numeric_limits<size_t>::max();
  double  max_seconds                      = 1;
};
enum class Site_option_status { complete, budget_exhausted };
struct Site_option_result {
  Site_option_status                    status = Site_option_status::complete;
  std::vector<std::vector<Tree_option>> options;
  std::map<std::string, uint64_t>       capacity;
  size_t                                candidate_checks = 0, routing_rejected = 0, emitted = 0;
  double                                seconds = 0;
};
// Enumerate legal starts within BOTH pins' declared Manhattan attachment bound.
// Match actual SITE/height/orientation, exact occupied spans and shared owners.
// Round footprints up to whole sites; this conservative inventory can omit a
// physically clear fractional-site gap. Empty options do not prove infeasibility.
// Check vertex->input and output->vertex X-then-Y wires against CLOSED routing
// masks, independently of placement masks. Return symbolic row-slot capacities
// for Context; these constraints do not add a second physical density charge.
// Other owners stay unavailable, own footprints may be reused. No commit occurs.
// A per-vertex option limit retains partial options there and continues other
// vertices; global candidate/time limits stop generation. Any exhausted set is partial; a caller must not claim an
// exhaustive graph/tree search from it. Layer/crossing capacity, full timing and
// physical materialization validation remain separate acceptance requirements.
Site_option_result physical_site_options(const Physical_sites&, const Site_reservations&, const std::string& owner,
                                         const std::vector<Route_point>& vertices, const std::vector<Physical_buffer_model>&,
                                         const std::vector<Site_rect>&   routing_exclusions, const Site_option_settings&);
}  // namespace livehd::placement::buffering
