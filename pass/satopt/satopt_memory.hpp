// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"
#include "satopt_stages.hpp"
namespace livehd::satopt {
struct Memory_satopt {
  // Per rewritten memory, write ordinal pairs that cannot update the same lane.
  std::map<std::pair<std::string, uint64_t>, std::set<std::pair<int, int>>> independent;
  uint64_t merged_writes = 0, merged_reads = 0, dead_ports = 0, collision_bits = 0, address_bits = 0;
  uint64_t queries = 0, proven = 0, reused = 0, budget_skips = 0;  // solver questions asked, answered yes, cached
};
// All proofs are combinational; memory read data and flop Q are independent
// symbols. Profile::synthesis (pass.abc's private copy) merges, removes and
// narrows ports and clears provably unreachable collision windows, rebuilding
// the Memory; Profile::shared only ties a write enable proven never active to
// 0 in place, since every other transform refines a don't-care value, changes
// the memory's identity or leaves an ordering simulation cannot model.
// `meter` bounds the solver queries (null = unlimited): an unaffordable one is
// answered `not proven`, so its rewrite is skipped.
Memory_satopt optimize_memories(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view cache_dir = {},
                                Profile profile = Profile::synthesis, Meter* meter = nullptr);
}  // namespace livehd::satopt
