// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::sim {
// Simulator-private rewrite. Fuse independent sibling loops with equal domains
// into one ordinal traversal. Returns newly created compact body definitions.
// Must run before any occurrence handles or generation digests are retained.
std::vector<std::shared_ptr<hhds::Graph>> fuse_parallel_loops(hhds::Graph* graph);
}  // namespace livehd::sim
