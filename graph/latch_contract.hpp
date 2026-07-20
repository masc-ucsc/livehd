// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>

#include "hhds/graph.hpp"
#include "node_util.hpp"

// todo/livehd/2f-latch M3 — the shared COMMIT-CLASS analysis and the CONTRACT
// CHECK that guards its precondition. ONE analysis, three intended consumers
// (sim, LEC, timing), so it lives here rather than inside any one of them.
//
// WHY A CONTRACT AT ALL. LiveHD models a latch as a flop-with-enable that
// commits at the CLOSING edge of its transparent window. That is an
// ABSTRACTION WITH A PRECONDITION, not a semantics: it is sound exactly when
// nothing observes the timing INSIDE the transparent window. Designs that do
// observe it (time borrowing, self-timed loops) must be REJECTED with a named
// error — never silently approximated, because the approximation is a
// plausible-looking wrong answer rather than a crash.
namespace livehd::latch_contract {

// The commit class of a state element: WHICH net commits it, and on WHICH edge.
//   posedge flop        -> (clock, RISE)
//   negedge flop        -> (clock, FALL)
//   transparent-HIGH latch (enable active high) -> (enable, FALL)
//   transparent-LOW  latch (enable active low)  -> (enable, RISE)
//
// For a latch the "net" is the ENABLE net — a latch's gate IS its enable
// (user ruling 2026-07-20); there is no separate clock identity.
//
// `net` is the ROOT driver the enable/clock cone resolves to, walked back
// through the identity and boolean-shaping nodes tolg inserts, so two latches
// written `if clk` and `if !clk` resolve to the SAME net with OPPOSITE parity —
// which is what makes a master/slave pair recognizable as opposite phases
// rather than two unrelated elements.
struct Commit_class {
  hhds::Pin_class net;             // root net; invalid when unresolvable
  bool            rising = false;  // true = commits on the net's RISE
};

// Commit class of `n`, or nullopt when `n` is not a state element (or its
// controlling cone could not be resolved to a root).
[[nodiscard]] std::optional<Commit_class> commit_class_of(const hhds::Node_class& n);

// Enforce the no-time-borrowing precondition on every latch in `g`. Emits a
// directed `latch-contract` error naming the offending path for each violation
// and returns false if any fired.
//
// Rules enforced (see the .cpp for why rules 3 and 4 from the task page are
// deliberately NOT here):
//   A. a latch's ENABLE may not depend on its own Q      (self-timed gate)
//   B. a latch's D may not depend on its own Q           (transparent self-update)
//      — EXCEPT through its own hold mux, which tolg synthesizes for every
//        Pyrope/slang latch and which is not a real path
//   C. no combinational data path between two latches that are SIMULTANEOUSLY
//      transparent (same root net, same parity) — this is where time borrowing
//      lives. A master/slave pair sits on OPPOSITE parities and is accepted.
bool check(hhds::Graph* g);

}  // namespace livehd::latch_contract
