// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <optional>

#include "pass/synth/cone_map.hpp"
#include "place.hpp"

namespace livehd::timing::fast { class Physical_ranker; }
namespace livehd::placement {
// Shared admission for optional optimization trials. Check before cloning,
// legalization or STA; baseline and final validation remain mandatory. Callers
// may reserve final evaluations against their invocation-wide timing count.
using Trial_admission = std::function<bool()>;
// Make a physical copy without synthesis-only identity buffers before a fresh
// solve. Preserve port bindings, fixed cells and every clock/reset cone,
// including cones crossing hierarchy boundaries. Logical definitions and their
// resynthesis identities/history remain unchanged.
// Never apply to an already placed/ECO graph: its physical trees are intentional.
struct Buffer_cleanup {
  std::shared_ptr<hhds::Graph> graph;
  unsigned                     removed = 0, boundary = 0, protected_cells = 0, unsupported = 0, inverter_pairs = 0;
  unsigned                     clock_gates = 0;  // cascaded clock-gating ANDs merged (Options::flatten_clock_gates)
  // With options.virtual_buffers: every bypassed tree as one virtual node, by root driver label (see Options).
  std::map<std::string, Options::Virtual_buffer> virtual_trees;
};
Buffer_cleanup                remove_synthesis_buffers(hhds::Graph& source, hhds::GraphLibrary& library, const Options& options);
Timing_feedback               timing_feedback(std::string_view json);
// A weight per timed non-clock net from its slack relative to the clock period of its own domain (x = slack /
// period): 1 for x >= margin, rising to the midpoint between 1 and max_weight at zero slack and to max_weight on
// the worst failing net (`exponent` shapes the rise). margin 0, or timing that reports no clock period: the
// legacy slack-RANK weights (worst max_weight, median 1, best min_weight).
std::map<std::string, double> timing_weights(const physical::Parasitics& nets, const Timing_feedback& timing, double min_weight = .5,
                                             double max_weight = 2, double margin = default_timing_margin, double exponent = 1);
// The weights above for every timed non-clock net; nets without timing return to the baseline weight 1, and the
// weights are smoothed against the previous ones.
void                          refresh_timing_weights(Result& state, const Options& options, const Timing_feedback& timing);
// Reuse the unmodified Liberty/control-port roles parsed for this placement
// invocation. Graph-specific control seeds still come from state.prects.
void                          refresh_timing_weights(Result& state, const Options& options, const Timing_feedback& timing,
                                                     const Prects_library& library_roles);
// All edges of one newly inserted tree share half the source's excess pull.
void                          inherit_buffer_weights(std::map<std::string, double>& weights, const std::string& driver,
                                                     const std::vector<std::string>& outputs);
struct Repair_result {
  // Own independent accepted graphs; libraries outlive the placed graph.
  std::vector<std::unique_ptr<hhds::GraphLibrary>> libraries;
  Result                                           placed;
  std::string                                      report;
  std::optional<Timing_feedback>                   timing;
  unsigned                                         moved_cells = 0;  // refine_regions: cells whose pose changed
  // timing_place: a round ran out of its GP continuation budget before the solve converged, so nothing was accepted. A
  // later round from nearly the same placement and weights runs the same budget and ends the same way.
  bool                                             gp_unconverged = false;
  // resynthesize_colors: the occurrences it tried and the ones whose new body it kept.
  std::vector<std::string>                         colors_tried, colors;
};
// Expand every recorded virtual buffer tree (options.virtual_buffer_trees) whose root net still has more loads than
// options.virtual_buffer_fanout into a position-aware, fanout- and capacitance-bounded tree at the current
// placement, legalize the new cells and re-time. Mandatory, not a trial: the mapper asked for this buffering, so a
// timer that misjudges a wide net (no slew limit seen) cannot leave it unbuffered. Later repair may resize or
// rebuild these trees on timing like any other.
struct Virtual_expansion {
  unsigned roots = 0, expanded = 0, fixed = 0, buffers = 0, skipped = 0, failed = 0;  // fixed: the timer-independent tree
  unsigned placed_buffers = 0, moved_cells = 0;    // legalized new buffers; existing cells legalization moved
  double   buffer_mean_um = 0, buffer_max_um = 0;  // new buffer: planned position to legal site
  double   moved_mean_um = 0, moved_max_um = 0;    // existing cells that moved, by how much
};
Repair_result expand_virtual_buffers(Result initial, const Options& options, const Evaluate& evaluate,
                                     Virtual_expansion* stats = nullptr);
Repair_result wirelength_resynthesize(Result initial, const Options& options, const Evaluate& evaluate, const Evaluate& trial_evaluate, Virtual_expansion& stats);
// Build the native clock network on the private graph and seed its cells for terminal legalization.
struct Clock_handoff {
  std::set<std::string> buffers;
  Clock_driver_pins drivers;
};
Clock_handoff build_clock_handoff(Result&, const Options&, const std::vector<std::string>&, std::string_view,
                                  std::unique_ptr<timing::fast::Physical_ranker>, const std::string& previous_clock_report = {});
void record_final_clock_leaf_geometry(const Result&, const std::string& output_dir);
std::set<std::string> build_clock_tree(Result& placed, const Options& options, const std::vector<std::string>& macro_libraries, std::string_view sdc_text);
std::set<std::string> build_clock_tree(Result&, const Options&, const std::vector<std::string>&, std::string_view,
                                       std::unique_ptr<timing::fast::Physical_ranker>);
struct Floorplan_resize {
  Timing_feedback timing;
  unsigned        proposed = 0, accepted = 0, repair_proposed = 0, repair_accepted = 0;
  std::string     report;
};
// Load-ranked size-only sweep at overlapping GP coordinates. refresh rebuilds physical
// geometry/RC; no legalization, topology edits or macro movement are permitted.
Floorplan_resize resize_floorplan(Result& state, const Options& options, const Timing_feedback& before, const Evaluate& evaluate,
                                  const std::function<void(Result&)>& refresh,
                                  const std::map<std::string, double>* width_limits = nullptr);
// Size plus whole-forest repair (with bounded fallback trials) at current GP coordinates.
// refresh must rebuild the physical view after topology changes without legalization.
Floorplan_resize repair_floorplan(Result& state, const Options& options, const Timing_feedback& before, const Evaluate& evaluate,
                                  const std::function<void(Result&)>& refresh);
// Design-wide sizing/forest sweeps with transactional bisection and a bounded
// fallback search. Every accepted batch has fresh legal placement, RC and STA.
bool             timing_improves(const Timing_feedback& before, const Timing_feedback& after);
// prior must describe exactly initial under the same constraints/model.
Repair_result    timing_place(Result initial, const Options& options, const Evaluate& evaluate, unsigned rounds,
                              std::optional<Timing_feedback> prior = std::nullopt, const Trial_admission& admit_trial = {});
// Complete the accepted optimization result with zero-GP terminal legalization,
// fresh RC/STA, and the exact repaired netlist. Does not insert or resize cells.
// Reuse prior only if final graph bindings, cell/IO poses, RC and cuts are
// identical. The caller must use unchanged timing constraints/model.
Repair_result    finalize_placement(const Result& initial, const Options& options, const Evaluate& evaluate,
                                    std::optional<Timing_feedback> prior = std::nullopt);
// Bus ordering / boundary pins / pin alignment (whichever the options enable) on a placed result: extra GP
// iterations from the current placement, then native legalization (the terminal legalization stays in
// finalize_placement). Runs once, on the timing-driven result; the caller gives timing a chance to react
// afterwards (timing_place again) when `moved_cells` is nonzero, then resizes and repairs.
Repair_result    refine_regions(Result initial, const Options& options, const Evaluate& evaluate);
// The experimental SAT legalization (satlegalize, fixed bins) on a placed result: run after resize and repair,
// immediately before final legalization; nothing resizes afterwards and timing-driven placement is not re-run.
Repair_result    sat_align_placed(Result initial, const Options& options, const Evaluate& evaluate);
// design_after_sizing consumes an unchanged sizing result: gate sizing only
// reopens if a forest edit is accepted. forest skips sizing/area recovery.
enum class Repair_scope { all, sizing, design, design_after_sizing, forest, recovery };
// prior must describe exactly initial under the same timing constraints/model.
Repair_result repair_buffers(Result initial, const Options& options, const Evaluate& evaluate,
                             Repair_scope scope = Repair_scope::all, std::optional<Timing_feedback> prior = std::nullopt,
                             const Trial_admission& admit_trial = {});
// Up to four 32-cell/12-input combinational cuts. Exact exhaustive equivalence
// precedes splicing, and each proved candidate gets local placement + fresh STA.
Repair_result resynthesize_cones(Result initial, const Options& options, const Evaluate& evaluate,
                                 const synth::Cone_provider& provider, std::optional<Timing_feedback> prior = std::nullopt,
                                 const Trial_admission& admit_trial = {});
// Whole-color resynthesis inside the final phase. One call tries up to `max_colors` color occurrences (placement
// blocks). A timing call takes the failing ones, worst slack first; an area call takes the ones with the most cone
// area first and keeps a smaller body only without timing loss (area_any: whatever the timing).
struct Color_request {
  enum class Goal { timing, area, area_any };
  Goal                  goal       = Goal::timing;
  double                trigger    = 0.05;   // of the clock period: how far past its met target a color must be to be called again
  unsigned              max_cells  = 20000;  // replaceable cells of one occurrence; 0: no limit
  unsigned              max_colors = 4;
  unsigned              serial     = 0;  // offset of the trial numbers, so that two calls do not share names
  std::string           prover     = "abc";
  // Occurrences this goal already took in this run.
  std::set<std::string> done;
  // What is known about the body a synthesis region id was mapped with: 0 no timing, 1 met its target, 2 missed it.
  std::function<int(uint32_t)> history;
};
Repair_result resynthesize_colors(Result initial, const Options& options, const Evaluate& evaluate,
                                  const std::vector<std::pair<std::string, synth::Cone_provider>>& providers,
                                  const Color_request& request, std::optional<Timing_feedback> prior = std::nullopt,
                                  const Trial_admission& admit_trial = {});
}  // namespace livehd::placement
