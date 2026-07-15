// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// ABC cone prover: discharge ONE of the induction step's per-cut proof
// obligations (a Boolean cvc5 term that must be UNSAT) by bit-blasting the
// term's DAG into an AIG and running ABC's CEC engine on it.
//
// The obligation is taken as a cvc5 TERM, not re-derived from the LGraph. That
// is the whole point: the term already carries encode.cpp's exact semantics
// (flop reset/enable folding, the magnitude+1 width rules, Sub inlining, X
// masking), so a proven cone discharges LITERALLY the same formula the cvc5
// obligation would, and no width/semantics drift can make ABC "prove" a cut
// cvc5 would refute. The term's free-variable support IS the cone boundary:
// shared current-state symbols, shared primary inputs, and memory read douts
// all appear as free constants and become AIG primary inputs.
//
// Terms outside the pure bit-vector fragment (arrays, uninterpreted functions,
// division) are NOT blasted; the cut then stays in the cvc5 obligation. Free
// symbols carrying SMT-level constraints elsewhere (a memory dout tied to
// SELECT(array, addr) by an asserted equality) become unconstrained AIG inputs,
// which only makes the ABC obligation STRICTLY STRONGER than cvc5's -- proving
// it for every value of the free symbol proves it for the tied one -- so a
// Proven verdict is always sound to subtract.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <cvc5/cvc5.h>

namespace livehd::lec {

enum class Cone_verdict {
  Proven,       // the diff is UNSAT: this cut can be subtracted from the obligation
  Refuted,      // the diff is SAT (may be an unreachable step state -- NOT a refutation)
  Unsupported,  // outside the blastable fragment (arrays / UF / division)
  Unknown,      // ABC hit its resource limit
};

struct Cone_stats {
  int         ands = 0;   // AIG and-gates built
  int         pis  = 0;   // boundary inputs (the cone's support)
  std::string why;        // Unsupported: the term kind that stopped the blast
};

// Try to prove `diff` (a Boolean term) UNSAT. `backtrack_limit` caps ABC's SAT
// effort (0 = ABC's own default). Never throws; any internal bail returns
// Unsupported so the caller keeps the cut.
//
// WARNING: this runs ABC in-process and has NO wall-clock bound -- ABC's
// Prove_Params limits only cap SAT conflicts, while the rewriting/fraiging
// phases that dominate a datapath cone are unbounded (an 8-bit multiplier
// reassociation miter of ~1k AIG nodes takes ~40s no matter how low the
// conflict limit is set). Callers on a budget must use abc_prove_unsat_batch.
Cone_verdict abc_prove_unsat(const cvc5::Term& diff, int64_t backtrack_limit, Cone_stats* st = nullptr);

// Prove as many of `diffs` as possible within `deadline_ms` of wall clock.
//
// The work runs in a FORKED CHILD, because that is the only reliable way to put
// a clock on ABC (see the warning above); the child streams each verdict back as
// it lands, so a deadline kill keeps every cone proven so far. The returned
// vector is index-aligned with `diffs`; cones the child never reached come back
// as Unknown, which callers must treat as "not proven" -- i.e. the obligation
// stays. deadline_ms <= 0 means no clock (the child still isolates ABC).
//
// Falls back to reporting everything Unknown if the fork fails: losing the
// optimization is always preferable to losing the bound.
std::vector<Cone_verdict> abc_prove_unsat_batch(const std::vector<cvc5::Term>& diffs, int64_t backtrack_limit,
                                                int64_t deadline_ms);

std::string_view cone_verdict_name(Cone_verdict v);

}  // namespace livehd::lec
