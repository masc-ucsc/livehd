//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>

#include "lnast.hpp"

// The LN pipe upass: insert the per-output pipeline flop of a
// `pipe` function tree (the LiveHD docs).
//
// Runs on a single post-SSA / post-runner function LNAST, AFTER the main
// upass walk and BEFORE the terminal consumers, so BOTH of them see the
// inserted reg: upass/tolg (-> LGraph -> Verilog) and the future
// lnast_to_slop (-> simulation).
//
// For every io_meta() output carrying a stages annotation (stages_min >= 1,
// stamped by prp2lnast from `pipe[N]` / `pipe[A..=B]` / bare `pipe`), append
// to the body:
//
//   declare(ref(___pipe_<out>), prim_type_none, const("reg"))
//                                  + trailing stages(min, max)   <- declared range, verbatim
//   store(ref(___pipe_<out>), ref(<out>))    // reg din  <- comb value of out
//   store(ref(<out>), ref(___pipe_<out>))    // out      <- reg q
//
// The inserted reg carries the DECLARED (min,max) range; the LG pass1
// checker later narrows it to (min-sigma, max-sigma). A pure-comb body has
// no reset/clock config to replicate, so the reg is the no-reset shape and
// tolg gives it the default (implicit) clock.
//
// Skips: outputs without a stages annotation (comb/mod trees are no-ops),
// and reg-as-output (the counter idiom) once regs can be outputs — today the
// Pyrope->LG path has no reg outputs, so every stamped output gets the flop.
class uPass_pipe {
public:
  // In-place: appends the declare/store statements to the tree's stmts.
  static void run(const std::shared_ptr<Lnast>& lnast);
};
