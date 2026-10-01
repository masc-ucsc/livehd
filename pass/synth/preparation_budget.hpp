// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <string_view>

namespace livehd::synth {

// Optional, caller-owned admission for private synthesis preparation. Source
// nodes are counted once per copied definition, not once per instance. Work
// checkpoints cover traversal; zero-work checkpoints bracket bulk operations
// so callers can force a process/time sample. This does not predict bytes or
// interrupt an individual HHDS copy or occurrence-materialization operation.
struct Preparation_budget {
  uint64_t                                        max_source_nodes = std::numeric_limits<uint64_t>::max();
  uint64_t                                        source_nodes     = 0;
  std::function<bool(std::string_view, uint64_t)> admission;
  bool                                            refused = false;

  bool check(std::string_view stage, uint64_t work = 1) {
    if (refused) {
      return false;
    }
    if (admission && !admission(stage, work)) {
      refused = true;
      return false;
    }
    return true;
  }

  bool source_node() {
    if (refused || source_nodes >= max_source_nodes) {
      refused = true;
      return false;
    }
    ++source_nodes;
    return check("source-node");
  }
};

inline bool admit_preparation(Preparation_budget* budget, std::string_view stage, uint64_t work = 1) {
  return !budget || budget->check(stage, work);
}

}  // namespace livehd::synth
