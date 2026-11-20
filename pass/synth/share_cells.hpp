// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

#include "hhds/graph.hpp"

namespace livehd::synth {

struct Share_stats {
  uint64_t cells       = 0;  // duplicate cells removed (their readers read the one that stays)
  uint64_t cross_color = 0;  // ... of which the one that stays is in another color
  uint64_t refused     = 0;  // duplicates kept: sharing them would add a dependency loop between two colors
};

// Structural sharing of duplicate logic, linear in the graph (synth.share_cells, on by default). One forward pass:
// a combinational cell is identified by its operation, its output width and sign and the identity of each operand
// (a constant by value, any other driver by the cell that already stands for it), the operands of And/Or/Xor/Sum/
// Mult/EQ/Ror as a multiset; a cell identical to an earlier one is removed and its readers read the earlier one. A
// duplicate found this way removes the duplicates built on top of it in the same pass, at any depth.
//
// The e-graph fold shares within one definition as it rewrites; this pass sees what only exists afterwards: the same
// logic in two definitions once they are inlined into one graph, and cells later steps mint. Run on a graph right
// before it is cut into regions. Colors: the earlier cell stays where it is, so its value may newly cross into the
// duplicate's color; that is refused when it would make the two colors depend on each other in a loop they did not
// have. Cells with state, instances, clock cells, latches and their readers are never touched.
Share_stats share_duplicate_cells(hhds::Graph* graph);

}  // namespace livehd::synth
