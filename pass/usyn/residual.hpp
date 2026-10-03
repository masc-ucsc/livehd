// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "xag.hpp"

namespace livehd::usyn {

struct Residual_output {
  Xsignal signal;
  bool    endpoint_input = false;  // prioritize this output's cone
};

struct Residual_options {
  uint32_t and_cost     = 2;
  uint32_t xor_cost     = 4;
  uint32_t depth_slack  = 0;
  uint32_t max_nodes    = 2000000;
  uint32_t windows      = 100000;
  uint32_t window_nodes = 256;
  uint32_t resub_inputs = 8;
  uint32_t divisors     = 32;
  uint32_t divisor_scan = 128;
  uint32_t inserted     = 2;
  uint64_t window_work  = 100000;
  uint64_t stage_work   = 20000000;
  bool     rewrite      = true;
  bool     resubstitute = true;
};

bool valid_residual_options(const Residual_options& options);

struct Residual_report {
  bool                     operator==(const Residual_report&) const = default;
  uint64_t                 cost_before      = 0;
  uint64_t                 cost_after       = 0;
  uint64_t                 rewrite_windows  = 0;
  uint64_t                 rewrite_wins     = 0;
  uint64_t                 resub_windows    = 0;
  uint64_t                 resub_wins       = 0;
  uint64_t                 candidates       = 0;
  uint64_t                 depth_rejections = 0;
  uint64_t                 cost_rejections  = 0;
  uint64_t                 reference_visits = 0;
  bool                     skipped          = false;
  bool                     exhausted        = false;
  std::vector<std::string> limits;
};

struct Residual_network {
  Xag                          graph;
  // Same order/polarity as the requested outputs. Source order and names are
  // preserved, including unused ports. Only live combinational logic remains.
  std::vector<Residual_output> outputs;
  // Conservative structural-change flags for the one endpoint feedback round.
  std::vector<bool>            affected;
  uint64_t                     estimated_cost = 0;
};

struct Residual_result {
  Status                          status = Status::invalid;
  std::optional<Residual_network> network;
  Residual_report                 report;
  std::string                     reason;
};

// Native cleanup -> rewrite4 -> bounded resubstitution -> cleanup. The caller
// supplies every protected output: DOMINO inputs, POs and special-state ports.
// All replacements preserve their functions on an exact common input basis.
// Shared nodes earn deletion credit only after their final reference vanishes.
// Equal-cost replacements must strictly reduce depth. AND/XOR weights are
// positive area proxies; complemented edges are free in
// this proxy, not a promise about physical CMOS inversion cost. This operation
// neither changes state nor invokes ABC, a solver, or endpoint search itself.
// On an incomplete rebuild, no partial graph is returned; the input is intact.
Residual_result optimize_residual(const Xag& graph, std::span<const Residual_output> outputs, const Residual_options& options,
                                  Budget& work);

}  // namespace livehd::usyn
