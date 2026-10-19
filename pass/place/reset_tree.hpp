// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string>

#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
struct Reset_tree_stats {
  unsigned nets = 0;     // reset nets that were split
  unsigned sinks = 0;    // cell pins moved behind a buffer (async clear/preset and every other load of the same net)
  unsigned buffers = 0;  // buffers minted
  unsigned levels = 0;   // deepest tree
};
// Pre-placement fanout tree over the FLAT mapped netlist (synth.place.reset_fanout). A net that
// drives an asynchronous clear/preset pin of a Liberty flop and more than `cap` cell pins in all
// (the sync-reset logic of the same signal included) is dealt out through the library's smallest
// buffer, `cap` pins per buffer, level by level, until the driver carries at most `cap` loads.
// Pins are grouped by instance-name order, which follows the source hierarchy, so a buffer serves
// neighbouring registers. Pure fanout restructuring: every pin keeps its logical driver (a chain
// of identity buffers), `cap` below 2 or a library without a flop/buffer does nothing. Idempotent.
Reset_tree_stats buffer_reset_nets(hhds::Graph& graph, const physical::Technology& tech, const std::string& liberty, unsigned cap);
}  // namespace livehd::placement
