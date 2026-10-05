// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "native_cost.hpp"
#include "xag_choice.hpp"

namespace livehd::usyn {
struct Choice_options {
  uint32_t classes = 128, max_nodes = 2000000;
  uint32_t and_cost = 2, xor_cost = 4;
  bool     gate_objective = true;
};
struct Choice_report {
  uint64_t        classes = 0, candidates = 0, retained = 0, selections = 0, cycles = 0, extractions = 0;
  uint64_t        bdd_nodes = 0, sop_cubes = 0, dsd_blocks = 0, scratch_nodes = 0;
  uint64_t        rejected = 0;
  bool            limited  = false;
  Native_estimate before, after;
};
struct Choice_result {
  Status                           status = Status::invalid;
  std::optional<Choice_extraction> network;
  Choice_report                    report;
};
// Bounded local multi-representation ownership followed by at most two complete
// initial area/depth extractions from the same immutable snapshot and eight
// bounded shared-area recovery trials. Whole-network
// repricing counts selected sharing, never retained candidate references. Keep
// the incumbent on refusal, cycles, increased cost or increased output depth.
Choice_result optimize_choices(const Xag& graph, std::span<const Xsignal> outputs, const Choice_options& options, Budget& work,
                               const Native_cost_model* model = nullptr);
}  // namespace livehd::usyn
