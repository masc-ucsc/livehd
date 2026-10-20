// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// synth.place.resize.engine=new: path-based gate sizing and buffering on the
// persistent incremental timer (core/timing_whatif.hpp). Every candidate move is
// applied to the timer as a transient what-if edit (wire cuts get fresh Steiner
// RC for both pieces), evaluated incrementally and rolled back unless it helps.
// Accepted moves form a plan that is materialized once, legalized (or refreshed
// at floorplan GP coordinates) and confirmed by a full evaluation.
#include <functional>
#include <string>
#include <vector>

#include "repair.hpp"

namespace livehd::placement {
struct Resize_edit {
  enum class Kind { resize, buffer };
  Kind                     kind = Kind::resize;
  std::string              instance, master;  // resize: instance -> master; buffer: new instance of master
  std::string              driver;            // buffer: driver label of the net it cuts
  std::vector<std::string> sinks;             // buffer: sink labels moved behind the buffer
  Point                    position{};        // buffer: seeded center, DBU
  std::string              owner;             // buffer: instance whose color/region/block it inherits
  unsigned                 group = 0;         // edits of one accepted move; plans are committed by group prefix
};
struct Resize_plan {
  std::vector<Resize_edit> edits;
  unsigned                 groups = 0, buffers = 0, upsized = 0, downsized = 0;
  double                   wns_before = 0, tns_before = 0, wns_after = 0, tns_after = 0;  // what-if ps
  std::string              report;                                                        // JSON object
};
enum class Resize_stage { floorplan, placed };
// Plan on state, which must be the state the what-if timer evaluated last.
// The timer is left without pending edits.
Resize_plan plan_resize(const Result& state, const Options& options, const Timing_feedback& timing, livehd::timing::Whatif& whatif,
                        Resize_stage stage);
// Materialize the edits (in order) in graph; origins maps each new buffer to its owner.
void        apply_resize_edits(hhds::Graph& graph, const Options& options, const std::vector<Resize_edit>& edits,
                               std::map<std::string, std::string>* origins = nullptr);
// Full-evaluation acceptance: setup WNS/TNS and electrical violations never worse
// (hold is reported, not gated, as pre-CTS), and some improvement (timing,
// electrical or area).
bool        resize_accepts(const Timing_feedback& before, const Timing_feedback& after);
// Post-legal-placement engine: plan, materialize, ECO-legalize, confirm; repeat
// while it improves. Writes resize.json in options.output_dir.
Repair_result    resize_placed(Result initial, const Options& options, const Evaluate& evaluate);
// Floorplan checkpoint engine at GP coordinates (no legalization); refresh
// rebuilds geometry/RC after topology or master changes.
Floorplan_resize resize_floorplan_engine(Result& state, const Options& options, const Timing_feedback& before,
                                         const Evaluate& evaluate, const std::function<void(Result&)>& refresh);
}  // namespace livehd::placement
