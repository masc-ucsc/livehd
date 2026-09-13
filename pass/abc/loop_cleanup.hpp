// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <memory>
#include <unordered_set>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::abc {
// Retain independently mapped loop definitions through partitioning. Carry
// expansion is per instance: a definition can serve both kinds of loop.
struct Loop_preparation {
  std::unordered_set<hhds::Gid> preserved_defs;
  size_t                        independent = 0;
  size_t                        carried     = 0;
  size_t                        expanded    = 0;
};
bool prepare_loop_bodies(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, bool unroll_carry, Loop_preparation& result);
}  // namespace livehd::abc
