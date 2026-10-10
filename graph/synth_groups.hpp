// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "hhds/graph.hpp"
#include <string>
#include <memory>
#include <vector>
namespace livehd::synth_attr {
// Materialize typed, source-guided membership before automatic coloring.
void specialize_calls(std::vector<std::shared_ptr<hhds::Graph>>& graphs);
bool seed_groups(hhds::Graph* g);
std::string group_name(hhds::Graph* g, int color);
}
