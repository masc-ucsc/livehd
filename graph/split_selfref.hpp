// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

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

}  // namespace livehd::graph_util
