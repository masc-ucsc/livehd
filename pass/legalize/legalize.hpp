// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// pass.legalize -- the ONE sanctioned structural transform between lnast.tolg /
// pass.cprop and everything downstream.
//
// WHAT RUNS TODAY (legalize_design):
//
//   1. ACYCLIC REPAIR (acyclic.hpp make_acyclic), the one place that handles
//      combinational loops: afterwards every def is acyclic at ARC level, so
//      no consumer schedules around a loop. Instances on an arc-level cycle
//      and state-free instances on an atomic-level one are inlined, a rolled
//      loop on an atomic-only ring is split by ring/free carries (never
//      unrolled), packed slices left in a body are rewired, and a loop left is
//      a diag error. It edits the shared library def in place -- an instance
//      it dissolves is gone from the hierarchy every consumer sees.
//
//   2. LOOP SPLIT (split_loops below), on the repaired graph.
//
//   3. FREEZE -- separate: the compile kernel runs legalize_design early (after
//      the first cprop+bitwidth) without freezing, and seal_design() records
//      every def's structure after the last pass, so a later pass that
//      reshapes it can be named (see FROZEN GRAPHS).
//
// REBUILD (rebuild_def / clone_io_decls) is a forward-walk reconstruction into
// a fresh, dense body with the input untouched. It is exercised by the tests
// and available to callers, but legalize_design does NOT run it yet: the
// pipeline's graphs are edited in place by 1 and 2 and then frozen.
//
// NOT AN OPTIMIZER. Nothing here folds constants, drops dead logic or rewrites
// for QoR -- that is cprop's job, and legalize runs after it. The rebuild is
// structurally the same design: `semdiff::structural_identical(src, rebuilt)`
// holds, which is exactly how the tests check it.
//
// RUNS UNCONDITIONALLY, not as a recipe step. `recipe:O0` has no steps at all,
// so a recipe-gated legalize would skip O0.

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "hhds/graph.hpp"

namespace livehd::legalize {

// Rebuild ONE def's body into `dst_gio` (which must already carry the same IO
// declarations, see clone_io_decls). A nested Sub re-binds to the destination
// library's def of the same name when it has one; otherwise it keeps the
// source's own GraphIO (a body-less black box shared across libraries).
// Returns nullptr after a diagnostic.
[[nodiscard]] std::shared_ptr<hhds::Graph> rebuild_def(hhds::Graph* src, const std::shared_ptr<hhds::GraphIO>& dst_gio);

// Copy `src`'s IO declarations (name, port id, loop_break, bits, sign) onto a
// freshly created GraphIO of the same name in `dst_lib`.
[[nodiscard]] std::shared_ptr<hhds::GraphIO> clone_io_decls(hhds::Graph* src, hhds::GraphLibrary& dst_lib);

// ---------------------------------------------------------------------------
// FROZEN GRAPHS
//
// The invariant legalize exists to make enforceable: after it runs, NO pass may
// structurally change the graph. Attribute writes stay legal -- that is how a
// pass records what it learned (color, place, proven, match).
//
// hhds's mutators (`create_node`, `del_node`, `connect_driver`, `del_edge`) are
// called directly on hhds handles, so a livehd-side wrapper cannot intercept
// them. Rather than wait on a graph-library change, freeze RECORDS what the
// structure was -- `semdiff::canonical_digest`, which folds op, width, IO and
// the full edge relation and deliberately excludes the attributes a pass is
// allowed to write -- and `verify_frozen` recomputes it. Any node added,
// deleted, retyped, re-widened or re-wired moves the digest; a color or a
// `proven` stamp does not. (A `bits` write does move it: width is structure.)
//
// So this is a CHECK, not a lock: it cannot prevent the write, it names the pass
// that made it. The digest costs a full walk per graph, so the kernel runs
// freeze+verify only when `compile.verify_frozen` is on (debug builds by
// default).
//
// The ledger is keyed by the graph OBJECT, not its gid: hhds gids are name
// hashes, identical across libraries, so two `top`s in one process (both sides
// of `lhd lec`, or a cache overlay swapping a body under the same name) would
// share one slot. The shared_ptr overload also records ownership, so an entry
// whose graph has since died is unclaimed rather than matched to whatever now
// lives at that address.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// ONE DRIVER PER SINK PIN
//
// The hhds invariant every consumer now relies on: a sink pin carries EXACTLY
// one driver, so `get_driver_pin()` is unconditionally valid and a commutative
// cell's operands are a multiset of PINS rather than a fan-in on one pin
// (graph/cell.hpp's ONE DRIVER PER SINK PIN block and its bank convention).
//
// It is checked HERE rather than with `I(...)`, deliberately: `I()` expands to
// nothing under -DNDEBUG, which is every `-c opt` build and therefore every
// build anyone ships or benchmarks. A cross-layer invariant guarded only by an
// `I()` is guarded in exactly the builds nobody runs.
//
// A sink pin may legally be UNDRIVEN or DOUBLY-driven while a mutation is in
// flight (cprop reconnects a consumer to a folded constant before deleting the
// node that used to drive it). The check therefore runs on a SETTLED design, as
// part of legalize_design, never inside a rewrite.
// ---------------------------------------------------------------------------

// Report every sink pin of `g` that carries more than one driver. Returns the
// number found (0 == legal) and emits one diagnostic per offender, naming the
// node, the sink pid and the drivers. `who` names the caller in the diagnostic.
[[nodiscard]] int verify_single_driver_sinks(hhds::Graph* g, std::string_view who);

// The same over a whole design. Called by legalize_design; exposed so a pass
// under suspicion can re-run it at its own boundary.
[[nodiscard]] int verify_design_single_driver_sinks(const std::vector<std::shared_ptr<hhds::Graph>>& graphs,
                                                    std::string_view                                 who);

// Record `g`'s structure. Call once, on the rebuild's output.
void freeze(hhds::Graph* g);
void freeze(const std::shared_ptr<hhds::Graph>& g);

// True iff `g`'s structure still matches what freeze() recorded. Returns true
// for a graph that was never frozen (nothing to check) and emits a diagnostic
// naming `who` when the structure moved.
[[nodiscard]] bool verify_frozen(hhds::Graph* g, std::string_view who);

// Number of graphs currently frozen (test/diagnostic use).
[[nodiscard]] size_t frozen_count();

// ---------------------------------------------------------------------------
// LOOP SPLIT
//
// Rewrite one compact loop into TWO loops over the same domain: the PARALLEL
// half (carries whose lanes are independent -- a per-lane slice write) and the
// INDUCTION half (genuine recurrences). Both keep the original
// `{first, step, count}`, so nothing about the iteration space changes.
//
// WHY, given no consumer requires it: the two halves want opposite treatment.
// Synthesis wants a BOUNDARY around the parallel half (map the body once,
// instantiate N times, and a count change re-maps nothing) and NO boundary
// around the recurrence (ABC has to see the whole chain to turn a ripple into a
// prefix structure). LEC wants a symbolic-index proof for one and ordinal
// induction for the other. Neither can act on that while both live in one body.
//
// v1 IS DELIBERATELY NARROW -- every refusal leaves the loop whole, which is
// always correct, just not yet optimized:
//   * a body node feeding BOTH a parallel and an induction carry would have to
//     be duplicated into both halves;
//   * a half whose cone READS the other half's carry-in port would read a seed
//     instead of the running value (each half self-wires only its own carries);
//   * a callee output that is not a carry (a per-lane "final", the descriptor's
//     next_active_output) has no half to live in.
// `sum += f(x[i])` is NOT affected by the first rule: `f` is inside the
// recurrence's own backward cone, so the induction half is self-contained.
//
// The classification driving it lives in graph/loop_split.hpp and is
// conservative: anything it cannot prove parallel stays in the induction half.
//
// Half bodies are named `<body>__par` / `<body>__ind` -- derived and STABLE,
// because abc_incr keys its region cache on the module name. They are reused
// across the loops of one run through `Split_state::halves`; a same-named def
// already in the library is a STALE half from an earlier compile (the library
// is reloaded verbatim by the `lg:` / `ln:` / slang paths) and is replaced,
// never reused -- it may describe a body the user has since edited.
// ---------------------------------------------------------------------------

struct Split_state {
  // Callee bodies whose loop was split. Splitting deleted the only instance a
  // body had in its host, so it may now be unreferenced; legalize_design
  // sweeps those, a direct caller owns that decision.
  std::vector<hhds::Gid>                                                                                split_bodies;
  // Half bodies created (the caller must surface them to whatever enumerates
  // the design -- var.graphs, the cache, the emits -- or a host instantiates
  // modules nobody writes).
  std::vector<std::shared_ptr<hhds::Graph>>                                                             added;
  // Stale same-named defs dropped from the library while creating a half.
  std::vector<std::shared_ptr<hhds::Graph>>                                                             removed;
  // (body gid, first suffix) -> its two halves, one pair per split kind per run.
  absl::flat_hash_map<std::pair<hhds::Gid, std::string>, std::pair<std::shared_ptr<hhds::Graph>, std::shared_ptr<hhds::Graph>>>
      halves;
};

// Split the rolled loop instance `sub` (in `host`) so a combinational ring
// through it no longer closes: carries whose cone reads any port in
// `ring_inputs` go to one rolled loop (`<body>__ring`), the rest to another
// (`<body>__free`), both over the original domain -- never unrolled. false
// when no such partition exists (every carry reads the ring) or the halves
// would share logic.
bool split_loop_by_ring(hhds::Graph* host, hhds::GraphLibrary& lib, const hhds::Node_class& sub,
                        const absl::flat_hash_set<hhds::Port_id>& ring_inputs, Split_state* state);

// Split every eligible compact loop of `host`. Returns the number split. With
// `state == nullptr` a local state is used (halves are still deduplicated
// within this call).
[[nodiscard]] int split_loops(hhds::Graph* host, hhds::GraphLibrary& lib, Split_state* state = nullptr);

struct Legalize_result {
  std::vector<std::shared_ptr<hhds::Graph>> added;    // new defs (split halves) the design now contains
  std::vector<std::shared_ptr<hhds::Graph>> removed;  // defs deleted from their library (orphaned bodies, stale halves)
};

// Run legalize over a whole design: the acyclic repair, the loop split and the
// one-driver check, then FREEZE every def (when `freeze_graphs`). The compile
// kernel runs it EARLY -- right after the first cprop+bitwidth, ahead of
// enableopt, satopt and pass.formal, so every later pass (formal's hierarchy
// views included) already sees an acyclic design -- with `freeze_graphs`
// false, and seals the design with seal_design() after the last pass.
//
// The caller MUST apply the result to its own view of the design: drop
// `removed` (their bodies are gone; touching them asserts) and add `added`.
//
// Semantically a no-op: the split is proven equivalent to the unsplit loop
// (pass/legalize/tests), and nothing else here changes behavior.
Legalize_result legalize_design(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, bool freeze_graphs = true);

// The end of the compile pipeline: re-check one driver per sink pin on the
// settled design (the passes after legalize may rewire), and FREEZE every def
// (when `freeze_graphs`) -- from here on a pass may record attributes but must
// not change structure. With `check_acyclic` it also re-scans for an
// arc-level combinational cycle a later pass re-created (cprop / enableopt /
// satopt must not); one found is reported as an internal error. Returns the
// number of such cycles.
int seal_design(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, bool freeze_graphs, bool check_acyclic);

// Check every frozen graph in `graphs` against what legalize recorded. Returns
// the number that moved. A graph legalize never froze is not claimed and is
// skipped, so this is safe to call on any design.
[[nodiscard]] int verify_design_frozen(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view who);

}  // namespace livehd::legalize
