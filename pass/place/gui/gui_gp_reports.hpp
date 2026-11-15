// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "gui_gp_options.hpp"

// Private same-build wire format: never include memory padding or process-local pointers.
// Decomposition requires an explicit audit when a DREAMPlace report gains a field.
#define LIVEHD_GP_REPORT(...)                                                                                       \
  auto& [__VA_ARGS__] = record_;                                                                                    \
  auto visit          = [&](auto&... items) { (DreamPlace::native::gp_wire_detail::input(archive_, items), ...); }; \
  visit(__VA_ARGS__)
namespace DreamPlace {
template <class A>
void field(A& archive_, MacroAlignConstraint& record_) {
  LIVEHD_GP_REPORT(a, b, axis, weight, offset);
}
}  // namespace DreamPlace
namespace DreamPlace::native {
template <class A>
void field(A& archive_, FloorplanCircle& record_) {
  LIVEHD_GP_REPORT(name, x, y, radius, area, family, parent);
}
template <class A>
void field(A& archive_, FloorplanRelation& record_) {
  LIVEHD_GP_REPORT(kind, axis, nodes, value);
}
template <class A>
void field(A& archive_, FloorplanSnap& record_) {
  LIVEHD_GP_REPORT(circles,
                   relations,
                   attempted,
                   rejected,
                   infeasible_families,
                   seconds,
                   maximum_overlap,
                   maximum_displacement,
                   maximum_boundary_violation,
                   feasible);
}
template <class A>
void field(A& archive_, AlignmentGroup& record_) {
  LIVEHD_GP_REPORT(align_class, axis, target, nodes);
}
template <class A>
void field(A& archive_, AlignmentReport& record_) {
  LIVEHD_GP_REPORT(regularity,
                   constraints,
                   groups,
                   x_groups,
                   y_groups,
                   members,
                   both_axes,
                   exact_groups,
                   broken_groups,
                   soft_violations,
                   macro_displacements,
                   macro_legalization_seconds,
                   cell_legalization_seconds);
}
template <class A>
void field(A& archive_, GpSample& record_) {
  LIVEHD_GP_REPORT(iteration,
                   overflow,
                   hpwl,
                   density_weight,
                   route_overflow,
                   inflation_area_ratio,
                   macro_overlaps,
                   macro_stacks,
                   raw_overflow,
                   model_density,
                   filler_charge_scale,
                   align_pairs,
                   align_residual,
                   align_ramp,
                   centroid_overlaps,
                   centroid_max_overlap,
                   centroid_repulsion_weight,
                   route_peak_utilization,
                   macro_routing_force,
                   macro_routing_channels,
                   align_force,
                   centroid_align_pairs,
                   centroid_align_residual,
                   centroid_align_ramp,
                   centroid_align_force,
                   centroid_parent_edges,
                   centroid_parent_max_distance,
                   centroid_parent_force,
                   route_model,
                   route_seconds,
                   route_switch_rudy,
                   route_switch_rsmt,
                   route_switch_iteration,
                   route_bins,
                   route_sampled,
                   route_probe,
                   route_blocked_pins,
                   route_unroutable_nets,
                   route_calls,
                   route_rudy_seconds,
                   route_rsmt_seconds,
                   route_uroute_seconds,
                   route_external_seconds,
                   route_uroute_switch_iteration,
                   route_switch_uroute_rsmt,
                   route_switch_uroute,
                   route_probe_overflow,
                   magnet_round,
                   magnet_groups,
                   macro_move_max,
                   macro_move_rms,
                   last_instability_iteration,
                   macro_opacity,
                   macro_exclusion_scale,
                   local_density_weight,
                   filler_count,
                   filler_area,
                   filler_size_x,
                   filler_size_y,
                   pin_timing_cost,
                   reserved_area,
                   materialized_reserved_area,
                   effective_filler_area,
                   overflow_floor);
}
template <class A>
void field(A& archive_, CentroidReport& record_) {
  LIVEHD_GP_REPORT(started,
                   completed,
                   start_iteration,
                   iterations,
                   force_seconds,
                   alignment_seconds,
                   parent_seconds,
                   attraction_coefficient,
                   attraction_calibration_iteration,
                   circles_converged,
                   circle_overlaps,
                   max_circle_overlap,
                   effective_repulsion_weight);
}
template <class A>
void field(A& archive_, DetailedSample& record_) {
  LIVEHD_GP_REPORT(kernel, before, after, legal, macros_preserved, accepted, legality_issue);
}
template <class A>
void field(A& archive_, GpRollbackSample& record_) {
  LIVEHD_GP_REPORT(iteration, restored_iteration);
}
template <class A>
void field(A& archive_, GpCheckpointSample& record_) {
  LIVEHD_GP_REPORT(iteration, overflow, invoked, forced);
}
template <class A>
void field(A& archive_, CentroidSample& record_) {
  LIVEHD_GP_REPORT(x, y, radius, area, cells, macros);
}
template <class A>
void field(A& archive_, MacroPull& record_) {
  LIVEHD_GP_REPORT(node, x, y, weight, jump, length);
}
template <class A>
void field(A& archive_, Result& record_) {
  LIVEHD_GP_REPORT(magnet,
                   magnet_seconds,
                   checkpoint_history,
                   density_iteration,
                   continuation_deadline,
                   objective_converged,
                   continuation_limit,
                   overflow_floor,
                   density_stalled,
                   route_stalled,
                   post_density_plateau,
                   post_density_best_iteration,
                   timing_observed,
                   timing_satisfied,
                   centroid,
                   alignment,
                   bins_x,
                   bins_y,
                   poisson_seconds,
                   macro_routing_seconds,
                   rudy_seconds,
                   rudy_calls,
                   route_external_seconds,
                   route_external_calls,
                   route_model,
                   route_switch_iteration,
                   route_switch_rudy,
                   route_switch_rsmt,
                   route_probed,
                   route_uroute_switch_iteration,
                   route_switch_uroute_rsmt,
                   route_switch_uroute,
                   route_probe_overflow,
                   uroute_skipped,
                   uroute_sample_ids,
                   filler_count,
                   filler_area,
                   filler_size_x,
                   filler_size_y,
                   filler_count_limited,
                   reserved_area,
                   materialized_reserved_area,
                   effective_filler_area,
                   physical_overflow,
                   macro_alignment_seconds,
                   global_placement_seconds,
                   detailed_placement_seconds,
                   history,
                   detailed_history,
                   iterations,
                   selected_iteration,
                   local_density_seconds,
                   local_density_force,
                   macro_cell_density_seconds,
                   macro_cell_density_force,
                   macro_cell_density_screening_length,
                   macro_cell_density_coefficient,
                   macro_density_pull_seconds,
                   macro_density_pull_force,
                   macro_density_pull_anchor_ratio,
                   local_density_grid_seconds,
                   timing_spread_cells,
                   timing_spread_min_mobility,
                   density_weight,
                   initial_hpwl,
                   hpwl,
                   overflow,
                   converged,
                   legal,
                   rollback_history);
}
}  // namespace DreamPlace::native
#undef LIVEHD_GP_REPORT
