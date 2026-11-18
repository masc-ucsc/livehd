// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <optional>

#include "pass/synth/cone_map.hpp"
#include "place.hpp"

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
  // With options.virtual_buffers: every bypassed tree as one virtual node, by root driver label (see Options).
  std::map<std::string, Options::Virtual_buffer> virtual_trees;
};
Buffer_cleanup                remove_synthesis_buffers(hhds::Graph& source, hhds::GraphLibrary& library, const Options& options);
Timing_feedback               timing_feedback(std::string_view json);
std::map<std::string, double> timing_weights(const physical::Parasitics& nets, const Timing_feedback& timing, double min_weight = .5,
                                             double max_weight = 2);
// Every timed non-clock net is weighted by its slack rank (worst max_weight, median 1, best min_weight); nets without
// timing return to the baseline weight 1, and the weights are smoothed against the previous ones.
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
}  // namespace livehd::placement
