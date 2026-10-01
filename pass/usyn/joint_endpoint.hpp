// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "decomposition.hpp"
#include "endpoint.hpp"
#include "endpoint_pairs.hpp"

namespace livehd::usyn {
struct Joint_endpoint_result {
  Status                                          status = Status::invalid;
  std::optional<std::array<Endpoint_solution, 2>> endpoints;
  uint32_t                                        shared_divisors = 0;
  bool                                            exhausted       = false;
};

// Synthesize a common first-phase code for the two exact functions on one
// ordered basis. Bound variables must belong to both endpoint windows; other
// inputs can be private to either root. Each endpoint keeps its own analysis
// cap and is independently validated by composition on its original cut.
// Unreachable code words initially use zero completion. This is one bounded
// encoding proposal, not optimal encoding or care-completion enumeration.
// Both endpoints must use at least one common new producer. No graph mutation
// or local-cost acceptance occurs: the caller prices the complete joint result.
// The caller also establishes eligible endpoints in the same known clock domain.
Joint_endpoint_result synthesize_joint_endpoints(const Xag& graph, const Pair_windows& windows,
                                                 const std::array<Truth_table, 2>& functions,
                                                 const std::array<std::string, 2>& names, uint32_t bound_mask,
                                                 const Endpoint_options& options, Budget& work);

struct Joint_endpoint_choices {
  Status                                        status = Status::invalid;
  std::array<std::vector<Endpoint_solution>, 2> endpoints;
  uint32_t                                      code_bits = 0;
  // bytes is the peak admitted payload estimate, not a process RSS measurement.
  uint64_t                                      phases = 0, attempts = 0, retained = 0, bytes = 0;
  bool                                          exhausted = false;
};

// Bounded per-root pools for the same joint encoding. Zero completion is kept
// when legal, plus at most choice_limit (1..8) other total completions per root.
// Every retained endpoint is independently validated; a complete pool entry
// survives later work/storage refusal. Unused producers are removed, even if
// their implementation was unavailable. Some entries can have no phase-1 cell.
// The caller prices cross-combinations and each entry with the original partner;
// individual top cost cannot decide which combination best preserves sharing.
Joint_endpoint_choices complete_joint_endpoints(const Xag& graph, const Pair_windows& windows,
                                                const std::array<Truth_table, 2>& functions,
                                                const std::array<std::string, 2>& names, uint32_t bound_mask,
                                                const Endpoint_options& options, uint32_t choice_limit, Budget& work);

// Try one alternate invertible code, including zero-filled tops for full code
// images and optional care completions. No preceding trajectory is displaced:
// the caller compares candidates using the complete regional sharing ledger.
// code_bits also reports the original width when a requested bit is absent.
Joint_endpoint_choices recode_joint_endpoints(const Xag& graph, const Pair_windows& windows,
                                              const std::array<Truth_table, 2>& functions, const std::array<std::string, 2>& names,
                                              uint32_t bound_mask, Joint_code_change change, const Endpoint_options& options,
                                              uint32_t choice_limit, Budget& work);
}  // namespace livehd::usyn
