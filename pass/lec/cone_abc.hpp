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
#include <unordered_map>
#include <vector>

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
// `st`, when given, is resized to diffs.size() and filled with each cone's size
// (cones the deadline cut off keep their zero-initialized entry).
//
// `merge` (optional) maps a free symbol onto a representative symbol, so both
// blast to the SAME primary inputs. This is speculative reduction: assuming two
// symbols equal makes the obligation WEAKER, so -- unlike everything else here --
// it is NOT free. Every entry must already have been discharged by its own proof
// (see mergeable_dout_pairs); an undischarged merge would be a false PROVEN.
using Cone_merge_map = std::unordered_map<cvc5::Term, cvc5::Term>;
std::vector<Cone_verdict> abc_prove_unsat_batch(const std::vector<cvc5::Term>& diffs, int64_t backtrack_limit,
                                                int64_t deadline_ms, std::vector<Cone_stats>* st = nullptr,
                                                const Cone_merge_map* merge = nullptr);

// Canonical structural digest of a cone obligation (32 hex chars / 128 bits).
//
// This is a COMPLETE cache key, which is what makes a cone cache simpler than
// the def-pair one: the obligation is self-contained -- "is this term UNSAT,
// with every free symbol unconstrained" -- so nothing about the run needs to
// enter the key. Everything that could change the answer is already IN the
// term: the widths, the flop reset/enable folding, the X-mask, the inlined cell
// bodies, and the names of the boundary symbols. Two cones with the same digest
// are the same formula, so a PROVEN transfers.
//
// Hashes the DAG directly (memoized, one visit per node) rather than the
// printed form: toString() is safe on a shared DAG (cvc5 let-binds, so it does
// not blow up) but it would couple a PERSISTED key to cvc5's printer and its
// _let_N numbering, and the walk is cheaper than materializing the string.
//
// Stable across runs and processes: only kinds, sorts, operator indices,
// constant values and symbol NAMES feed the hash -- never term ids or pointers.
// Returns "" for a term that has no such stable identity (an unnamed symbol
// prints as var_<allocation-id>); an empty digest means DO NOT CACHE, never
// "cache under the empty key".
std::string cone_digest(const cvc5::Term& t);

std::string_view cone_verdict_name(Cone_verdict v);

}  // namespace livehd::lec
