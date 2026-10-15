// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <string>

#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Physical-only masters for native memories. The graph stays a Memory, with
// its exact state semantics; these masters carry geometry, never a logic model.
std::map<uint64_t, std::string> memory_proxies(hhds::Graph& graph, physical::Technology& tech, const std::string& liberty);
bool                            memory_runtime_pin(hhds::Port_id pid);
std::string                     memory_pin(hhds::Port_id pid, int bit, bool output);
}  // namespace livehd::placement
