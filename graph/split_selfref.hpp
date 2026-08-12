// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "absl/container/flat_hash_set.h"
#include "hhds/graph.hpp"

namespace livehd::graph_util {

// Dissolve a WORD-level-false combinational cycle through a packed wire: a net
// built as an Or/shift concat accumulator that is read back via constant
// Get_mask slices or And(SRA(w,k),mask) readers looks cyclic at word level
// while the bit-level DAG is acyclic (the Chisel arbiter grant-chain shape).
// Each such slice-read is rebuilt from only the operands whose proven bit
// footprints overlap it (including bounded EQ/Mux controls). Strictly
// a NO-OP unless a genuine word-level comb cycle exists; a genuine bit-level
// loop (e.g. w = w+1) is never split and still fails loudly downstream.
//
// Runs in pass/cprop (every backend sees the acyclic DAG: lec encode, cgen,
// sim scheduling) and again from inou/cgen/cgen_sim for O0 graphs that skip
// cprop. Returns the number of rewired reads.
int split_packed_selfref_wires(hhds::Graph* g);

// Break a false combinational loop that runs THROUGH a pure-comb sub-instance:
// inline the offending instance into `g` so the cycle becomes ordinary logic
// the scheduler can linearize. The whole callee CLOSURE must be state-free --
// nested comb Subs are fine and are re-instantiated as ordinary pure-comb leaf
// instances (the ExeUnitImp_4/Alu/AluDataModule shape) -- because inlining a
// Flop/Latch/Memory would change state identity. A no-op unless a stateless
// Sub's output actually feeds back into one of its own inputs.
//
// Same deal as split_packed_selfref_wires above, and it must run in the same
// places: a WRITER cannot assume an optimization pass ran first. cgen_verilog
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
