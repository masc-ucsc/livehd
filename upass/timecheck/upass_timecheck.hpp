//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <vector>

#include "lnast.hpp"

// Task 1u-B — LNAST timecheck discharge (todo/ #item-1u-B).
//
// A time check is discharged at the EARLIEST level that can decide it. This
// pass handles the LNAST-decidable subset: pure feedforward cycle derivation
// with no register classification and no graph topology —
//
//   * graph inputs are at cycle (0,0)
//   * a stage-decl result = operand cycle + the declared (min,max)
//   * a pipe/mod call result = aligned operand cycle + the callee's declared
//     interval; the stage[N] binding over a call result REPLACES the callee
//     interval with the picked N (validated against the declared range)
//   * a comb op's result = the equal-meet of its operands (two KNOWN operands
//     at different fixed cycles is the 06c-pipelining.md misalignment error)
//   * constants unify with any cycle; state regs / branch-written values /
//     tuples are UNKNOWN and their checks stay for the LG checker (1u-D)
//
// Every `timecheck(ref, N, N)` whose target cycle derives to a fixed value is
// verified (mismatch = located compile error) and marked DISCHARGED by a
// trailing const("checked") child — the 1u-C lowering skips marked checks, so
// the obligation is removed once checked. Mod interface declared cycles and
// the pipe sigma <= min honesty obligation are verified the same way when the
// full output chain is static (multiply_add discharges entirely here).
//
// Runs on each extracted pipe/mod tree AFTER the main runner walk and BEFORE
// uPass_pipe inserts the pipe output flop (the body sigma must not include
// it). Ungated by toln so the LSP pipeline gets the located diagnostics.
class uPass_timecheck {
public:
  using Registry = std::vector<std::shared_ptr<Lnast>>;
  static void run(const std::shared_ptr<Lnast>& lnast, const Registry& registry);
};
