// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// pass.legalize acyclicity: the ONE place that reasons about combinational
// loops (todo/livehd/legalize_acyclic.md). After legalize, every definition is
// acyclic at ARC level: each Sub instance is one call whose output depends on
// the inputs its callee's port_reach summary lists (its comb arcs; a Moore
// output on none). A consumer orders instances by those arcs (or flattens) and
// never schedules around a loop -- sim, LEC, Verilog, synth. A STATE-FREE
// instance or a rolled loop is also never left on a cycle with instances
// atomic (the instance inlined, the loop split by ring/free carries -- never
// unrolled), so a consumer never has to evaluate a comb callee in pieces.
//
// A vertex is a DRIVER PIN. Combinational dependence:
//   Flop/Fflop/Latch outputs, graph inputs, constants   none (cut)
//   Memory outputs        an async read: its address/enable (port_reach::
//                         Memory_deps without write cones -- every consumer
//                         sequences a forwarded/unclocked write after the read)
//   Sub outputs           the callee's port_reach summary (the arcs), closed
//                         over the carries for a rolled loop; a body-less
//                         blackbox is a source
//   any other node        every input

#include <cstdint>
#include <string_view>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "legalize.hpp"

namespace livehd::legalize {

inline constexpr std::string_view kAcyclicPass = "pass.legalize";

struct Acyclic_result {
  int instances_inlined = 0;
  int loops_split       = 0;
  int slices_rewired    = 0;
  int loops             = 0;  // true loops left (each a diag error)
  // per graph: callee names of the instances inlined into it
  absl::flat_hash_map<const hhds::Graph*, std::vector<std::string>> inlined;
};

// Establish the arc-level contract on every live graph, callee first: inline
// each instance on an arc-level cycle (warning comb-loop-through-module) and
// each state-free instance on an atomic-level one, split each rolled loop on
// an atomic-only ring by ring/free carries, fixpoint; resolve packed slices on
// what remains inside a body; report any loop left as an error. Loop halves
// land in `state` like legalize's own loop split.
Acyclic_result make_acyclic(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Split_state& state);

}  // namespace livehd::legalize
