// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/timing_feedback.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
struct Satlegalize_route_grid {
  physical::Rect      bounds;
  unsigned            bins_x = 0, bins_y = 0;
  std::vector<double> horizontal, vertical, pin_utilization;
};
struct Satlegalize_options {
  std::string                                   backend = "cadical", encoding = "candidate";
  unsigned                                      max_cells = 24, max_candidates = 12, max_windows = 8, max_pairs = 32;
  // Zero preserves the global-only trial budget. The optional packing probe
  // distinguishes an infeasible candidate menu from an alignment conflict.
  unsigned                                      max_pairs_per_window = 0;
  bool                                          check_packing        = false;
  // Zero is unbounded. Intended for already legal post-repair placements;
  // count all moved region members, including alignment endpoints and spills.
  unsigned                                      max_moved_cells      = 0;
  unsigned                                      move_sites = 4, move_rows = 1, conflicts = 10000, milliseconds = 50;
  bool                                          skip_conflicts  = true;
  double                                        boundary_growth = .05;
  // Both zero preserve the original pair-bounding-box domain exactly.
  double                                        region_width_um = 0, region_height_um = 0;
  bool                                          spill_neighbors       = false;
  double                                        spill_slack_margin_ps = 0;
  // Exact immutable native routing snapshot. Grid mode changes menus and
  // region geometry; guidance=false is its matched cell-density control.
  bool                                          route_grid = false, route_guidance = true;
  // Zero keeps strict peak non-regression. One permits peak demand changes
  // below estimated capacity, while retaining the other route guards.
  double                                        route_peak_floor = 0;
  std::shared_ptr<const Satlegalize_route_grid> routing;
  // Placement caller supplies the active, rounded physical macro keepout.
  int64_t                                       macro_halo_x_dbu = 0, macro_halo_y_dbu = 0;
};
struct Satlegalize_alignment {
  size_t      first = 0, second = 0;
  std::string first_pin, second_pin;
};
struct Satlegalize_checkpoint {
  std::vector<std::pair<size_t, Ann_place>> positions;
};
struct Satlegalize_density_tile {
  physical::Rect bounds;
  size_t         movable_cells = 0;
  double         usable_area = 0, movable_area_before = 0, movable_area_after = 0;
  double         density_before = 0, density_after = 0;
  double         route_utilization = 0;
};
struct Satlegalize_window {
  physical::Rect                        bounds;
  size_t                                movable_cells = 0;
  bool                                  oversized     = false;
  unsigned                              trials = 0, accepted = 0, conflicts = 0, unknown = 0;
  unsigned                              moved_cells      = 0;
  double                                displacement_dbu = 0, max_displacement_dbu = 0;
  std::string                           packing_status = "not_checked";
  double                                usable_area = 0, movable_area_before = 0, movable_area_after = 0;
  double                                density_before = 0, density_after = 0;
  double                                route_utilization = 0;
  size_t                                source_gap_cells = 0, source_gap_candidates = 0;
  unsigned                              spill_candidates = 0, spilled_cells = 0;
  size_t                                candidate_cells = 0, retained_candidates = 0, spill_eligible_cells = 0;
  std::string                           first_no_candidate_cell, first_no_candidate_master, first_no_candidate_spill_reason;
  Ann_place                             first_no_candidate_place;
  std::optional<double>                 first_no_candidate_slack_ps;
  std::vector<Satlegalize_density_tile> neighbors;
};
struct Satlegalize_report {
  unsigned                            windows = 0, trials = 0, conflicts = 0, unknown = 0, boundary_rejected = 0, oversized = 0;
  double                              seconds = 0, hpwl_before = 0, hpwl_after = 0;
  unsigned                            sampled_windows = 0, spill_candidates = 0, spilled_cells = 0, density_rejected = 0;
  unsigned                            cross_grid_pairs = 0;
  std::vector<Satlegalize_window>     window_stats;
  std::vector<Ann_place>              before;
  std::vector<Satlegalize_alignment>  accepted;
  std::vector<Satlegalize_checkpoint> checkpoints;
};
struct Satlegalize_route_quality {
  double overflow = 0, peak = 0;
  int    blocked_pins = 0, unroutable_nets = 0;
};
bool satlegalize_route_preserved(const Satlegalize_route_quality& candidate, const Satlegalize_route_quality& baseline,
                                 double peak_floor = 0);
// Only complete legal local windows are installed. Fixed/macros/outside cells
// and IOs are immutable. Empty/missing routing-grid or timing evidence skips.
Satlegalize_report satlegalize(const physical::Technology&, physical::Floorplan&, const timing::Feedback&,
                               const Satlegalize_options&);
// Revalidate after ordinary legalization/detail. Caller must explicitly relax
// an invalid prefix, or restore zero and run the original legalization flow.
bool               satlegalize_preserved(const physical::Technology&, const physical::Floorplan&, const Satlegalize_report&,
                                         size_t prefix = std::numeric_limits<size_t>::max());
void               satlegalize_restore(physical::Floorplan&, const Satlegalize_report&, size_t prefix = 0);
}  // namespace livehd::placement
