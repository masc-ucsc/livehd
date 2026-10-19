// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include "hhds/graph.hpp"

// pass/sweep -- whole-design clean-up that a hierarchy of separately compiled
// modules hides from the per-module passes. Everything here is opt-in
// (compile.flatten, compile.unknown_zero, compile.sweep): the default flow is
// unchanged.
//
//   flatten        splice every Sub instance that has a body into its root, so
//                  constants, unobserved outputs and shared state are visible
//                  to one cprop run instead of stopping at each port
//   refine_unknown rewrite every constant that has unknown (x) bits so the
//                  unknown bits read 0 -- a legal refinement of a don't-care,
//                  the same one `setundef -zero` makes -- so constant
//                  propagation can fold through what was an undriven output
//   sweep_state    constant and unobserved state bits (state.cpp)
namespace livehd::sweep {

struct Flatten_stats {
  size_t inlined = 0;  // instances spliced into a root
  size_t kept    = 0;  // instances left alone (no body, a rolled loop, refused)
};

// Inline every body-carrying Sub instance, recursively, into each of `roots`
// (in place). The callee definitions stay in the library untouched; a caller
// that no longer needs them drops them (see reachable_closure).
Flatten_stats flatten(const std::vector<std::shared_ptr<hhds::Graph>>& roots);

// The roots plus every definition still instantiated below them.
std::vector<std::shared_ptr<hhds::Graph>> reachable_closure(const std::vector<std::shared_ptr<hhds::Graph>>& roots);

// Replace each constant with unknown bits that drives a sink of `g` by the same
// constant with the unknown bits cleared. Returns the number of sinks rewired.
size_t refine_unknown(hhds::Graph& g);

struct State_options {
  bool unobserved = true;  // also remove state bits nothing can observe
  // A property marker instance (fproperty / lgassert / lgundef) observes the signals it
  // reads: dropping their cone would change an assertion that cgen emits or the
  // simulator evaluates. In a synthesis flow the markers have no hardware, so
  // `lhd synth` clears this and the logic only they read is dead.
  bool observe_markers = true;
};

struct State_stats {
  size_t const_bits     = 0;  // flop bits replaced by a constant
  size_t dead_bits      = 0;  // unobserved flop bits removed
  size_t dead_memories  = 0;  // memories none of whose read data is observed, removed
  size_t flops_rebuilt  = 0;  // flop nodes replaced by narrower ones (or constants)
  size_t fix_rounds     = 0;  // evaluation rounds of the constant-state fixpoint
  [[nodiscard]] bool changed() const { return const_bits != 0 || dead_bits != 0 || dead_memories != 0; }
};

// One round of the state sweep on `g` (see state.cpp). The caller folds the
// result with cprop + bitwidth and calls again until it reports no change.
State_stats sweep_state(hhds::Graph& g, const State_options& options = {});

}  // namespace livehd::sweep
