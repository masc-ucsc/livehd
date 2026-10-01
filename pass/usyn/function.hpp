// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "unate.hpp"

namespace livehd::usyn {

// Masks refer to logical variables, not independent rails. A binate variable
// can still be implemented by using both Q and !Q in a single DOMINO gate.
struct Function_analysis {
  Status   status   = Status::invalid;
  uint32_t support  = 0;
  uint32_t negative = 0;
  uint32_t binate   = 0;
};
bool              valid_truth_table(const Truth_table& table);
Function_analysis analyze_function(const Truth_table& table, Budget& work);

// A concrete series/parallel network. Nodes are in postorder; children precede
// their parent. Output polarity names which of the cell's free Q/!Q ports is
// the logical function. Constants model degenerate endpoints, not transistors.
struct Gate_formula {
  enum class Kind : uint8_t { constant, literal, series, parallel };
  struct Node {
    Kind     kind = Kind::constant;
    uint32_t left = 0, right = 0, variable = 0;
    bool     inverted = false;  // literal polarity or constant value
  };
  std::vector<Node> nodes;
  bool              output_inverted = false;

  struct Metrics {
    uint32_t transistors = 0, stack = 0, branches = 0;
    uint32_t support = 0, positive = 0, negative = 0;
    bool     operator==(const Metrics&) const = default;
  };
  // Reject malformed/cyclic/non-tree representations instead of trusting
  // serialized metrics. An empty formula is invalid.
  std::optional<Metrics>     metrics() const;
  bool                       evaluate(uint32_t assignment) const;
  // Checked packed simulation: one AND/OR per formula node per 64 assignments.
  std::optional<Truth_table> evaluate_table(uint32_t inputs, Budget& work) const;
};

struct Gate_constraints {
  uint32_t logical_inputs = 8;
  uint32_t stack          = 4;
  uint32_t branches       = 10;
};

// Resource admission, deliberately separate from technology legality. There
// is no maximum-transistor constraint. The caller supplies work/time/process
// admission through Budget, and bounds live covers/formulas here.
struct Function_search_limits {
  uint32_t max_cubes         = 4096;
  uint32_t max_formula_nodes = 16384;
  uint32_t factoring_choices = 8;
};

struct Gate_candidate {
  Status                      status = Status::search_exhausted;
  std::optional<Gate_formula> formula;
  Gate_formula::Metrics       cost;
  bool                        search_exhausted = false;  // a valid incumbent may survive exhaustion
  std::string                 reason;
};

// Exact minimal-true-point cover when unate. Signed covers and SP factoring
// are bounded heuristics; inability to find a formula is not an infeasibility
// certificate. Every returned formula is checked on logical assignments.
Gate_candidate synthesize_gate(const Truth_table& table, const Gate_constraints& constraints, Budget& work,
                               const Function_search_limits& limits = {});

// Establish F = H(divisors) on the image of a common independent-input window.
// Conflicting image values reject dependence; unreachable values remain don't
// cares. This is Boolean construction, never a solver/LEC invocation.
struct Divisor_function {
  Status                                       status = Status::invalid;
  Truth_table                                  function;
  Truth_table                                  care;
  // Optional assignments with equal divisor values and opposite root values.
  std::optional<std::pair<uint32_t, uint32_t>> conflict;
};
Divisor_function derive_divisor_function(const Truth_table& root, std::span<const Truth_table> divisors, Budget& work,
                                         bool conflict_witness = false);

struct Function_completion {
  Status      status = Status::invalid;
  Truth_table table;
};
// Exact existence and least monotone completion for a specified input-phase
// mask. Unreachable image points are unconstrained. Phase selection belongs to
// the caller's bounded search; this does not enumerate 2^n phase assignments.
Function_completion monotone_completion(const Truth_table& function, const Truth_table& care, uint32_t negative_mask, Budget& work);

inline constexpr uint32_t max_completion_phases = 64;
struct Function_completions {
  Status                   status = Status::invalid;
  std::vector<Truth_table> tables;
  uint32_t                 phases           = 0;
  bool                     search_exhausted = false;
};
// Bounded alternatives: zero/one fill plus least and greatest signed-monotone
// completions. Every table agrees on care points. Unknown points are not new
// independent inputs; the caller must establish the divisor image separately.
// Partial results survive work exhaustion. This does not optimize across all
// possible completions, and phase_limit counts input-polarity assignments.
Function_completions function_completions(const Truth_table& function, const Truth_table& care, uint32_t phase_limit, Budget& work);

}  // namespace livehd::usyn
