// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <optional>

#include "pass/synth/cone_map.hpp"
#include "place.hpp"

namespace livehd::placement {
struct Timing_feedback {
  struct Pin {
    std::optional<double> arrival, required, slew, load_ff, slack, capacitance_ff;
    bool                  clock = false;
  };
  double                     time_unit_ps = 1000;
  std::map<std::string, Pin> pins;
  double                     delay = 0, area = 0, electrical_excess = 0;
  unsigned                   violations = 0;
  std::optional<double>      wns, tns, hold_wns;
  std::vector<std::string>   path;
};
Timing_feedback               timing_feedback(std::string_view json);
std::map<std::string, double> timing_weights(const physical::Parasitics& nets, const Timing_feedback& timing);
struct Repair_result {
  Result                                           placed;
  // Accepted graphs live in independent libraries; rejected trials are discarded.
  std::vector<std::unique_ptr<hhds::GraphLibrary>> libraries;
  std::string                                      report;
};
using Evaluate = std::function<Timing_feedback(const Result&)>;
// Bounded gate sizing, buffer bypass and Steiner tree rebuilding. Every
// candidate gets fresh legal placement, parasitics and timing before acceptance.
bool          timing_improves(const Timing_feedback& before, const Timing_feedback& after);
Repair_result timing_place(Result initial, const Options& options, const Evaluate& evaluate, unsigned rounds);
enum class Repair_scope { all, sizing, design };
Repair_result repair_buffers(Result initial, const Options& options, const Evaluate& evaluate,
                             Repair_scope scope = Repair_scope::all);
// Up to four 32-cell/12-input combinational cuts. Exact exhaustive equivalence
// precedes splicing, and each proved candidate gets local placement + fresh STA.
Repair_result resynthesize_cones(Result initial, const Options& options, const Evaluate& evaluate,
                                 const synth::Cone_provider& provider);
}  // namespace livehd::placement
