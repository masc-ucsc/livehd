// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <memory>
#include <tuple>
#include <utility>

#include "core/binary_message.hpp"
#include "dreamplace/native/placement.hpp"

namespace DreamPlace::native {
template <class A>
void field(A& a, MagnetOptions& v) {
  livehd::binary_message::fields(a,
                                 v.enabled,
                                 v.before_snap,
                                 v.settle_displacement,
                                 v.start_displacement,
                                 v.max_gap,
                                 v.density_fraction,
                                 v.settle_window,
                                 v.max_per_round,
                                 v.max_rounds);
}
template <class A>
void field(A& a, GpSearch& v) {
  livehd::binary_message::fields(a,
                                 v.enabled,
                                 v.start,
                                 v.interval,
                                 v.steps,
                                 v.rounds,
                                 v.filler_step,
                                 v.local_step,
                                 v.route_tolerance,
                                 v.timing_tolerance,
                                 v.hpwl_tolerance);
}
template <class A>
void field(A& a, GpTrialTuning& v) {
  livehd::binary_message::fields(a, v.filler_charge, v.local_weight);
}
template <class A>
void field(A& a, MacroTransparency& v) {
  livehd::binary_message::fields(a, v.cycles, v.warmup, v.period, v.always);
}
template <class A>
void field(A& a, FloorplanOptions& v) {
  livehd::binary_message::fields(a,
                                 v.contain_circles,
                                 v.alignment_tolerance,
                                 v.spacing_tolerance,
                                 v.spacing_penalty,
                                 v.symmetry_tolerance,
                                 v.max_displacement);
}
template <class A>
void field(A& a, RoutePolicy& v) {
  livehd::binary_message::fields(a,
                                 v.model,
                                 v.switch_overflow,
                                 v.target_overflow,
                                 v.hold,
                                 v.degree,
                                 v.accuracy,
                                 v.probe,
                                 v.probe_iterations);
}
// Same-build numeric recipe. Keep aggregate decomposition exhaustive so adding
// a DREAMPlace option cannot silently change a private solver's trajectory.
// The named members appear only once; both directions walk exactly this order.
namespace gp_wire_detail {
template <class T>
constexpr bool runtime_handle = false;
template <class T>
constexpr bool runtime_handle<std::shared_ptr<T>> = true;
template <class T>
constexpr bool runtime_handle<std::function<T>> = true;
template <class A, class T>
void input(A& a, T& v) {
  if constexpr (runtime_handle<T>) {
    if (v) {
      throw std::invalid_argument("native GP with an external callback or runtime requires a host evaluator");
    }
  } else {
    using livehd::binary_message::field;
    field(a, v);
  }
}
}  // namespace gp_wire_detail
template <class A>
void field(A& a, Options& o) {
#define LIVEHD_GP_INPUTS                                                                                                           \
  magnet, macro_merge_check, magnet_runtime, macro_motion_observer, macro_step_observer, iteration_offset, gp_search,              \
      trial_tuning, trial_timing, trial_observer, macro_transparency, vanished_memory, vanished_memory_initial_scale,              \
      vanished_memory_growth_iterations, vanished_memory_start, vanished_memory_local_floor, macro_pull_interval,                  \
      macro_pull_after_density, macro_pull_settle_iterations, macro_pull_update, macro_pull_max_weight, macro_density_margin,      \
      terminal_interval, terminal_update, movable_interval, movable_update, interact_interval, interact_centroids,                 \
      interact_optimizer_identity, interact, checkpoint_interval, checkpoint_overflow_milestones, checkpoint_start,                \
      checkpoint_final, checkpoint_after_density, checkpoint_observer, checkpoint, min_slice_iterations, max_iterations, bins_x,   \
      bins_y, automatic_bins, exact_macro_density, dynamic_bins, bin_max, auxiliary_bins, local_density_weight,                    \
      local_density_range, local_density_jitter, local_density_target, local_density_bins, timing_spread_weight,                   \
      macro_cell_density_weight, macro_cell_density_reach, macro_density_pull_weight, macro_density_pull_range,                    \
      exact_cell_density, adaptive_density, improved_routability, scale_macro_density, routability_macro_weight,                   \
      routability_macro_range, routability_charge_compensation, progress_observer, wirelength_gamma_end, wirelength_gamma_start,   \
      fixed_macro_overlap_weight, macro_overlap_weight, macro_jitter, macro_align_weight, macro_align_sigma,                       \
      macro_align_overlap_boost, macro_align_start, macro_align_full, macro_align_nearest, macro_snap, macro_align_global,         \
      macro_snap_regular, macro_snap_options, macro_snap_tolerance, macro_snap_weight, centroid_weight,                            \
      centroid_distance_attraction, centroid_anchor_macros_only, centroid_attraction_outside, centroid_repulsion_weight,           \
      centroid_adaptive_repulsion, centroid_stop_overlap, centroid_ramp, centroid_align_weight, centroid_align_sigma,              \
      centroid_align_start, centroid_align_full, centroid_parent_weight, centroid_parent_outside, num_threads, seed,               \
      target_density, stop_overflow, min_iterations, post_density_factor, convergence_tolerance, post_density_patience,            \
      initial_density_weight, density_weight_seed, hold_overflow, fillers_in_whitespace, max_step, filler_width, filler_max_count, \
      filler_area_fraction, filler_cell_space, filler_macro_fraction, filler_cell_scale, filler_bin_fraction, cohesion_weight, cohesion_auxiliary, \
      order_weight, order_growth, schedule_iterations, route, route_external, route_external_interval, route_physical_snapshot,    \
      route_initialize, routability, route_capacity_h, route_capacity_v, route_cell_pin_capacity, density_upper_bound, add_fillers, \
      legalize,                                                                                                                    \
      macro_only_legalize, detailed_place, detailed_iterations, before_legalize, after_macro_legalize, observer,                   \
      observer_interval, checkpoint_rollback
  auto& [LIVEHD_GP_INPUTS] = o;
  auto apply               = [&](auto&... inputs) { (gp_wire_detail::input(a, inputs), ...); };
  apply(LIVEHD_GP_INPUTS);
#undef LIVEHD_GP_INPUTS
}
}  // namespace DreamPlace::native
