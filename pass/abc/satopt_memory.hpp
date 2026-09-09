// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <set>
#include <vector>

#include "hhds/graph.hpp"
namespace livehd::abc {
struct Memory_satopt {
  // Per rewritten memory, write ordinal pairs that cannot update the same lane.
  std::map<std::pair<std::string, uint64_t>, std::set<std::pair<int, int>>> independent;
  uint64_t merged_writes = 0, merged_reads = 0, dead_ports = 0, collision_bits = 0, address_bits = 0;
};
// Applies only to the caller's synthesis working copy. All proofs are
// combinational; memory read data and flop Q are independent symbols.
Memory_satopt optimize_memories(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view cache_dir = {});
}  // namespace livehd::abc
