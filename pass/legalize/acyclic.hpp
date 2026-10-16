// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// pass.legalize acyclicity: the ONE place that reasons about combinational
// loops (todo/livehd/legalize_acyclic.md). After repair and any deferred
// check following cprop/bitwidth, every definition is
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
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "legalize.hpp"

namespace livehd::legalize {

inline constexpr std::string_view kAcyclicPass = "pass.legalize";

struct Acyclic_result {
  int  instances_inlined = 0;
  int  loops_split       = 0;
  int  slices_rewired    = 0;
  int  loops             = 0;      // candidate loops left (errors unless deferred)
  bool needs_loop_check  = false;  // rewritten candidate: check after the existing cprop + bitwidth
  // per graph: callee names of the instances inlined into it
  absl::flat_hash_map<const hhds::Graph*, std::vector<std::string>> inlined;
};

// Establish the arc-level contract on every live graph, callee first: inline
// each instance on an arc-level cycle (warning comb-loop-through-module) and
// each state-free instance on an atomic-level one, split each rolled loop on
// an atomic-only ring by ring/free carries, fixpoint; resolve packed slices on
// what remains inside a body; report any loop left as an error. With
// defer_inlined_loops, carry internal candidates into their callers and leave
// rewritten candidates for check_comb_cycles after the scheduled cleanup.
// Definitions made unreachable by loop repair land in state.removed; the caller deletes
// them and drops them from its view. Explicit roots are retained even when
// instantiated elsewhere. No cprop/bitwidth runs inside this pass.
Acyclic_result make_acyclic(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Split_state& state,
                            bool defer_inlined_loops = false, const std::vector<std::shared_ptr<hhds::Graph>>& roots = {});

// Final user-facing loop verdict, with fresh dependency summaries after cleanup.
int check_comb_cycles(const std::vector<std::shared_ptr<hhds::Graph>>& graphs);

// Read-only: the arc-level combinational cycles left in `graphs` (0 on a
// legalized design). Each one is described in `where` when given.
[[nodiscard]] int count_comb_cycles(const std::vector<std::shared_ptr<hhds::Graph>>& graphs,
                                    std::vector<std::string>*                        where = nullptr);

}  // namespace livehd::legalize
