// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <string>

#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Physical-only masters for native memories. The graph stays a Memory, with
// its exact state semantics; these masters carry geometry, never a logic model.
struct Memory_proxy_shape {
  uint64_t bits = 0, depth = 0;
  double   area = 0;  // um^2: bits * depth * flop_area * 1.1^ports
};
// The proxy geometry of one native Memory node (throws on non-constant width/depth).
Memory_proxy_shape              memory_proxy_shape(const hhds::Node_class& node, double flop_area);
std::map<uint64_t, std::string> memory_proxies(hhds::Graph& graph, physical::Technology& tech, const std::string& liberty);
// Quantize edge-pin centers to a common phase of the actual routing tracks.
// Apply after floorplan/track import, before fixing proxy origins or building pins.
void                            align_memory_proxy_pins(physical::Technology& tech, const physical::Floorplan& floorplan);
bool                            memory_runtime_pin(hhds::Port_id pid);
std::string                     memory_pin(hhds::Port_id pid, int bit, bool output);
}  // namespace livehd::placement
