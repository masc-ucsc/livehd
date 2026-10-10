// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "hhds/graph.hpp"
namespace livehd::synth_attr {
// Materialize typed, source-guided membership before automatic coloring.
void        specialize_calls(std::vector<std::shared_ptr<hhds::Graph>>& graphs);
bool        seed_groups(hhds::Graph* g);
std::string group_name(hhds::Graph* g, int color);
}  // namespace livehd::synth_attr
