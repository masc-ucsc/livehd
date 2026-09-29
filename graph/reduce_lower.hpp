// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>

#include "hhds/graph.hpp"

namespace livehd::graph_util {

// Certificate exporters have a fixed primitive-op vocabulary. Expand counted
// reductions in a private copy, leaving the source graph and its native cells
// intact. The caller keeps scratch alive for the returned graph's lifetime.
std::shared_ptr<hhds::Graph> lower_counted_reductions_copy(const std::shared_ptr<hhds::Graph>& graph, hhds::GraphLibrary& scratch);

}  // namespace livehd::graph_util
