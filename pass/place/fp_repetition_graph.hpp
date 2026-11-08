// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string>

#include "fp_repetition.hpp"
#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// The flop/memory view of a mapped (possibly hierarchical) netlist for
// detect_fp_repetition. Flops are Liberty sequential masters (and unmapped
// registers at the smallest flop area); memories are LEF BLOCK masters and
// native memories at their proxy area. Other cells are ignored.
Fp_rep_design fp_repetition_design(hhds::Graph& top, const physical::Technology& tech, const std::string& liberty);
}  // namespace livehd::placement
