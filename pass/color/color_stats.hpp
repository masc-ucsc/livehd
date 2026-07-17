// This file is distributed under the BSD 3-Clause License. See LICENSE for
// details.
#pragma once

// `pass.color --stats`: what did the coloring actually produce?
//
// The unit reported is a PARTITION = one (def, color) pair, because that is the
// unit pass.partition emits (`<def>__c<id>`, one Partitioner per def) and
// therefore the unit pass.abc later hands to ABC. Two defs that happen to share
// a color id are two partitions, not one.
//
// Every number here is accumulated from the walk apply_coloring already makes,
// so the report costs no extra traversal and works for every algorithm.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "color_common.hpp"

namespace livehd::color {

class Color_stats {
public:
  // Fold one def's outcome in. `def` is the graph name (for the per-def table).
  // `instances` is how many times the def appears under --top, which is what
  // makes the `flat` projection right: `pass.color flat` sets pass.abc's
  // flatten=auto, so ABC inlines the hierarchy and maps ONE region holding
  // every instance -- not one region per def.
  void add(std::string_view def, const Def_color_sizes &sizes,
           uint64_t instances);

  [[nodiscard]] bool empty() const { return partitions_.empty(); }

  // Human report on STDERR. Not stdout: run_step dup2()s fd 1 into the pass log
  // for the whole pass body (lhd/lhd_kernel_common.cpp), so a stdout report is
  // invisible unless --verbose. fd 2 is never redirected.
  // `per_def` also prints the per-def table (pass.color.verbose).
  void report(std::string_view alg, bool per_def) const;

private:
  struct Partition {
    std::string def;
    int         color = 0;
    uint64_t    nodes = 0;
  };
  struct Def_row {
    std::string def;
    uint64_t    nodes = 0, uncolored = 0, instances = 1;
    size_t      colors = 0;
  };

  std::vector<Partition> partitions_;
  std::vector<Def_row>   defs_;
  uint64_t               total_nodes_ = 0;
  uint64_t               total_uncolored_ = 0;
  uint64_t               flat_nodes_ = 0;  // sum of nodes * instances

  // Partition sizes, largest first. Sorted by (size, def, color) so the report
  // does not inherit hash-iteration order.
  [[nodiscard]] std::vector<uint64_t> sizes_desc() const;
};

}  // namespace livehd::color
