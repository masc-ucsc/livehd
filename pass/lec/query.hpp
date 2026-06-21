// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
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
  std::string engine  = "ind";   // bmc | ind (k-induction) | ic3
  std::string solver  = "cvc5";  // cvc5 | bitwuzla (not yet built)
  int         bound   = 6;       // BMC / induction depth
  int         timeout = 0;       // per-query seconds (0 = none)
  bool        witness = true;    // print counterexample on Refuted

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
};

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
