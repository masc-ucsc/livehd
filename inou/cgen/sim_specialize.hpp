// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::sim {
// Propagate constant instance inputs in the simulator's private library.
// Native loop descriptors and state instance identity remain intact.
void specialize_constants(std::vector<std::shared_ptr<hhds::Graph>>& graphs);
}  // namespace livehd::sim
