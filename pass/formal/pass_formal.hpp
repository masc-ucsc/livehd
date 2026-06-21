// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

// pass.formal — single-design formal property checks on the cvc5 prover
// (pass/formal/prove.hpp). Distinct from pass.lec (which compares TWO designs):
// pass.formal reasons about ONE design. Runs in the compile pipeline after the
// recipe passes (cprop/bitwidth) and before cgen, gated by a none|fast|normal
// engine mode (mirrors LEC's IND vs BMC):
//   fast   = induction — the Prover cuts flops/memories as free inputs, so a
//            PROVEN holds for every reachable state and a COMBINATIONAL refutation
//            is exact; a stateful refutation (cone cut a flop) may rest on an
//            unreachable state, so fast defers it to a runtime check.
//   normal = BMC-intent — also trusts stateful refutations (today still the
//            induction over-approximation; the BMC unroll engine is a follow-up).
//   none   = skip the pass.
// Severity (testing policy "don't mask, but don't false-fail"): a PROVEN/passing
// property is always sound (induction over free register state is pessimistic),
// so only a "fail" (refutation) can be spurious. A refutation FAILS the build
// only when CONFIRMED real — at the committed boundary (the explicit --top, or a
// ROOT no Sub instantiates) and not resting on free register state fast cannot
// prove reachable. A refutation in an instantiated SUBMODULE ("not enough top":
// a parent constrains its inputs) or a fast/stateful one is a LOUD `DEFERRED`
// warning kept as a runtime check, never a build failure. `--set
// compile.formal.on_refute=warn` downgrades EVERY refutation to a warning (the
// escape hatch for a design checked without enough top context).
// `lhd compile` defaults to fast, none under -O0/--recipe O0; standalone
// `lhd pass formal` defaults to normal. Set with --set compile.formal.mode=...
// (or `lhd pass formal --set mode=...`). budget_k/cone_max are mode-independent
// (0 = built-in default). The first built-in obligation is Hotmux selector
// one-hotness; user assert/assert_always/assume materialized in tolg are handled
// here too. The add_label_optional registry below IS the pass.formal.* option
// set. See todo/livehd/2f-formal.
class Pass_formal : public Pass {
public:
  explicit Pass_formal(const Eprp_var& var);

  static void setup();
  static void work(Eprp_var& var);
};
