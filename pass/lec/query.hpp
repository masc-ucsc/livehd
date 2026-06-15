// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>

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
};

// Discharge / engine knobs (filled from the lec.* set-options).
struct Lec_options {
  std::string engine  = "ind";   // bmc | ind (k-induction) | ic3
  std::string solver  = "cvc5";  // cvc5 | bitwuzla (not yet built)
  int         bound   = 20;      // BMC / induction depth
  int         timeout = 0;       // per-query seconds (0 = none)
  bool        witness = true;    // print counterexample on Refuted

  // Reset-phase separation for the BMC engine (lec.phase). The reset-asserted
  // and the free-running behaviors are best checked SEPARATELY:
  //   free  (default): reset input ranges freely; the unrolling mixes both.
  //   reset : hold every primary reset input ASSERTED on every cycle and miter
  //           each cycle — proves the two designs agree under reset.
  //   run   : hold reset asserted for `reset_cycles` cycles (NO miter — just to
  //           drive both into their reset state), then DEASSERT it and miter
  //           the following `bound` cycles — proves free-running equivalence.
  // A primary reset input is one that drives some flop's reset_pin directly;
  // its asserted level follows the flop's negreset attribute (active-low -> 0).
  std::string phase        = "free";  // free | reset | run
  int         reset_cycles = 2;       // run phase: reset-hold prologue length

  // Optional explicit reset-input spec (lec.reset), comma-separated, each
  // `name` (polarity inferred from a _n/_ni suffix) or `name:lo` / `name:hi`.
  // When set it is AUTHORITATIVE (replaces auto-detection). Auto-detection
  // otherwise finds (a) every primary input that drives a flop reset_pin
  // (async resets) and (b) canonical reset-named inputs like rst / rst_ni
  // (synchronous resets, which the front-end folds into din rather than a
  // reset_pin so they have no structural marker).
  std::string reset;  // e.g. "rst_ni,clr_i:hi"
};

// Prove the (combinational) outputs of `ref` and `impl` equal for all inputs,
// matching primary inputs by name. Returns Proven / Refuted(+witness) /
// Unknown(+detail).
Query_result prove_equal(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts = {});

}  // namespace livehd::lec
