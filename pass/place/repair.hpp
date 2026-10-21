// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <optional>

#include "pass/synth/cone_map.hpp"
#include "place.hpp"

namespace livehd::placement {
Timing_feedback               timing_feedback(std::string_view json);
std::map<std::string, double> timing_weights(const physical::Parasitics& nets, const Timing_feedback& timing, double fraction = .2,
                                             double max_weight = 5, double exponent = 1);
// Keep only current driver identities; nets outside the current timed critical
// set return immediately to baseline, while current priorities may be smoothed.
void                          refresh_timing_weights(Result& state, const Options& options, const Timing_feedback& timing);
// All edges of one newly inserted tree share half the source's excess pull.
void                          inherit_buffer_weights(std::map<std::string, double>& weights, const std::string& driver,
                                                     const std::vector<std::string>& outputs);
struct Repair_result {
  // Own independent accepted graphs; libraries outlive the placed graph.
  std::vector<std::unique_ptr<hhds::GraphLibrary>> libraries;
  Result                                           placed;
  std::string                                      report;
  std::optional<Timing_feedback>                   timing;
};
struct Floorplan_resize {
  Timing_feedback timing;
  unsigned        proposed = 0, accepted = 0, repair_proposed = 0, repair_accepted = 0;
  std::string     report;
};
// Load-ranked size-only sweep at overlapping GP coordinates. refresh rebuilds physical
// geometry/RC; no legalization, topology edits or macro movement are permitted.
Floorplan_resize resize_floorplan(Result& state, const Options& options, const Timing_feedback& before, const Evaluate& evaluate,
                                  const std::function<void(Result&)>& refresh);
// Size plus whole-forest repair (with bounded fallback trials) at current GP coordinates.
// refresh must rebuild the physical view after topology changes without legalization.
Floorplan_resize repair_floorplan(Result& state, const Options& options, const Timing_feedback& before, const Evaluate& evaluate,
                                  const std::function<void(Result&)>& refresh);
// Design-wide sizing/forest sweeps with transactional bisection and a bounded
// fallback search. Every accepted batch has fresh legal placement, RC and STA.
bool             timing_improves(const Timing_feedback& before, const Timing_feedback& after);
Repair_result    timing_place(Result initial, const Options& options, const Evaluate& evaluate, unsigned rounds);
enum class Repair_scope { all, sizing, design, forest, recovery };  // forest skips sizing/area recovery
// prior must describe exactly initial under the same timing constraints/model.
Repair_result repair_buffers(Result initial, const Options& options, const Evaluate& evaluate,
                             Repair_scope scope = Repair_scope::all, std::optional<Timing_feedback> prior = std::nullopt);
// Up to four 32-cell/12-input combinational cuts. Exact exhaustive equivalence
// precedes splicing, and each proved candidate gets local placement + fresh STA.
Repair_result resynthesize_cones(Result initial, const Options& options, const Evaluate& evaluate,
                                 const synth::Cone_provider& provider);
}  // namespace livehd::placement
