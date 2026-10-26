// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "late_timing.hpp"

namespace livehd::placement::late {
struct Timing_difference {
  std::string pin;
  unsigned    transition = 0;
  double      prediction = 0, reference = 0, error = 0, tolerance = 0;
};
struct Timing_error {
  size_t                           compared = 0, coverage_disagreements = 0;
  // Arrival/required: max(5ps,5% reference); slew: max(1e-8ps,10%).
  // No load-error tolerance has been declared, so its count remains absent.
  std::optional<size_t>            tolerance_failures;
  double                           maximum = 0;
  std::optional<double>            mean;
  std::optional<Timing_difference> worst;
  std::vector<Timing_difference>   failure_examples;  // largest error/tolerance, at most eight
};
struct Timing_ranking {
  size_t                compared_endpoints = 0, coverage_disagreements = 0;
  size_t                reference_critical = 0, prediction_critical = 0, union_size = 0;
  std::optional<double> period_ps, jaccard, tau_b;
  std::string           unavailable;
};
struct Timing_comparison {
  // Arrival, required, slew, then driver load, all common pin transitions.
  std::array<Timing_error, 4> fields;
  Timing_ranking              ranking;
  bool                        numerical_gates_pass = false, ranking_gates_pass = false;
  bool                        comparison_gates_pass = false, supported_coverage_pass = false;
};
// Compare the same graph and constraints, by physical pin identity. Critical
// sets use endpoints within 10% of each observation's (period-worst slack).
// Ranking requires one common active period; no missing/tied tau becomes zero
// or a passing value. Model coverage and numerical agreement are separate.
// Passing these comparisons alone does not authorize optimization or establish
// reference-independent/routed fidelity. Neither result is modified.
Timing_comparison compare_timing(const Result& prediction, const Result& reference);
}  // namespace livehd::placement::late
