// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::synth {
struct Sharing_hint_result {
  bool                     completed = false;
  uint64_t                 roots = 0, nodes = 0;
  uint64_t                 candidates = 0, scalar_rejections = 0, closure_rejections = 0;
  std::vector<std::string> rejection_examples;
};
// Experimental mapping partition on a PRIVATE prepared graph. Admit high-fanout
// scalar Boolean cones of at most 128 nodes / six external source pins. Color
// their complete combinational fan-in closure together, ending at PI/register
// boundaries, so the quotient cannot introduce a combinational back edge.
// No selected DOMINO metadata is consumed and no source is mutated. Zero is
// the unchanged unrestricted-map control. This heuristic is not a cost claim.
Sharing_hint_result preserve_shared_cones(hhds::Graph& graph, uint32_t minimum_fanout,
                                          const std::function<bool(std::string_view)>& admission = {});
}  // namespace livehd::synth
