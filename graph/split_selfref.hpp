// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>

#include "absl/container/flat_hash_set.h"
#include "hhds/graph.hpp"

namespace livehd::graph_util {

// Resolve a packed self-reference exactly when a single-driver wire's defining
// edge is attached. `driver` is already connected to `buffer`; `early_readers`
// are the buffer consumers that existed before the defining write. Only the
// dependency cone between those readers and `driver` is inspected -- there is
// no whole-graph cycle scan. Returns the number of slice reads rewired.
//
// This is a lowering operation, not an optimization or writer repair: after it
// returns, every downstream pass sees the same valid LGraph.
int split_packed_selfref_wire(hhds::Graph* g, const hhds::Node_class& buffer, const hhds::Pin_class& driver,
                              const std::vector<hhds::Node_class>& early_readers);

// Repair a packed self-reference EXPOSED by a structural mutation. The per-wire
// splitter above treats a `Sub` call as a scheduling boundary, so a packed wire
// whose feedback threads through a pure-comb instance is a no-op at bind time --
// and then dissolving that instance (flatten_false_loop_subs, pass.color absorb,
// sim.flatten) turns the boundary into ordinary word-level logic with nothing
// left to break it. Whoever performs such a mutation owns the repair; a writer
// still must not call this speculatively. Strictly a no-op when no word-level
// cycle exists. Returns the number of slice reads rewired.
int split_packed_selfref_cycles(hhds::Graph* g);

// Does `driver`'s backward cone still reach `target` COMBINATIONALLY? State,
// memories and Sub calls are scheduling boundaries, matching the model the
// splitter above uses -- which is why the two share one implementation: the
// caller that asks "did the split finish?" and the caller that asks "is a
// genuine self dependency left?" must not disagree about what a cycle is.
[[nodiscard]] bool comb_pin_depends_on(const hhds::Pin_class& driver, const hhds::Node_class& target);

// Break a false combinational loop that runs THROUGH a pure-comb sub-instance:
// inline the offending instance into `g` so the cycle becomes ordinary logic
// the scheduler can linearize. The whole callee CLOSURE must be state-free --
// nested comb Subs are fine and are re-instantiated as ordinary pure-comb leaf
// instances (the ExeUnitImp_4/Alu/AluDataModule shape) -- because inlining a
// Flop/Latch/Memory would change state identity. A no-op unless a stateless
// Sub's output actually feeds back into one of its own inputs.
//
// This separate legacy hierarchy repair is still writer-owned. cgen_verilog
// emits one always_comb of ordered BLOCKING assignments, so a residual cycle
// makes it emit a read before the line that assigns it -- Verilog that is not
// combinational at all (measured on tests/equiv/sim_sub_nested_comb_feedback:
// 299/300 vectors wrong at O0, 0/300 at O1, from the SAME source).
// Returns the number of instances inlined.
int flatten_false_loop_subs(hhds::Graph* g);

// The comb nodes of `g` that sit on a WORD-LEVEL cycle, non-mutating.
//
// `strict` picks the scheduling model. FALSE cuts a `Sub` call and a `Memory`
// read (both are boundaries to an event-driven simulator, and to the splitter
// above). TRUE treats them as ordinary nodes, which is what `inou.cgen.sim`'s
// single-pass `forward_class` walk does — so a node reported under `strict` but
// not otherwise is on a cycle the SCHEDULE manufactures rather than one the
// design has. Registers always cut: a q is last period's value.
void word_level_cycle_nodes(hhds::Graph* g, bool strict, absl::flat_hash_set<hhds::Node_class>& out,
                            const absl::flat_hash_set<hhds::Class_index>* allowed = nullptr);

}  // namespace livehd::graph_util
