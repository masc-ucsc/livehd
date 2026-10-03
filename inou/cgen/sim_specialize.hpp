// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::sim {
// Propagate constant instance inputs in the simulator's private library.
// Native loop descriptors and state instance identity remain intact.
void specialize_constants(std::vector<std::shared_ptr<hhds::Graph>>& graphs);

// One constant-folding and width round over a private body a structural
// rewrite just changed (a small compact loop spliced flat exposes its ordinal
// arithmetic with constant inputs): the same pair specialize_constants runs
// after a callee publishes a constant. Kept here so cgen_sim.cpp does not
// include the pass headers (pass/common/pass.hpp's `Pass` shadows its locals).
void refold_private_body(const std::shared_ptr<hhds::Graph>& graph);
}  // namespace livehd::sim
