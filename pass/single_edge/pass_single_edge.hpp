// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <vector>

#include "hhds/graph.hpp"
#include "pass.hpp"

// todo/livehd/2f-latch M8 — EDGE NORMALIZATION.
//
// One LGraph->LGraph transformation that REMOVES latches and negedge state, so
// the cvc5 encoder (and anything else that assumes "one step = one commit")
// never has to model sub-cycle time. It is the plan of record for the two holes
// M4 left: the encoder refuses a `Latch` outright, and it cannot tell a negedge
// flop from a posedge one on the same clock.
//
// CONDITIONAL BY DESIGN. The pass does not run at all unless the design has a
// latch, a negedge flop, or more than one clock net (livehd::latch_contract::
// needs_single_edge). A plain posedge single-clock design never enters this
// code path, is never re-emitted, and therefore cannot change a verdict — that
// is the whole blast-radius argument, and it is why this can land without
// re-baselining the corpus.
//
// SCOPE (user ruling 2026-07-24): VERIFICATION AND SIMULATION ONLY, never on
// the synthesis path — slot enables and a phase divider cost QoR, and the
// netlist handed to ABC must still contain a real `always_latch`. But it must
// stand as a genuinely semantics-preserving transformation, not a formal-only
// hack: same bar as any other pass.
//
// WHAT IT DOES. Every state element becomes a plain `Flop` on a single posedge
// clock. When the design's commit classes all land at the same instant the
// transformation is just a retype (P = 1, no divider, no solver change). When
// they do not, the original timing is carried by a synthesized phase divider
// plus per-flop slot enables, so an INTRA-step ordering problem (a negedge flop
// reads a posedge Q committed earlier in the SAME step) becomes an ordinary
// CROSS-step one the existing Flop encoding already handles.
//
// FAIL CLOSED. A partial lowering reproduces the exact step-1 counterexample
// M4 died on, with no diagnostic anywhere. So anything not understood — a
// stateful `Sub`, a second clock domain with no known ratio, a yosys raw-D/EN
// latch, a coincident-edge latch/flop pair — DECLINES the whole design (the
// caller then keeps the old, honestly-refusing behavior) or errors, never
// half-transforms.
namespace livehd::single_edge {

// Outcome of one normalize() call. `applied` false + `error` false is the
// common, healthy case: the design had nothing to normalize.
struct Result {
  bool applied = false;  // the graph was rewritten
  bool error   = false;  // a hard, diagnosed refusal (the caller must not proceed)
  int  slots   = 1;      // P: sub-steps per source clock period after the rewrite
  int  latches_retyped = 0;
  int  flops_slotted   = 0;
  int  icg_folded      = 0;  // gated clocks rewritten into a flop enable
  // Identity of the REFERENCE clock the slots are expressed against (a graph
  // input's name when there is one; "" when the design has no clock at all).
  // A miter whose two sides normalize against DIFFERENT reference clocks is
  // being compared in two different time bases -- the same class of error as a
  // P mismatch, and not otherwise visible, because the encoder models a single
  // clock as "commits every step" and so cannot tell one clock from another.
  std::string ref_clock;
  std::string reason;  // why it was skipped / refused / what it did (for recipe_steps)
};

struct Options {
  // Compute the plan (and therefore `slots`) WITHOUT touching the graph. How a
  // miter agrees on ONE time base: probe both sides, take the max P, then apply
  // it to both.
  bool dry_run = false;
  // Lower into this many slots even when the design's own commit classes need
  // fewer — including when its trigger is quiet. A side with nothing to lower
  // still needs the divider so it counts time the same way as its counterpart;
  // without this an all-posedge ref (P=1) against a negedge impl (P=2) is
  // compared in two different time bases, which is a guaranteed false verdict.
  int force_slots = 0;
  // Suppress the refusal diagnostics (probe callers report their own).
  bool quiet = false;
};

// Normalize `g` in place. `defs` (may be empty) are the resolution-library
// bodies reachable from `g`: a `Latch` inside one of them is NOT covered by
// fixing the top graph (pass/lec/encode.cpp screens defs separately and sends a
// stateful one to the blackbox path), so they are scanned too and a stateful
// def that needs normalization declines the run.
//
// SKIP vs REFUSE. The pass only OWES a lowering for what the downstream engines
// get WRONG: a `Latch` (the encoder refuses the cell outright) and a negedge
// flop (the encoder is blind to the edge). Anything else it merely could have
// handled — most importantly a second clock domain with no known ratio — is
// SKIPPED, not refused, so pass/lec's detected-edge branch and sim's M6 model
// keep working exactly as they did. Refusing there would degrade a correct
// REFUTED into an unsupported exit, which is a regression, not fail-closed.
Result normalize(hhds::Graph* g, const std::vector<hhds::Graph*>& defs = {}, const Options& opts = {});

// Trigger only: would normalize() rewrite this design? Cheap; no mutation.
// The miter-wide rule (task page, condition 1) is that if EITHER side of a
// comparison needs normalization, BOTH are normalized — otherwise the two are
// compared in different time bases, a guaranteed false REFUTED and exactly the
// shape of the prp-v2prp-latch_* corpus.
[[nodiscard]] bool needed(hhds::Graph* g, const std::vector<hhds::Graph*>& defs = {});

}  // namespace livehd::single_edge

// pass.single_edge — the Eprp surface of the above (2f-latch M8 step 1f). The
// artifact the validation harness consumes: `lhd pass single_edge --top M
// lg:IN --emit-dir lg:OUT`.
class Pass_single_edge : public Pass {
public:
  explicit Pass_single_edge(const Eprp_var& var);

  static void setup();
  static void work(Eprp_var& var);
};
