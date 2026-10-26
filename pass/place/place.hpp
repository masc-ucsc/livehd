// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "boundary_pins.hpp"
#include "busorder.hpp"
#include "cell_jump.hpp"
#include "core/physical_rc.hpp"
#include "core/timing_feedback.hpp"
#include "core/timing_whatif.hpp"
#include "dreamplace/native/floorplan.hpp"
#include "dreamplace/native/macro/topology.hpp"
#include "dreamplace/native/placement.hpp"
#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"
#include "power_grid.hpp"
#include "preroute_design.hpp"
#include "rc_cache.hpp"
#include "satlegalize.hpp"
#include "terminal_legalize.hpp"
#include "uroute.hpp"
#include "warpspace.hpp"

namespace livehd::placement {
namespace timeclosure {
struct Gp_timing_settings;
}
namespace buffering {
struct Checkpoint_settings;
}
inline constexpr double max_net_weight = 100;  // physical net weight ceiling shared by option checks and the placement database
struct Result;
enum class Placement_stage { floorplan, macro, final };
struct Placement_restart : std::exception {
  physical::Floorplan seed;
  Placement_stage     stage;
  Placement_restart(physical::Floorplan frame, Placement_stage s) : seed(std::move(frame)), stage(s) {}
  const char* what() const noexcept override { return "placement restarted after whole-color resynthesis"; }
};
using Timing_feedback = livehd::timing::Feedback;
using Evaluate        = std::function<Timing_feedback(const Result&)>;
struct Stage_route {
  std::string                     model = "inherit";
  DreamPlace::native::RoutePolicy policy;
};
// Read-only diagnostic sample. Both routing measurements use the same pins;
// opaque_* uses full physical macro bodies even during memory growth.
struct Gp_observation {
  int                           iteration   = 0;
  double                        macro_scale = 1, hpwl_um = 0;
  double                        density_overflow = 0;
  double                        route_overflow = 0, opaque_route_overflow = 0;
  double                        route_peak = 0, opaque_route_peak = 0;
  int                           unroutable_nets = 0, opaque_unroutable_nets = 0;
  int                           blocked_pins = 0, opaque_blocked_pins = 0;
  std::optional<Rc_reuse_stats> rc_reuse;
  bool                          rc_reuse_verified = false;
};
struct Preroute_settings {
  bool        enabled         = false;
  double      setup_margin_ps = 20, hold_margin_ps = -1, nominal_slew_ps = 20;
  double      capacity_fraction = .30;
  std::string hold_buffer       = "auto";
  int         budget_ms = 10, stage_budget_ms = 10000, bins = 64;
  int         max_rows = 0, max_sites = 0;  // Zero leaves original-net pin spans unlimited.
};
struct Options {
  // Experimental native fixed-footprint checkpoint binding; absent preserves
  // the existing flow. Requires the native per-pin timing dependency API.
  std::shared_ptr<const timeclosure::Gp_timing_settings> native_gp_timing;
  // Experimental joint native checkpoint, absent by default. Requires native
  // timing and fillers; refreshes source/LR state after all timing gates pass.
  std::shared_ptr<const buffering::Checkpoint_settings>  native_gp_buffering;

  Preroute_settings                                         preroute;
  std::vector<Pg_channel>                                   pg_channels;  // Opt-in patches; reference grid by default.
  std::optional<double>                                     pg_channel_max_ir_drop_v;
  // Internal host handoff, collected only for a completed final placement.
  bool                                                      retain_preroute_design = false;
  // Diagnostic-only, synchronous observation; never supplies optimizer feedback.
  std::function<void(const Result&, const Gp_observation&)> gp_observer;
  int                                                       gp_observer_interval  = 0;
  bool                                                      gp_observer_rc_reuse  = false;
  bool                                                      gp_observer_rc_verify = false;
  // Internal staged-flow freeze: restore user pin constraints before persistence.
  std::map<std::string, physical::Status>                   stage_pin_status;
  std::function<bool(Placement_stage, const Result&)>       after_stage;
  std::shared_ptr<const physical::Floorplan>                restart_seed;
  Placement_stage                                           completed_stage = Placement_stage::final;
  std::vector<std::string>                                  lefs;
  std::string                                               floorplan, output_dir, cache_dir;
  std::string                                               liberty, tech_rc_file, tracks_file;
  std::string                                               macro_mode = "none";
  Warp_options                      fp_warp{.weight = 1}, macro_warp{.weight = .7, .start_stability = .2, .refresh_interval = 25};
  DreamPlace::native::MagnetOptions macro_magnet;
  bool                              fp_place = false, fp_snap_enabled = true;
  bool                              fp_colors           = false;  // eligible color leaves; never independently centroid-snapped
  std::string                       fp_color_source     = "synthesis";
  bool                              analyze_colors_only = false;  // diagnostic: no GP/LG/STA or cache
  double                            fp_color_maximum    = 30000;  // memory-first logic area in equivalent cells
  double                            fp_color_minimum    = -1;     // inherit fp_minimum; physical member area only
  bool                              fp_pin_adjust       = true;
  int                               fp_pin_interval     = 50;
  double                            fp_pin_step = .01, fp_pin_improvement = .001;  // perimeter fraction, relative gain
  bool                              macro_pin_adjust   = true;
  int                               macro_pin_interval = 50;
  double                            macro_pin_step = .01, macro_pin_improvement = .001;
  int                               fp_iterations = 1500, fp_min_copies = 4, fp_min_macros = 2;
  int                               fp_timing_interval      = 50;
  double                            fp_timing_movement      = 0;  // RMS bin movement; 0 keeps fixed cadence
  bool                              fp_timing_after_density = false;
  double                            timing_fraction         = .2;  // worst-slack fraction of timed non-clock nets
  double                            timing_max_weight = 5, timing_smoothing = .5;
  double                            timing_exponent             = 1;  // convex criticality curve; 1 preserves linear weights
  double                            timing_spread_weight        = 0;  // [0,1] protection from extra routing/local spreading
  int                               macro_timing_interval       = 50;
  int                               final_timing_start          = 200;      // GP warm-up before placement STA
  std::string                       final_timing_schedule       = "fixed";  // fixed | late (4x, 2x, 1x cadence)
  // Opt-in fresh timing displacement protection, bounded to 100x.
  double                            legalize_timing_strength    = 0;
  double                            legalize_timing_threshold   = 0;
  unsigned                          legalize_timing_corrections = 0;
  // Internal lifecycle state: optimization trials need legal snapshots before
  // the mandatory final legalization. Never exposed as a user option.
  bool                              optimization_pending        = false;
  bool                                  handoff_taps                = false;   // profile fixed taps before terminal legalization
  std::string                       pg                          = "none";  // opt-in native power-grid profile
  Region_options                        region;  // detail-final region mesh shared by busorder/boundarypins/pinalign
  Busorder_options                  busorder;
  Boundary_pin_options              boundarypins;
  Cell_jump_options                 cell_jump;                            // diagnostic: extend a supplied distance-one seed
  bool                              vanished_memory             = false;  // final-GP centered memory growth and local-pressure ramp
  double                            vanished_memory_local_floor = 0;
  double                            vanished_memory_initial_scale     = .6;
  int                               vanished_memory_growth_iterations = 25;
  int                               vanished_memory_start             = 70;
  DreamPlace::native::GpSearch      gp_search;               // opt-in final-GP A/B trials
  DreamPlace::native::MacroTransparency macro_transparency;  // diagnostic final-GP barrier schedule
  Terminal_legalize_options             terminal_legalize_options;
  int                                   fp_timing_max_updates = 10;
  bool                                  fp_resize             = false;
  // Resolved by the driver: enabled with resize unless explicitly disabled.
  bool                                  gp_resize             = false;
  int                                   gp_buffer_trials      = 4;    // maximum attempts per insertion/removal phase
  double                                gp_buffer_margin      = .01;  // fraction of clock period (path delay without clocks)

  bool                                  repair_reset   = false;  // retired: true is rejected before pre-CTS optimization
  // synth.place.resize.engine: "new" = path-based sizing/buffering on the
  // incremental what-if timer (resize_engine.hpp); "legacy" = batch sweeps.
  std::string                           resize_engine  = "legacy";
  bool                                  resize_vt_swap = false;  // allow same-footprint swaps across Liberty files (multi-VT)
  std::vector<std::string>              resize_dont_use;         // glob patterns never chosen as a new master
  // What-if handle of the timer behind fp_evaluate/evaluate (required by engine=new).
  livehd::timing::Whatif*               whatif       = nullptr;
  // Legacy reset-tree control retained for a clear diagnostic on nonzero values.
  unsigned                              reset_fanout = 0;  // retired: pre-CTS control nets are never buffered
  std::string                           control_ports;     // override optional roots; recognized resets stay excluded
  Evaluate                              fp_evaluate;
  double                                fp_minimum = 3000, fp_attraction = .25, fp_repulsion = 5, fp_anchor = .25;
  bool                                  fp_distance_attraction = true;
  bool                                  fp_anchor_macros_only  = false;  // frozen targets; retain all-member normalization
  double                                fp_attraction_outside  = 4;      // additional stiffness beyond the centroid circle
  double                                fp_align_weight = 0, fp_align_sigma = .25;
  int                                   fp_align_start = 100, fp_align_full = 500;
  double                                fp_parent_weight = 0, fp_parent_outside = 4;
  DreamPlace::native::FloorplanOptions  fp_snap;
  DreamPlace::native::macro::Parameters macro_parameters;
  std::shared_ptr<const DreamPlace::native::macro::Topology> macro_topology;
  // In-process ECO reference; independent of persistent-cache admission.
  std::shared_ptr<const physical::Floorplan>                 previous;
  std::vector<std::string>                                   changed_cells;
  // Opt-in incident-net wirelength cost during nearest-whitespace ECO legalization (0..100).
  double                                                     eco_connectivity_weight = 0;
  // Diagnostic only: exact edited-cell names whose successful ECO seeds/sites are recorded in qor.eco.trace.
  std::vector<std::string>                                   eco_trace_cells;
  std::map<std::string, double>                              net_weights;
  double                                                     utilization = 0.30, aspect = 1, density = 0.5;
  // Fixed generated frame in microns, {xl, yl, xh, yh}; empty means derive it from utilization/aspect.
  // `core` is snapped inward to the site/row grid; with only one of the two given the other equals it.
  // An imported `floorplan` (also what the staged macro/cell passes feed themselves) wins over both.
  std::vector<double>                                        die, core;
  double                                                     stop_overflow = .1, initial_density_weight = .01;
  // -1 resolves to ten placement-row heights; nonnegative values are explicit microns per side.
  double                                                     halo_x = -1, halo_y = -1;
  // Unset preserves the macro halo for cells. Applied only after the macro frame is fixed.
  std::optional<std::pair<double, double>>                   cell_halo;
  bool        cell_stage                = false;  // internal explicit final/placed context; never inferred from an ECO frame
  bool        scale_macro_density       = true;
  bool        dynamic_bins              = false;
  bool        macro_fill_cell_space     = false;  // complement of cell-density target
  double      macro_filler_fraction     = 0;      // macro GP only; physical macro dimensions
  bool        virtual_fillers           = true;
  int         virtual_filler_max_count  = 0;
  double      virtual_filler_fraction   = .25;
  double      virtual_filler_cell_scale = 1, virtual_filler_bin_fraction = .25;
  int         bin_max = 512, local_density_bins = 512;
  int                        route_bins    = 0;  // grid.route: 0 inherits the configured density count (also in dynamic mode)
  int                        legalize_bins = 0;  // legalize.bin.grid: 0 uses the routing grid
  double      local_density_weight = 0, local_density_range = 4, local_density_jitter = .01;
  double      local_density_target      = -1;  // -1 inherits the global density target
  double      macro_cell_density_weight = 0;   // standard-cell pressure on macros during macro GP only
  double      macro_cell_density_reach  = 2;   // screening length / largest physical SRAM side
  double      macro_density_pull_weight = 0, macro_density_pull_range = 4;
  bool        automatic_bins = false, exact_macro_density = true, adaptive_density = true, routability = true;
  std::string route_model = "rsmt";
  Stage_route fp_route, macro_route, final_route;
  Uroute_options uroute;
  // Internal: a pool snapshot preloaded by load_uroute_pool(), reused when its
  // resolved controls match so one run hashes the manifest/weights once.
  std::optional<Uroute_pool> uroute_pool;
  // Internal immutable snapshot supplied to the H2D host adapter.
  Uroute_selection           uroute_selection;
  Uroute_identity            uroute_stage_identity;
  int         route_degree = 40, route_accuracy = 3;
  bool        routability_charge_compensation = true;
  double      routability_macro_weight = 1, routability_macro_range = 2;  // direct channel pressure; range in bins
  double      macro_overlap_weight = 50, macro_jitter = .01;
  double      macro_align_weight = 25, macro_align_sigma = .25, macro_align_overlap_boost = 0;
  int         macro_align_start = 100, macro_align_full = 500;
  bool        macro_align_nearest = false, macro_snap = true;
  bool        macro_align_global = true;         // same master across colors; refresh strip pairs each GP iteration
  std::string macro_snap_mode    = "alignment";  // alignment | symmetry
  DreamPlace::native::FloorplanOptions macro_snap_options;
  double                               macro_snap_tolerance = .25;
  int                                  macro_snap_weight    = 100;
  int                                  schedule_iterations = 500, io_iterations = 500;
  double                               cohesion          = 0;
  double                               resistance_ohm_um = -1, capacitance_ff_um = -1;
  double                               rc_pessimism = 1;  // wire R and C each scale by sqrt(factor)
  int                                  iterations = 1500, bins = 64, threads = 1;
  double                               fixed_macro_overlap_weight = 0;
  double                               wirelength_gamma_end       = .05;  // experimental; existing smoothing schedule by default
  int                                  detailed_iterations        = 2;
  bool                                 macro_detailed_place       = true;   // cell detail while macros are movable
  bool                                 macro_legalize_cells       = true;   // false leaves macro-stage cells at GP poses
  bool                                 final_independent_timing   = false;  // synthesis weights, then density-gated final STA
  bool                                 exact_cell_density         = false;
  bool                                 density_upper_bound        = false;  // experimental filler-free upper-bound penalty
  int                                  min_iterations             = 40;
  int                 post_density_factor                 = 4;  // bounded continuation after geometry/density settle; zero disables
  double              convergence_tolerance               = .01;
  bool                full                                = true;
  bool                final_cells                         = true;
  int                 final_iterations                    = 0;  // zero inherits iterations; only after fp in full mode
  // Diagnostic physical GP coordinates, before macro/cell legalization.
  bool                snapshots                           = false;
  // Internal post-synthesis STA snapshot: mapped pins and zero wire RC, no GP
  // or legalization, and no placement-cache publication.
  bool                timing_seed                         = false;
  // Timing-only import of a complete saved placement. Preserve every location;
  // do not legalize, optimize or certify the imported placement's legality.
  bool                retain_placement                    = false;
  // Try nearest-whitespace placement of edited/new cells before windowed GP.
  bool                legalize_cells                      = false;
  bool                satlegalize                         = false;
  bool                satlegalize_route                   = false;
  bool                satlegalize_pg_after                = false;
  bool                satlegalize_route_vias              = false;
  double              satlegalize_route_critical_fraction = .10;
  Satlegalize_options satlegalize_options;
  // Opt-in: an unknown (X) constant bit that reaches a physical net is a synthesis
  // don't-care (`assign y = 'x`), so any value is a legal refinement; tie it low
  // instead of refusing the placement. Off keeps the refusal.
  bool                unknown_const_zero     = false;
  // Geometry-only replay: retain characterized imported shared tie masters/nets.
  // Logical constants still receive real per-load physical sources.
  bool                preserve_imported_ties = false;
};
DreamPlace::native::RoutePolicy resolve_route_policy(const Options&, const Stage_route&);
void                            initialize_route_tables();
// The learned-routing pool snapshot requested by any stage, or nullptr. Store it
// in Options::uroute_pool to reuse one snapshot for replay identity and placement.
std::optional<Uroute_pool>      load_uroute_pool(const Options&);
// Content identity of requested learned routing inputs for whole-flow replay.
std::string                     uroute_cache_identity(const Options&);
struct Macro_gp_timing {
  Timing_feedback                                  timing;
  std::shared_ptr<physical::Parasitics>            parasitics;
  std::map<std::string, std::pair<double, double>> pin_locations;
};
struct Result {
  std::shared_ptr<const Preroute_design>           preroute_design;
  unsigned                                         fp_resized_cells = 0, fp_repair_edits = 0;
  // Unknown constant bits tied low under Options::unknown_const_zero (zero otherwise).
  size_t                                           unknown_const_tied = 0;
  // Own the last FP topology trial; declared before graph so it outlives it.
  std::map<std::string, std::string>               fp_buffer_origins;
  std::shared_ptr<hhds::GraphLibrary>              fp_library;
  std::map<std::string, std::pair<double, double>> pin_locations;
  std::map<std::string, double>                    net_weights;
  std::shared_ptr<hhds::Graph>                     graph;
  std::shared_ptr<physical::Parasitics>            parasitics;
  std::string                                      qor;
  physical::Floorplan                              floorplan;
  Prects_library                                   prects;
  // Selected GP measurement, before macro snapping. Resynthesis can harvest
  // this without analyzing provisional cells after macro-only legalization.
  std::optional<Macro_gp_timing>                   macro_gp_timing;
};
using Point = std::pair<double, double>;
struct Steiner_tree {
  std::vector<Point>                         points;
  std::vector<std::pair<unsigned, unsigned>> edges;
  std::vector<unsigned>                      terminals;
};
Steiner_tree steiner_tree(const std::vector<Point>& points);
// flat_library must outlive the returned graph. Source graphs are read-only.
Result       place(hhds::Graph& source, hhds::GraphLibrary& flat_library, const Options& options);
// Mark a final/placed context without losing the original macro spacing request.
Options      final_cell_options(const Options& options);
// Regenerate native memory geometry as well as external LEFs for placed-API checks.
void         check_placed_legality(const Result& result, const Options& options);
// Independent physical check against the LEF/DEF model, after DBU rounding.
void         check_legality(const physical::Technology& tech, const physical::Floorplan& fp, double halo_x = 0, double halo_y = 0);
void         check_legality(const physical::Technology& tech, const physical::Floorplan& fp, const Options& options);
}  // namespace livehd::placement
