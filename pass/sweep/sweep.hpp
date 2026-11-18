// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
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

// Where each flop of a flattened root came from: the instance context tree
// (context 0 is the root itself; context c > 0 is the instance whose Sub node,
// by debug nid, sits in the definition of context `parent`, and whose callee is
// `def`) and, per flop of the flat root, its context and the debug nid of the
// flop in that context's definition. Filled by flatten() for a single root.
struct Flat_origin {
  struct Context {
    int32_t   parent = -1;
    hhds::Nid sub    = 0;
    hhds::Gid def    = 0;
  };
  std::vector<Context>                                                  contexts;
  absl::flat_hash_map<hhds::Class_index, std::pair<int32_t, hhds::Nid>> flops;
  absl::flat_hash_map<hhds::Class_index, std::pair<int32_t, hhds::Nid>> memories;  // the same, per Memory node
};

// Inline every body-carrying Sub instance, recursively, into each of `roots`
// (in place). The callee definitions stay in the library untouched; a caller
// that no longer needs them drops them (see reachable_closure). `origin`
// (only with one root) records the provenance above.
Flatten_stats flatten(const std::vector<std::shared_ptr<hhds::Graph>>& roots, Flat_origin* origin = nullptr);

// The roots plus every definition still instantiated below them.
std::vector<std::shared_ptr<hhds::Graph>> reachable_closure(const std::vector<std::shared_ptr<hhds::Graph>>& roots);

// Replace each constant with unknown bits that drives a sink of `g` by the same
// constant with the unknown bits cleared. Returns the number of sinks rewired.
size_t refine_unknown(hhds::Graph& g);

struct State_options {
  bool                                                      unobserved      = true;  // also remove state bits nothing can observe
  // A property marker instance (fproperty / lgassert / lgundef) observes the signals it
  // reads: dropping their cone would change an assertion that cgen emits or the
  // simulator evaluates. In a synthesis flow the markers have no hardware, so
  // `lhd synth` clears this and the logic only they read is dead.
  bool                                                      observe_markers = true;
  // Provenance hooks, called while a flop is rebuilt: `on_fold(old, bit, value)` for every bit cut out of
  // `old` (value 0/1: a constant bit that reads it; 2: an unobserved bit, which reads 0), and `on_run(old, lo, hi, run)` for
  // every narrower flop `run` that keeps bits [lo, hi) of `old` (its bit 0 is old bit lo).
  std::function<void(const hhds::Node_class&, int, int8_t)> on_fold;
  std::function<void(const hhds::Node_class&, int, int, const hhds::Node_class&)> on_run;
  // Called with each memory removed because none of its read data is observed (its readers read 0).
  std::function<void(const hhds::Node_class&)>                                    on_memory_dropped;
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

// Cut bits out of one flop of `g` the way the sweep does: bit i reads the constant `replace[i]` (0/1), and a bit
// with replace[i] < 0 stays state. What stays is named like synth's hierarchical state sweep names it (a kept
// prefix keeps the register's name, any other kept bit becomes a single-bit flop named after its source bit), so
// LEC pairs it bit by bit. Returns false when nothing changed (no bit to cut, a partial cut of a signed flop, a
// flop the sweep does not handle).
bool fold_flop_bits(hhds::Graph& g, const hhds::Node_class& flop, const std::vector<int8_t>& replace);

// Remove one memory of `g` the way the sweep removes an unobserved one: every reader of its read data reads 0.
// Returns false when `mem` is not a plain (non-image) memory.
bool drop_memory(hhds::Graph& g, const hhds::Node_class& mem);

}  // namespace livehd::sweep
