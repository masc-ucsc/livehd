// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::liberty {
struct Cell_library;
}

// pass.pcrs: power, clock, reset and scan-chain networks. Only reset exists so far.
//
// A reset network is one reset signal and everything it reaches through identity buffers and
// inverters. The pass rebuilds that distribution as one balanced tree per (reset root, clock
// domain): N levels of flops (N >= 0) and a fanout cap per driver, so every sink of a domain sees
// the reset exactly N cycles late and the tree is as deep as timing needs, not as deep as fanout
// would force. A tree is rebuilt from its sinks every call (the previous tree is stripped first),
// so changing a domain's depth is just a second call with another depth.
//
// Asynchronous reset pins are served the same way (the reset is registered to the clock edge), and
// the synchronous-reset logic of the same signal moves behind the tree with them. Sinks that are
// not placeable library cells (graph outputs, macros) keep their direct connection.
namespace livehd::pcrs {

struct Options {
  unsigned fanout = 32;  // loads on any driver of the tree (>= 2)
};

// How deep one clock domain's tree is. `domain` names the (reset root, root clock) pair, `sinks` is
// its load count. The default is a flat net for a network that one driver can serve, one flop
// level otherwise.
using Depth_policy = std::function<unsigned(std::string_view domain, size_t sinks)>;

// Where a placed cell is, for spatial clustering (the sinks of one flop are neighbours). An empty
// result keeps instance-name order, which follows the source hierarchy.
using Position_of = std::function<std::optional<std::pair<double, double>>(std::string_view instance)>;

struct Domain_stats {
  std::string name;
  unsigned    depth   = 0;
  unsigned    sinks   = 0;
  unsigned    flops   = 0;
  unsigned    buffers = 0;
};

struct Stats {
  unsigned                  networks = 0;  // reset roots rebuilt
  unsigned                  sinks    = 0;
  unsigned                  flops    = 0;
  unsigned                  buffers  = 0;
  unsigned                  removed  = 0;  // cells of the previous distribution deleted
  unsigned                  skipped  = 0;  // roots left alone (multi-bit, multi-driver or tied)
  double                    flop_um2 = 0;  // LEF area of the flop master (0: none placeable)
  std::vector<Domain_stats> domains;
};

// `depth_of` and `position_of` are optional. The graph must be the FLAT mapped netlist.
Stats build_reset(hhds::Graph& graph, const physical::Technology& tech, const liberty::Cell_library& cells, const Options& options,
                  const Depth_policy& depth_of = {}, const Position_of& position_of = {});

// A cell minted by this pass, by its instance name (leaf segment after the hierarchy prefix).
std::string_view instance_leaf(std::string_view instance);
bool             is_pcrs_cell(std::string_view instance);

}  // namespace livehd::pcrs
