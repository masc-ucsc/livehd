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
  // Rebuild every network, also one whose domains all keep their depth: the first call that knows where the
  // cells are regroups the sinks by position (a tree built before placement follows instance names).
  bool regroup = false;
};

// How deep one clock domain's tree is. `domain` names the (reset root, root clock) pair, `sinks` is
// its load count, `current` the flop levels of the tree already in the graph (absent: none yet). The
// default keeps the current depth, and starts a new domain flat when one driver can serve it, with
// one flop level otherwise. A network whose domains all keep their depth is left untouched.
using Depth_policy = std::function<unsigned(std::string_view domain, size_t sinks, std::optional<unsigned> current)>;

// Where a placed cell is, for spatial clustering (the sinks of one flop are neighbours). An empty
// result keeps instance-name order, which follows the source hierarchy.
using Position_of = std::function<std::optional<std::pair<double, double>>(std::string_view instance)>;

struct Domain_stats {
  std::string name;
  unsigned    depth   = 0;
  unsigned    sinks   = 0;
  unsigned    flops   = 0;
  unsigned    buffers = 0;
  bool        kept    = false;  // already at this depth: the tree was not rebuilt
  // Full instance names of the cells serving this domain, for mapping a timing path back to its
  // domain. Flops are exact; buffers of a network with several domains are listed with its first domain.
  std::vector<std::string> cells;
  // `instance:pin` labels of the domain's flop D and Q pins, where the reset hops are timed.
  std::vector<std::string> flop_pins;
};

struct Stats {
  unsigned                  fanout   = 0;  // the cap the trees were built with
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

// Replace every flop this pass minted (node attribute pcrs_stage) by a wire, or by an inverter when the cell's Q is
// inverted, and delete it. Formal equivalence then compares the design as if the network had zero depth, which is
// what the design means whatever depth timing chose. Returns the flops bypassed. Each flop's D pin is port 1 and its
// Q pin port 3 (liberty::create_dff_io).
unsigned bypass_stages(hhds::Graph& graph);

// The depth each clock domain should have, shared by every placement call of one flow so a decision
// made in a timing round survives the next build. Unknown domains start by the default policy.
class Depth_state {
public:
  unsigned max_depth = 8;
  [[nodiscard]] std::optional<unsigned> get(std::string_view domain) const;
  void                                  set(std::string_view domain, unsigned depth);
  // Explicit entry first, else the depth already in the graph, else the default start.
  [[nodiscard]] Depth_policy policy(unsigned fanout) const;
  [[nodiscard]] const std::map<std::string, unsigned, std::less<>>& all() const { return depth_; }
  bool clustered = false;  // the trees in the graph were grouped by cell position (see Options::regroup)

private:
  std::map<std::string, unsigned, std::less<>> depth_;
};

// Timing evidence for a depth decision: the physical labels (`instance:pin`) of the worst setup path,
// and the slack/arrival of any pin (nullopt: the pin is unknown).
struct Pin_timing {
  std::optional<double> slack, arrival;
};
using Timing_of = std::function<Pin_timing(std::string_view label)>;

// Deepen the domain whose cells are on the worst setup path: one level at least, and straight to the depth
// where no hop branches by more than `branching` (each timing round is expensive, and a reset hop is
// spread over the die, so a narrower tree is what shortens it). A reset path that is the critical path is
// what a deeper tree fixes; the worst path of the design elsewhere is not this pass's business, and TNS is
// not consulted (the clock goal may be unreachable). One domain per call. Returns the domain that grew, or empty.
std::string retarget_for_wns(const Stats& stats, const std::vector<std::string>& worst_path, Depth_state& state, unsigned branching = 8);

// Remove one flop level from every domain whose reset hops have slack to spare: its worst slack must
// exceed the longest hop arrival plus `margin` (merging two hops costs about one more hop). Meant for
// the last timing iteration, when the placement is settled. Returns the domains that shrank.
std::vector<std::string> retarget_shrink(const Stats& stats, const Timing_of& timing, double margin, Depth_state& state);

// A cell minted by this pass, by its instance name (leaf segment after the hierarchy prefix).
std::string_view instance_leaf(std::string_view instance);
bool             is_pcrs_cell(std::string_view instance);

}  // namespace livehd::pcrs
