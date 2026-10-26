// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
struct Reset_tree_stats {
  unsigned nets    = 0;  // nets that were split
  unsigned sinks   = 0;  // cell pins moved behind a buffer (the trigger pins and every other load of the same net)
  unsigned buffers = 0;  // buffers minted
  unsigned levels = 0;   // deepest tree
  unsigned skipped = 0;   // trigger pins/drivers left alone: a multi-bit or multi-driver wiring alias
};
// `is_constant(driver)` says a driver resolves to a constant through bus wiring: the inactive pin of a
// clear+preset flop is tied by such an alias, and a tree behind a tie cell would only add cells.
using Constant_test = std::function<bool(const hhds::Pin_class&)>;
using Reset_pins    = std::map<std::string, std::vector<std::string>>;  // cell (LEF macro) name -> pin names

// Pre-placement reset buffer tree over the FLAT mapped netlist (synth.place.reset_fanout), the one
// reset-buffering mechanism of every mapper (abc, usyn, the domino profile). A reset net is a net
// that drives an asynchronous clear/preset pin of a Liberty register, an `extra_reset_pins` pin (the
// domino storage RST pins of 2d-domplace U22), or whose driver carries the synthesis reset role (a
// synchronous reset folded into data logic). A reset net with more than `cap` cell loads in all (its
// data loads included) is dealt out through the library's smallest buffer, `cap` pins per buffer,
// level by level, until the driver carries at most `cap` loads. Pins are grouped by instance-name
// order, which follows the source hierarchy, so a buffer serves neighbouring registers. Pure fanout
// restructuring: every pin keeps its logical driver (a chain of identity buffers), `cap` below 2 or a
// library without a placeable buffer does nothing. Idempotent. Buffers are named `__place_rsttree*`.
Reset_tree_stats buffer_reset_nets(hhds::Graph& graph, const physical::Technology& tech, const std::string& liberty, unsigned cap,
                                   const Constant_test& is_constant = {}, const Reset_pins& extra_reset_pins = {});

// The same tree for the nets on `pins` only (the domino data rails, synth.place.domino_rail_fanout),
// buffers named `__place_<tag>*`.
Reset_tree_stats buffer_pin_nets(hhds::Graph& graph, const physical::Technology& tech, const std::string& liberty, unsigned cap,
                                 const Reset_pins& pins, std::string_view tag, const Constant_test& is_constant = {});
}  // namespace livehd::placement
