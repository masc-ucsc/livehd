// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/timing_feedback.hpp"
#include "domino_timing_model.hpp"
#include "hhds/graph.hpp"
#include "pass/place/sdc_subset.hpp"

// Half-cycle stage timing and the stage report (2d-domplace E): the native
// late proxy with the provisional domino cells, per launch/capture class
// slack, the separate internal and precharge checks, coverage, clock
// accounting and electrical counts. Setup only: early (hold) checks are
// reported unknown.
namespace livehd::domplace::timing {
// One ideal clock on a design input, for designs whose SDC is outside the
// strict saved-SDC subset.
struct Clock_spec {
  std::string                          name, port;  // an empty name is the port's
  double                               period_ps = 0;
  std::optional<std::array<double, 2>> waveform_ps;  // rise, fall
};
struct Constraint_source {
  late::Constraints                                constraints;
  std::string                                      kind;      // "saved-sdc" | "clock-scan"
  std::vector<std::pair<std::string, std::string>> excluded;  // (port, reason), cut from the measured subset
  std::vector<std::string>                         notes;
};
// The saved SDC when it parses (read_saved_sdc), else the one clock spec. The
// stage schedule is a fixed T/2, so a non-50% waveform throws. Every port that
// is neither a clock root nor constrained is cut with reason
// "io-unconstrained"; the subset is then complete. clock_uncertainty_ps is
// the full-cycle uncertainty of every clock (the SDC subset has none).
// Neither an SDC nor a clock spec, or an invalid spec, throws.
Constraint_source stage_constraints(const hhds::Graph& graph, const std::optional<std::string>& sdc_text, std::string_view design,
                                    late::Sdc_units units, const std::optional<Clock_spec>& clock, double clock_uncertainty_ps);

struct Stage_timing_options {
  bool                   classes = true, require_complete = false;
  uint64_t               model_generation = 0;
  std::optional<int64_t> dbu;  // LEF units per micron of the RC cell sizes; the bundle's when absent
};
struct Stage_timing {
  std::string              status;     // complete | partial (IO cut) | incomplete
  std::optional<double>    period_ps;  // the larger of the graph, internal and precharge periods; absent when incomplete
  std::string              limiting;   // "<class> <endpoint or instance>"
  size_t                   excluded = 0;
  std::vector<std::string> unmeasured;  // null coefficients a present domino instance needs
  std::vector<std::string> reasons;     // why the status is not complete
  std::optional<double>    c_clk_ff;    // clock-pin capacitance over every clock; absent if one is missing
  // For sizing: delay = period_ps, area on the LEF footprint basis (Liberty
  // when the DBU is unknown), native electrical violation count. Empty
  // unless period_ps is present.
  livehd::timing::Feedback feedback;
  std::string              json;  // stage_timing.json, schema 1
};
// One full evaluation (period, coverage, endpoints, stages) and, with
// options.classes, three launch-filtered ones (flop, domino and input
// launches) for exact per-class slack. The constraint source must be the one
// the provisional cells and view were made for. options.require_complete
// throws (require_complete below) unless the status is complete.
Stage_timing evaluate_stage_timing(const hhds::Graph& graph, const physical::Parasitics* parasitics,
                                   std::shared_ptr<const liberty::timing::Library> library,
                                   std::shared_ptr<const late::Provisional_cells> provisional, const Domino_timing_view* view,
                                   Constraint_source source, uint64_t topology_generation, const Stage_timing_options& options);
// Throws std::runtime_error naming the status and its reasons unless complete.
void         require_complete(const Stage_timing& timing);
}  // namespace livehd::domplace::timing
