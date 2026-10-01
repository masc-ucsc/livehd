// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "hhds/graph.hpp"
#include "preparation_budget.hpp"

namespace livehd::synth {
// Retain independently mapped loop definitions through partitioning. Carry
// expansion is per instance: a definition can serve both kinds of loop.
struct Loop_preparation {
  std::unordered_set<hhds::Gid>             preserved_defs;
  std::vector<std::shared_ptr<hhds::Graph>> shared_bodies;
  size_t                                    independent = 0;
  size_t                                    carried     = 0;
  size_t                                    expanded    = 0;
};
bool prepare_loop_bodies(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, bool unroll_carry, Loop_preparation& result,
                         std::string_view from_pass = "pass.synth", Preparation_budget* budget = nullptr);
}  // namespace livehd::synth
