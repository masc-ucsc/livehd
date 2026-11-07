// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "sat_solver.hpp"

namespace livehd::placement::lr {
// Opaque finite choices: Local owns the request/track/geometry mapping. An
// incompatible pair is {request_a, candidate_a, request_b, candidate_b}.
// A self-pair forbids that candidate. Repeated pairs are allowed.
struct Track_repair_problem {
  std::vector<size_t>                candidate_counts;
  std::vector<std::array<size_t, 4>> incompatible;
  // Empty, or one optional nonbinding preferred candidate per request.
  std::vector<std::optional<size_t>> preferred;
};

struct Track_repair_limits {
  // Shared absolute budget, including validation, CNF construction and decoding.
  // Default expired: the caller must explicitly opt in with a live deadline.
  std::chrono::steady_clock::time_point deadline{};
  size_t                                max_requests = 32, max_candidates_per_request = 64;
  size_t                                max_candidates = 2048, max_clauses = 200000;
  unsigned                              conflicts = 100000;
};

struct Track_repair_result {
  Sat_status          status = Sat_status::unknown;
  std::string         reason;
  // Present only for SAT: exactly one candidate index for every request.
  std::vector<size_t> selected;
  size_t              candidate_literals = 0, variables = 0, clauses = 0, solve_calls = 0;
  double              build_seconds = 0, solve_seconds = 0, decode_seconds = 0, total_seconds = 0;
};

// No caller state is changed. SAT certifies only this supplied finite allocation;
// complete affected-owner geometry/resource checks are required before commit.
// UNSAT is conditional on the finite candidates and constraints, not physical
// routing infeasibility. Deadline/construction/conflict limits return UNKNOWN
// with no selection. Inspected malformed indices throw std::invalid_argument.
Track_repair_result solve_track_repair(const Track_repair_problem&, const Track_repair_limits&);
}  // namespace livehd::placement::lr
