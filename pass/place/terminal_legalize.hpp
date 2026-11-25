// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "capacity_bins.hpp"
#include "core/timing_feedback.hpp"
#include "inou/lefdef/physical.hpp"
#include "prects_networks.hpp"
#include "row_occupancy.hpp"
#include "satlegalize.hpp"

namespace livehd::placement {
// This attempt could not satisfy the hard density/padding constraints or exhaustively place a cell.
// The generated-core owner may retry with more area. Invalid controls and implementation errors remain distinct.
struct Terminal_legalize_unfit : std::runtime_error {
  using std::runtime_error::runtime_error;
};
// Final LiveHD placement operation. Locks exist only during this transaction;
// successful output retains ordinary PLACED status for the OpenROAD handoff.
struct Terminal_legalize_domain {
  bool                          hold = false;  // retain the complete input pose and status
  std::optional<physical::Rect> origins;       // inclusive lower-left coordinate bounds, not footprint bounds
};
struct Terminal_legalize_options {
  // Empty leaves existing behavior unchanged; otherwise one domain per component.
  std::vector<Terminal_legalize_domain> domains;
  Prects_library                        prects;
  unsigned                              bins_x = 64, bins_y = 64;
  bool                                  route_drc   = false;  // Full rectangular trace checks for the signal-routing experiment.
  double                                max_density = .8, target_density = .5;
  double                                remainder_wire_weight = 4;
  double                                critical_fraction = .1, local_pair_fraction = .5;
  bool                                  joint_minimize_total   = true;
  unsigned                              critical_padding_sites = 0, critical_fanin_depth = 0;
  unsigned                              padding_max_extra_rows = 0;  // zero keeps mandatory per-cell padding
  // Pin-density spreading: a movable cell whose input pose lies where the signal pins exceed pin_density per um2
  // (bins of four standard rows) takes extra sites on each side, in proportion to the excess and at most
  // pin_padding_max. Every pin needs its own way up through M2 and M3, so such spots are where the detailed router
  // runs out of M3. The extra is given up for a cell that would move more than pin_padding_rows rows for it. Zero
  // disables.
  double                                pin_density            = 0;
  unsigned                              pin_padding_max = 3, pin_padding_rows = 4;
  // Pin footprint: a movable cell is at least as wide, with its padding, as its signal pins need at this many pins per
  // um2 of its own rows. It depends on the cell alone, so it holds wherever the cell goes and the same number can
  // size the cell in global placement. Zero disables. The same two limits apply.
  double                                pin_footprint        = 0;
  // The most critical cells are placed first with critical_padding_sites and keep out of both rules above unless this
  // is set (NVDLA O: 33,277 of 230,262 cells are critical, pack with no padding and keep the pin-dense spots).
  bool                                  pin_padding_critical = false;
  unsigned       padding_sites = 1, candidates = 12, pair_move_sites = 4, pair_move_rows = 1;
  unsigned       sat_conflicts = 10000000, sat_milliseconds = 1000;
  std::string    backend          = "cadical";
  int64_t        macro_halo_x_dbu = 0, macro_halo_y_dbu = 0;
  // Zero disables that alignment stage, without disabling critical-cell locks.
  unsigned       critical_pair_limit = 256, local_pair_limit = 0;

  // Permit checked pin-to-escape endpoint access with this technology profile's
  // layers; null disables it.
  const physical::Tech_profile* route_vias = nullptr;
};
struct Terminal_legalize_cell {
  size_t    index = 0;
  Ann_place before, after;
  unsigned  stage = 0, bin_distance = 0;
  bool      critical      = false;
  double    slack_ps      = 0;
  bool      timed         = false;
  unsigned  padding_sites = 0;
};
struct Terminal_legalize_report {
  size_t                              held_cells = 0, bounded_cells = 0;
  // Cells that kept extra padding for their pins, and cells that gave it up for a nearer site.
  size_t                              pin_dense_bins = 0, pin_padded_cells = 0, pin_padding_dropped = 0;
  unsigned                            bins_x = 0, bins_y = 0;
  bool                                joint_feasible = false, joint_max_optimized = false, joint_total_optimized = false;
  // Accepted alignments across critical/local stages, classified by whether
  // at least one endpoint retained its original XY/orientation or both moved.
  size_t                              anchored_pairs = 0, joint_pairs = 0;
  size_t                              joint_selected = 0, joint_dropped = 0, joint_cuts = 0;
  size_t                              joint_no_witness = 0, joint_conflict_dropped = 0;
  size_t                              joint_menu_recovered = 0, joint_pool_candidates = 0;
  size_t                              joint_static_obstructions = 0;
  bool                                joint_admission_fallback  = false;
  size_t                              joint_timeout_deferred    = 0;
  size_t                              joint_density_cuts = 0, joint_obstruction_cuts = 0, joint_corridor_cuts = 0;
  size_t                              supporting_cells = 0, full_site_scans = 0, relaxed_padding_cells = 0, wire_changed_cells = 0;
  size_t                              ignored_nets = 0;
  size_t                              timed_nets = 0, critical_nets = 0, critical_cells = 0;
  size_t                              critical_pairs = 0, local_pairs = 0, sat_trials = 0, sat_unknown = 0, sat_unsat = 0;
  size_t                              candidate_checks = 0, moved_cells = 0, crossed_cells = 0, max_bin_distance = 0;
  unsigned                            packing_retries = 0;
  double                              seconds = 0, max_displacement_dbu = 0, total_displacement_dbu = 0;
  std::vector<Capacity_bin>           bins;  // logic occupancy includes input-fixed cells and critical locks
  std::vector<Terminal_legalize_cell> cells;
  // Final validated one-edge pin alignments; indices refer to the input frame.
  std::vector<Satlegalize_alignment>  alignments;
};
// The row occupancy controls these options set (shared with the multirow packer).
Occupancy_controls occupancy_controls(const Terminal_legalize_options&);
// Bound an inherited routing grid for tiny cores before selecting/locking cells:
// each bin is at least as wide as the widest movable CORE cell over the cap and
// at least as tall as the tallest (one row unless a multi-row cell is movable).
// Explicit caller-supplied terminal grids remain authoritative unless this helper is called.
void fit_terminal_legalize_grid(const physical::Technology&, const physical::Floorplan&, Terminal_legalize_options&);
// Atomic: any failure leaves the caller's floorplan unchanged. Candidate-menu
// exhaustion is reported as a search failure, never as proof of infeasibility.
Terminal_legalize_report terminal_legalize(const physical::Technology&, physical::Floorplan&, const timing::Feedback&,
                                           const Terminal_legalize_options&);
void write_terminal_legalize_report(const Terminal_legalize_report&, const physical::Floorplan&, const Terminal_legalize_options&,
                                    const std::string& prefix);
}  // namespace livehd::placement
