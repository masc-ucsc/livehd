// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"

namespace livehd::lec {

// L1 relational query API. v1 covers the combinational equivalence client:
// prove_equal(ref, impl) under assume_equal(primary inputs). prove_distinct /
// is_sat are the duals/relatives (added as the clients land).
enum class Verdict { Proven, Refuted, Unknown };

struct Query_result {
  Verdict     verdict = Verdict::Unknown;
  std::string witness;  // satisfying input assignment when Refuted
  std::string detail;   // engine / bound / encode error, for diagnostics

  // Which engine produced this verdict + how long it took (for the per-block
  // progress/info record). Normally just the requested engine; under the `auto`
  // portfolio it is the engine that reached the trustworthy verdict FIRST
  // (inductive-Proven or BMC-Refuted) — and for an inconclusive auto run it lists
  // the attempted engines. `elapsed_ms` is that engine's wall-clock (-1 = unset).
  std::string engine;
  long long   elapsed_ms = -1;

  // BMC bound bookkeeping for the `auto` portfolio's bounded-Proven policy: the
  // checked-window size and the number of (output,cycle) comparisons actually run.
  // A BMC `Proven` with output_checks>0 is a BOUNDED proof (no CEX <= bound), which
  // `auto` reports as PASS (transparently labelled) rather than inconclusive —
  // deeper-than-bound cycles are out of scope by design.
  int checked_steps = 0;
  int output_checks = 0;

  // Structural-correspondence report (so `lhd lec` can ITERATE instead of bailing
  // on the first unmatched cut point). When the two designs don't expose the same
  // set of state/outputs, the miter still runs over the COMMON ones and these list
  // what is unmatched on each side (human-readable, control prefixes stripped).
  // Non-empty `unmatched_*` ⇒ the verdict cannot be a definitive Proven (the
  // correspondence is incomplete); the engine reports Unknown but `detail`/`witness`
  // still carry the matched-portion result + the per-output divergences found.
  std::vector<std::string> unmatched_ref;   // in ref, no corresponding impl signal
  std::vector<std::string> unmatched_impl;  // in impl, no corresponding ref signal
};

// Discharge / engine knobs (filled from the lec.* set-options).
struct Lec_options {
  // Reference-side X semantics — the cvc5 analogue of yosys `miter
  // -ignore_gold_x`. "ignore" (default): a ref constant's '?' bits source an
  // undef bit-plane; the miters exclude ref-unknown output/next-state bits from
  // the compare, and the shared state hypothesis binds ref X-state to the
  // impl's value (any impl choice is a legal resolution of a ref don't-care).
  // "zero": legacy behavior — '?' bits silently concretized to 0 on BOTH sides.
  std::string gold_x = "ignore";

  std::string engine  = "bmc";   // bmc | ind (k-induction) | ic3 | auto (portfolio). This is
                                 // the programmatic-API fallback (a single, fork-free engine for
                                 // in-pass design-queries); the `lhd lec` CLI defaults to `auto`.
  std::string solver  = "cvc5";  // cvc5 | bitwuzla (not yet built)
  int         bound   = 6;       // BMC / induction depth
  int         timeout = 0;       // per-query seconds (0 = none)
  bool        witness = true;    // print counterexample on Refuted
  std::string decompose = "auto"; // prove each cut/output diff as a separate UNSAT query
                                 // instead of one monolithic OR-miter. Same proof (an OR
                                 // is UNSAT iff every disjunct is), but each query is a
                                 // small focused cone, so the easy cuts discharge instantly
                                 // and only the genuinely-hard one is the bottleneck.
                                 //   auto  (default): run the per-cut sweep AND fall back
                                 //          to the monolithic solve on any cut that does not
                                 //          discharge — fast when it proves, definitive (+a
                                 //          witness on a real diff) otherwise. The intended
                                 //          everyday mode: same verdict as monolithic, but it
                                 //          turns a 60s monolithic miter into ~1s when the cuts
                                 //          are easy (a name-matched register-correspondence
                                 //          proof of two large front-end netlists is the case).
                                 //   true  : decompose ONLY — report "N/M cuts PROVEN" + the
                                 //          unresolved residue as Unknown, NEVER run the
                                 //          monolithic solve (the diagnostic mode to isolate the
                                 //          hard cone fast, e.g. a wide ALU/barrel-shift cone
                                 //          that needs SAT-sweeping). LEC_DECOMP_LOG=1 logs each
                                 //          cut's PROVEN/DIFF/unknown verdict.
                                 //   false : monolithic OR-miter only (one big checkSat).
                                 // on/1 == true, off/0 == false.
  bool        strict = false;    // treat an inconclusive UNKNOWN (no counterexample, the
                                 // solver merely could not complete the proof) as a hard
                                 // failure. Default false: REFUTED (a real counterexample)
                                 // hard-fails, but a witness-free UNKNOWN is a deferred
                                 // warning that exits cleanly (it disproves nothing).

  // Reset-phase separation for the BMC engine (lec.phase). The reset-asserted
  // and the free-running behaviors are best checked SEPARATELY:
  //   after_reset (default): hold reset asserted for `reset_cycles` cycles (NO
  //           miter — just to drive both into their reset state), then DEASSERT
  //           it and miter the following `bound` cycles — free-running equivalence.
  //   just_reset : hold every primary reset input ASSERTED on every cycle and
  //           miter each cycle — proves the two designs agree DURING reset.
  //   free_toreset : reset input ranges freely; the unrolling mixes both (the
  //           solver may still assert reset, so it explores odd reset patterns).
  //   full : run BOTH just_reset and after_reset and require equivalence in each.
  // A primary reset input is one that drives some flop's reset_pin directly;
  // its asserted level follows the flop's negreset attribute (active-low -> 0).
  std::string phase        = "after_reset";  // after_reset | just_reset | free_toreset | full
  int         reset_cycles = 2;               // after_reset phase: reset-hold prologue length

  // Optional explicit reset-input spec (lec.reset), comma-separated, each
  // `name` (polarity inferred from a _n/_ni suffix) or `name:lo` / `name:hi`.
  // When set it is AUTHORITATIVE (replaces auto-detection). Auto-detection
  // otherwise finds (a) every primary input that drives a flop reset_pin
  // (async resets) and (b) canonical reset-named inputs like rst / rst_ni
  // (synchronous resets, which the front-end folds into din rather than a
  // reset_pin so they have no structural marker).
  std::string reset;  // e.g. "rst_ni,clr_i:hi"

  // Explicit register/state CORRESPONDENCE (`lhd lec --match FILE` / lec.match):
  // {ref_state_name, impl_state_name} pairs that name the SAME state element when
  // the two front-ends chose unrelated names — e.g. a firtool stage register
  // `id_ex_ctrl.reg_ex_ctrl_aluop` vs a flat reimplementation `idex_aluop`. Each
  // side is canonicalized (canon_flop_name) and collapsed onto ONE shared cut
  // symbol, so the inductive miter shares their current state and compares their
  // next states directly. Names that already agree (after canon) need no entry;
  // the register-file flop-bank <-> Memory case is bridged structurally, no entry
  // needed there either.
  std::vector<std::pair<std::string, std::string>> match;

  // Proven-module black-box collapse (`lhd lec --collapse <def>` / lec.collapse):
  // module-def names the driver has ALREADY proven equivalent in isolation, which
  // are FORCED to the sound black-box path even when they could be flattened (a
  // combinational def supplied via --lib). Their outputs become shared UF(inputs)
  // symbols and their inputs become miter compare points, so the parent proof
  // stops re-solving the leaf's internals ("equal inputs => equal outputs", already
  // discharged when the leaf was proven). Matched case-sensitively (name policy).
  // The bottom-up hierarchical driver fills this with its per-round proven set.
  std::vector<std::string> collapse;

  // Structural def-diff reduction (`lec.semdiff`, M3): the semdiff matching
  // algorithm to run per module BEFORE the solver — `none` (default, always LEC)
  // or `structural` (pass/semdiff::structural_match). A def whose ref/impl are
  // structurally IDENTICAL (no unmatched node on either side) AND whose children
  // are all already proven needs NO solver call — it is dropped as proven. Only
  // the changed defs reach cvc5. The driver (lhd lec --set lec.hierarchical=true)
  // consumes this; prove_equal itself ignores it.
  std::string semdiff = "none";
};

// lec.decompose mode predicates (auto | true | false; on/1==true, off/0==false).
// `lec_decompose_try` = run the per-cut sweep; `lec_decompose_fallback` = on a cut
// that does not discharge, fall back to the monolithic solve for a definitive
// verdict + witness (only `auto`). `true` runs the sweep but never the monolithic
// solve (the diagnostic mode). See Lec_options::decompose.
inline bool lec_decompose_try(std::string_view m) {
  return m == "auto" || m == "true" || m == "on" || m == "1";
}
inline bool lec_decompose_fallback(std::string_view m) { return m == "auto"; }

// Normalize a lec.semdiff value to the canonical {none, structural}. `true`/`on`/
// `1` are accepted as ergonomic aliases for `structural` (the only algorithm),
// everything else (false/off/0/none/empty) maps to `none`. Applied at the CLI
// read sites so downstream (the validator + the hierarchical driver) only ever
// sees the canonical pair.
inline std::string lec_canon_semdiff(std::string_view v) {
  if (v == "structural" || v == "true" || v == "on" || v == "1") {
    return "structural";
  }
  return "none";
}

// The BMC engine unrolls `bound` (+ `reset_cycles`) SMT copies of the design;
// an absurd value (e.g. from `--set lec.bound=2000000000`) builds billions of
// cycles and hangs. Callers validate against this ceiling and reject out-of-range
// values with a clean diagnostic. Real bounds are tiny (tests use <= 32).
inline constexpr int kLecMaxUnroll = 100000;

// "" if bound/reset_cycles are in [0, kLecMaxUnroll], else a human message.
inline std::string lec_options_range_error(const Lec_options& o) {
  if (o.bound < 0 || o.bound > kLecMaxUnroll) {
    return "lec.bound out of range (0.." + std::to_string(kLecMaxUnroll) + "), got " + std::to_string(o.bound);
  }
  if (o.reset_cycles < 0 || o.reset_cycles > kLecMaxUnroll) {
    return "lec.reset_cycles out of range (0.." + std::to_string(kLecMaxUnroll) + "), got " + std::to_string(o.reset_cycles);
  }
  if (o.phase != "after_reset" && o.phase != "just_reset" && o.phase != "free_toreset" && o.phase != "full") {
    return "lec.phase unknown '" + o.phase + "' (after_reset | just_reset | free_toreset | full)";
  }
  if (o.engine != "bmc" && o.engine != "ind" && o.engine != "ic3" && o.engine != "auto") {
    return "lec.engine unknown '" + o.engine + "' (bmc | ind | ic3 | auto)";
  }
  if (o.semdiff != "none" && o.semdiff != "structural") {
    return "lec.semdiff unknown '" + o.semdiff + "' (none | structural)";
  }
  if (o.decompose != "auto" && o.decompose != "true" && o.decompose != "false" && o.decompose != "on"
      && o.decompose != "off" && o.decompose != "1" && o.decompose != "0") {
    return "lec.decompose unknown '" + o.decompose + "' (auto | true | false)";
  }
  return {};
}

// Prove the (combinational) outputs of `ref` and `impl` equal for all inputs,
// matching primary inputs by name. Returns Proven / Refuted(+witness) /
// Unknown(+detail).
//
// `sub_lib` (optional): name-hash Gid -> definition graph, used to flatten
// `Sub` instances inline during encoding (M5). Needed when a side is a
// hierarchical / standard-cell netlist (e.g. an ABC mapping whose cells resolve
// to gensim models); nullptr keeps the sound Sub -> Unknown.
Query_result prove_equal(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts = {},
                         const absl::flat_hash_map<hhds::Gid, hhds::Graph*>* sub_lib = nullptr);

// Parse a lec.match correspondence spec into {ref_name, impl_name} pairs. Pure (no
// file IO — a caller resolves any leading "@FILE" to its text first). Pairs are
// separated by commas / semicolons / newlines; the two names within a pair by "="
// or whitespace; blank lines and "#" comments are skipped.
std::vector<std::pair<std::string, std::string>> parse_match_pairs(std::string_view text);

}  // namespace livehd::lec
