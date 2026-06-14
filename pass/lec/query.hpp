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
};

// Prove the (combinational) outputs of `ref` and `impl` equal for all inputs,
// matching primary inputs by name. Returns Proven / Refuted(+witness) /
// Unknown(+detail).
Query_result prove_equal(hhds::Graph* ref, hhds::Graph* impl, const Lec_options& opts = {});

}  // namespace livehd::lec
