// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>
#include <unordered_set>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::abc {
// Fold iteration constants in ABC's already-materialized private library.
// Ordinary module boundaries and bodies with explicit region options stay intact.
bool cleanup_loop_bodies(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, const std::unordered_set<hhds::Gid>& loop_bodies);
}  // namespace livehd::abc
