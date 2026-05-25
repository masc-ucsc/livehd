# TODO — LiveHD internal refactor

Pending work on LiveHD internals: CLI, upass infrastructure, source-map
machinery, LGraph cleanup, simulation/debug substrate, test reorg,
benchmarks, and HHDS-side optimizations.

Items use the same Group N letters as the master plan in [TODO.md](TODO.md).
Items in the same group can be done in parallel; all letters in group N must
complete before group N+1 starts. Group letters are shared across
[TODO_prp.md](TODO_prp.md), [TODO_verilog.md](TODO_verilog.md), and
[TODO_livehd.md](TODO_livehd.md), so cross-file dependencies stay visible.

## Group 1 — foundation

- **1b** New CLI: `setup/run/status/list/describe`, TOML config, JSONL
  results, error classes — `docs/contracts/future_cli.md`.
- **1f** Source-map indirection (LOC propagation: canonical map + per-cell
  index, alias multi-loc, partition-root fallback) — see "Source location
  (LOC) propagation strategy" below and `docs/contracts/sourcemap.md`.
- **1j** upass: prefer iterators over `std::vector` copies in pass plumbing
  — see "upass: prefer iterators over std::vector copies" below.
- **1c** `pass.lnastfmt` validator (arity, SSA uniqueness, `delay_assign`
  rules, deprecation warnings) — `docs/contracts/lnast_spec.md §5`.
- **1e** Drop `wrap`/`sat` as attributes; keep only the `wrap`/`sat`
  *statement* form, lowered to a new LNAST `wrap`/`sat` op — see
  "wrap/sat: statement-only via LNAST op" below.

## Group 2 — depends on Group 1

- **2a** Forest per-tree readiness (`local_done`) —
  `docs/contracts/hhds_migration.md §8`.
- **2h** Demand-driven incremental upass cache keyed on
  `(tree_body_hash, deps.interface_hash)` —
  `docs/contracts/architecture.md §4`.
- **2j** Hot-reload tier reporting in JSONL (`hot-debug` / `hot-approx` /
  `cold`) — `docs/contracts/architecture.md §8`.
- **2k** Verifier `pending` counter + `:type: top` semantics. Once the
  upass `pending_import` poison mechanism (see `docs/upass_redesign.md`)
  is functional, the verifier needs a third disposition: a cassert whose
  cond is still poisoned at end-of-walk is `pending` (not pass, not fail).
  Add a `verifier_pending:N` counter alongside `verifier_pass` /
  `verifier_fail`. For tests tagged `:type: top` (whole-program runs),
  pending casserts are rolled into the pass count — the contract is "at
  the top level all imports must resolve, so any surviving pending is a
  bug." For non-top tests (libraries with deferred imports), `pending`
  is a legitimate disposition. Complications worth working out:
    - A cassert that started pending on invocation N and then proves
      true on invocation N+1 should count once as pass, not once as
      pending then again as pass. Tally lifetime is per-program, not
      per-invocation.
    - `:type: top` aggregation crosses LNASTs in `var.lnasts` (including
      func_extract-spawned bodies) and the `verifier_include_funcs` knob
      already in `pass.upass`. The pending→pass roll-up should respect
      that scope.
    - Reporting: an unresolved pending at `:type: top` end-of-run needs
      a diagnostic naming the blocking `import`(s); requires keeping
      enough info on the `__pending_import` marker to identify the
      blocker (today the redesign uses a presence-only flag).
    - A cassert that started pending and later proves false is a hard
      error, same as any known-false cassert.

## Group 3 — depends on Group 2

- **3c** `Partition` descriptor as a tree-level HHDS attribute (`kind`,
  `latency_range`, ports, `ext`, `interface_hash`, `state_shape_hash`) —
  `docs/contracts/architecture.md §3`.
- **3d** New upass `lnast_to_slop` (parallel to `lnast_to_lgraph`) producing
  executable slop.
- **3f** Unified compile error/warning surface from `inou/prp`, upass,
  lgraph passes (+ tests for expected diagnostics).
- **3h** `Bitwidth_range` → `Const min/max` (drop `int+overflow`) once Dlop
  migration is stable — see "Bitwidth_range" section below.

## Group 4 — depends on Group 3

- **4c** Hot-probe injection at checkpoint replay (no recompile) —
  `docs/contracts/simulation.md`.
- **4d** What-if `poke(signal, value)` engine with safety semantics.
- **4e** Queryable indexed trace store (slice, transition, distribution
  queries).
- **4f** Invariant-detector codegen → emits `debug.prp` the agent can edit.
- **4g** Checkpoint format + reload validator (after slop codegen lands in
  [TODO_livehd.md](TODO_livehd.md) **3d**); partition-keyed via
  `state_shape_hash`, covers memories and registers for large simulation —
  `docs/contracts/architecture.md §9`.
## Group 5 — polish / final

- **5a** Rewrite `simlib/{uint,sint}.hpp` as part of the HLOP runtime;
  retire `firrtl-sig` attribution — see "simlib fixed-width int types"
  below.
- **5b** Attribute hot-path benchmark (flat_storage vs lhtree regression
  check) — `docs/contracts/hhds_migration.md §8`.
- **5c** Agent-legible globally-unique, optimization-stable signal names —
  `docs/contracts/simulation.md`.
- **5d** Test reorg per "Test Organization" below (`tests/` layout,
  `contracts/` split, `*_test.cpp` rules).
- **5e** IDAP perf-sweep pass: hold correctness flags steady, drive each
  optional pass on/off to measure marginal cost and find regressions —
  see "IDAP perf-sweep pass" below.
- **5f** Benchmark contracts (extend `benchmark/`): codegen throughput,
  simulation speed, agent-loop iteration latency — regression gates so the
  "seconds matter" promise of `docs/contracts/architecture.md §1` stays
  measurable.

## Test Organization

Tests are scattered across the codebase. Some subsystems place tests in
dedicated `tests/` directories (core, lgraph, lnast, inou/pyrope) while others
have test files alongside source code (upass, pass/label, pass/mockturtle,
main, inou/lefdef, inou/liveparse).

Rules to enforce:

- All main C++ tests must live in a `tests/` subdirectory under each component.

- A file should only use the `_test.cpp` suffix (e.g., `foo_test.cpp`) when
  there is a corresponding `foo.cpp` in the parent directory. Otherwise it is
  just a standalone test and should not carry the `_test` suffix.

- When the `_test` convention is used, it must be a suffix, never a prefix
  (`foo_test.cpp`, not `test_foo.cpp`).

- Create a separate `contracts/` directory (per component or top-level) for
  higher-level integration and API tests. Contracts test only public/high-level
  APIs and overall integration. Agents should not edit contract tests directly;
  they are maintained by hand to guarantee stable interface behavior.

- Treat compile-driver scripts as contracts, not ordinary tests. Move
  `yosys_compile.sh`, `slang_compile.sh`, and the other `*_compile.sh` scripts
  out of `tests/` directories and place them under a consistent `contracts/`
  structure.

## Source location (LOC) propagation strategy

Decide how LOC anchors (`file:line:col`, module, pipeline-stage) flow through
LNAST and LGraph so that `sigref("alu.flag_z")`, hot-probe injection
(`docs/contracts/simulation.md`), waveform-to-source mapping, and the
invariant-detector code generator can all map an internal signal back to
agent-editable Pyrope without best-effort heuristics.

### Hard commitments (not in scope for this decision)

- Every `Partition` (`comb` / `pipe[N]` / `mod`) keeps its declaration LOC.
- Every port (input/output of a partition) keeps its declaration LOC.
- Every register and memory declaration keeps its LOC.

These ride on the partition descriptor / port struct and are mandatory
through every pass.

### Open: how to handle everything else (internal wires, SSA temps, synthesized cells)

The naive "anchor on every value" approach is expensive and brittle in practice.
The favored direction is a **source-map indirection**: build one canonical
source map from the original Pyrope, and let internal names (SSA / temp /
synthesized) point at one or more locations in that map (so an alias
`a = b` can legitimately reference both `a`'s and `b`'s sites).

#### Issues to resolve

- Alias chains (`a = b; c = a`): does `c` map to `{a, b}`, `{a, b, c}`, or
  follow transitive closure? Closure could explode on deeply aliased SSA.
- Synthesized cells with no surface name (e.g., a `Mux` born from an
  `if/elif` lowering, a `Sum` materialized during slice/concat lowering):
  which source location is the "right" one — the condition, the join, the
  containing statement, or the partition root as a fallback?
- Optimization passes that drop a name entirely (constprop folding,
  cell-removal): the indirection handle disappears; what does `sigref` on
  that name return after optimization?
- Round-trip through external Verilog + LEC must not lose the source map.
  Emit `// src: file:line:col` comments at egress? A sidecar `.srcmap`
  file? Both?
- Pass discipline: with per-cell LOC, every pass must remember to set
  anchors and CI catches misses. With a source map, pass authors mostly
  don't think about LOC, but a pass that *renames* an SSA value must
  update its indirection — quiet failure mode if forgotten.

#### Pros of source-map indirection

- One canonical store; cells carry a small index, not 8+ bytes of anchor.
- Multiple-location attribution is natural (alias case).
- Source-derived SSA names (`foo_l42_b` per `lnast_spec.md §13`) already
  encode source by construction — the source map is a formal version of
  that scheme.
- Verilog egress emits source-map references rather than duplicating LOC
  per wire.
- Pass surface is smaller; LOC-propagation logic is centralized.

#### Cons of source-map indirection

- Indirection adds a lookup at every debug query (`sigref`, waveform
  click-through, LEC failure attribution). Hot-probe injection needs fast
  resolution.
- Synthesized cells still need *some* attribution rule — falling back to
  the partition root is honest but coarse.
- Tools outside LiveHD (waveform viewers, external LEC) must learn the
  source-map format or rely on the Verilog-egress sidecar.
- Optimization that removes the last reference to a name loses the link;
  recovering it requires the optimization to log a "removed: <name> ->
  <loc>" entry into the source map, which is a discipline issue too.

#### Alternatives considered

1. **Per-cell LOC (full anchor on every value).** Matches the aspirational
   wording in `docs/contracts/simulation.md`. Maximum fidelity but every
   pass becomes responsible for LOC propagation; quiet failure mode when a
   pass forgets; memory cost on every cell.
2. **Boundary-only LOC.** Only the hard-commitment list above carries LOC.
   Internal signals return "no source mapping" when probed. Simplest
   contract; weakest UX for `sigref` on internal wires.
3. **Source-map indirection with multi-location names** *(favored)*. One
   source store; names index in; 1-to-many supported for aliases.
   Synthesized cells without a surface name fall back to the enclosing
   partition's anchor.
4. **LOC-on-LNAST-only, drop at LGraph.** Let LGraph be LOC-free; resolve
   through `LGraph cell -> LNAST node -> LOC` indirection at debug time.
   Requires LNAST to be retained alongside LGraph for the lifetime of a
   debug session; works only if the upass cache keeps LNAST around.
5. **Hybrid (the working assumption).** Hard commitments above + source
   map for the rest, with partition-root fallback. Verilog egress emits
   `// src:` comments derived from the source map.

#### To decide before implementation

- Source-map storage: in-memory only, on-disk sidecar, or both? On-disk is
  needed for the `runs/<test>/vN/` workflow in `simulation.md`.
- Lookup API surface: what does `sigref(name).source()` return when the
  name has 0, 1, or N locations?
- Lifetime: when an upass invalidates a tree's cache, does the
  corresponding source-map slab get invalidated atomically?
- Egress comment format: pick one (`// src: file:line:col`) and commit, so
  external LEC tooling can parse it reliably.

## wrap/sat: statement-only via LNAST op

Today `wrap` / `sat` can appear both as attributes on a variable and as
prefix statements (`wrap a = x`, `sat a = x`). Collapse this to a single
form: the statement, lowered through a new dedicated LNAST op so that the
target variable's existing `max`/`min` (and bitwidth) drive the
wrap/saturate computation.

- Drop the attribute form (`a.[[wrap]] = ...` / `a.[[sat]] = ...`) from
  the Pyrope frontend and from `pass.upass` attribute handling.
- Add a new LNAST node kind `wrap` (and `sat`) with three children:
  `(__tmp, a, x)`. The op *reads* `a` because `a`'s declared
  `max`/`min`/`bits` is the input to the wrap/saturate range — not a
  control-flow read of `a`'s current value, but a type-info read.
- `wrap a = x` lowers to:
  ```
  wrap
    __tmp
    a
    x
  assign
    a
    __tmp
  ```
  (same shape for `sat`). The intermediate `__tmp` makes the dataflow
  explicit and keeps the existing `assign` lowering path unchanged.
- LGraph has no `wrap` / `sat` cell. The op gets converted at LNAST→LGraph
  time to an equivalent `get_mask` (for `wrap`) or `mux + get_mask` (for
  `sat`) using `a`'s `max`/`min` to compute the mask / saturation
  comparators.
- Update `pass.lnastfmt` (Group 1 **1c**) to validate the new node's
  arity and SSA discipline so the rule lands with validator coverage.
- Migrate any existing tests / `.prp` files that rely on the attribute
  form to the statement form (or delete them if redundant with the
  statement-form tests already in `inou/prp/tests/comptime/`).

## Bitwidth_range: replace int+overflow with Const min/max

`pass/bitwidth/bitwidth_range.{hpp,cpp}` currently stores `int min, max`
plus an `overflow` flag, then materializes a `Const` from those when the
value escapes the range representable in `int`. This was a workaround for
when the constant value type was expensive to allocate.

With `Const = Dlop` (inline storage for size <= 1, pool storage above), the
internal `int+overflow` representation is largely redundant — `Dlop`
already handles arbitrary widths efficiently. Refactor to store `Const
min; Const max;` directly and drop the `overflow` bookkeeping. Expected
benefits:

- Fewer materialize/round-trip conversions per query (`get_max`/`get_min`
  currently rebuilds a `Const` every call; storing it eliminates the
  reconstruction).
- Cleaner arithmetic in callers — no need to branch on `is_overflow()` or
  to special-case the int-fits path; the same `Dlop` ops cover both.
- One representation for the value, simpler invariants
  (`min <= max` becomes a single `Dlop` comparison).

Deferred until after the lconst-to-Dlop migration stabilizes so that the
churn lands in one focused commit.

## HHDS PinEntry/NodeEntry: native bits + sign storage

LiveHD currently stores per-pin `bits` (Bits_t / 23-bit) and `sign` (1 bit)
as HHDS attributes (`livehd::attrs::bits` / `livehd::attrs::sign`) — see
`docs/contracts/hhds_graph_migration_plan.md §G3`. Attributes are
flat_storage hashmaps; fine for correctness, but every read does a hash
lookup.

Native fields on `PinEntry` / `NodeEntry` would be one indexed load. The
proposed layout (keeps both classes at 32 bytes by dropping `ledge1` and
widening `Nid_bits` from 42 to 48, and port_id:23):

- **PinEntry** — `master_nid:48 + next_pin_id:48 + ledge0:48
  + port_id:23 + use_overflow:1 + sign:1 + bits:23 + sedges_/overflow_idx:64`
- **NodeEntry** — `type:16 + next_pin_id:48 + ledge0:48 + use_overflow:1
  + alive:1 + pad:6 + sign:1 + bits:23 + sedges_extra:48 + sedges_:64`

Touches every overflow/edge code path in `hhds/graph.cpp` (single-ledge
fast path, EdgeRange `kInlineMax` constants, NodeEntry edge accounting).
Worth its own focused multi-commit campaign in `../hhds`. Once landed,
LiveHD drops `livehd::attrs::bits` / `livehd::attrs::sign` and the
`mirror_set_pin_bits_hhds` / `mirror_set_pin_sign_hhds` helpers, reading
`bits()` / `is_unsign()` directly off the HHDS pin/node handle.

The `GraphIO::DeclaredIoPin` graph-level bits + sign extension lands
sooner (it's the immediate Sub_node-deletion unblocker, smaller scope,
no PinEntry/NodeEntry surgery required).

## upass: prefer iterators over std::vector copies

`upass/` code paths build and pass `std::vector<...>` results all over the
place (per-pass intermediate buffers, dependency lists, traversal results).
This is convenient but forces materialization and copies even when the
consumer only iterates once.

Refactor toward iterator/range-based APIs:

- Return ranges / lazy iterators instead of `std::vector` where the caller
  only needs a single forward pass.
- Where a concrete container is unavoidable, pass it by `const&` / move it
  through, and avoid the "build vector, copy vector, return vector"
  chains.
- Audit hot upass paths (scheduler, dependency walking, per-tree work
  queues) for accidental quadratic copies of vectors that just get fed
  into the next pass.

Performance optimization; may require non-trivial code rewrite in pass
plumbing. Tracked as **1j** so the cleanup lands before the upass cache
(**2h**) and partition descriptor (**3c**) work bakes vector-copy
assumptions into more APIs.

## IDAP perf-sweep pass

A correctness-preserving optimization/IDAP pass that toggles individual
upass stages on and off while keeping correctness invariants in place, so
we can attribute perf regressions to a specific pass and confirm each pass
earns its runtime cost. Concretely, we want to drive runs like:

```
time lg "inou.prp files:xx.prp |> pass.upass verifier:0 max_iters:1 ssa:0 bitwidth:1 constprop:0 attributes:0"
```

and have the pass framework:

- Enumerate the pass flag matrix (`verifier`, `ssa`, `bitwidth`,
  `constprop`, `attributes`, `max_iters`, …) and run the user-selected
  combinations.
- Diff results against a reference run so disabling a pass that *must* run
  for correctness fails loudly rather than silently miscompiling.
- Emit per-pass wall-clock + allocation deltas into the JSONL results
  stream (consumed by **5f** benchmark contracts).

While building this pass, audit the hot upass plumbing for the
performance anti-patterns we keep hitting. These are the same axes the
sweep should report on, so callers can see which axis regressed:

- **Repeated allocations.** No `new` / `make_unique` / `std::vector{}` in
  inner loops. Hoist scratch buffers out of per-node code paths; reuse
  per-pass arenas where lifetime allows. Look for `clear()` + refill
  patterns and convert them into stable, reusable buffers.
- **`std::vector` returns.** Replace "build vector, return vector" APIs
  with iterators / ranges / output iterators. Where a concrete container
  is unavoidable, take it by `&` from the caller so allocation lives at
  the call site, not inside the helper. This is the broader scope of
  **1j**; **5e** finishes the sweep across non-upass code.
- **Small-vector hotspots.** When a container is known to be tiny in the
  common case (≤8 elements: per-node fanin/fanout, per-cell port list,
  pass-local work stack), use `absl::InlinedVector<T, N>` (or
  `absl::FixedArray` when the size is known at the call) instead of
  `std::vector` to keep the storage on the stack.
- **String churn.** Hunt `std::string` temporaries on hot paths: prefer
  `std::string_view`, `absl::string_view`, or pre-interned symbol handles
  (we already have `mmap_lib::str` / interned IDs — use them). No
  `to_string()` in inner loops; no `+` concatenation for log lines that
  are then discarded; no `absl::StrCat` materialization when a
  `string_view` would do.
- **Tree / graph iteration without copying.** Iterate HHDS trees and
  LGraph in place via the existing iterator APIs. Do not snapshot
  `children`, `descendants`, `fanin`, or `fanout` into a `std::vector`
  just to walk them. If a pass must mutate during traversal, use the
  iterator invalidation-safe variants HHDS exposes rather than
  materializing a copy. Forbid the "collect into vector, then iterate"
  pattern in pass code; CI should grep for the usual offenders
  (`get_children`, `get_fanin_nodes`, …) returning vectors and replace
  them with range-returning siblings.

Deliverables:

- The sweep harness itself (driver + diff-vs-reference checker).
- A short report contract under `docs/contracts/` describing the flag
  matrix, what "correctness-preserving" means per flag, and the JSONL
  schema for the per-pass timing/allocation rows.
- One pass of cleanup across upass / lgraph passes hitting the
  allocation, vector-return, small-vector, string, and copy-to-iterate
  anti-patterns above — landed *with* the sweep so the regression gates
  have something to lock in.

Sequenced as **5e** so it lands after the upass cache (**2h**) and
partition descriptor (**3c**) have stabilized — otherwise we'd be
optimizing APIs that are still moving.

## simlib fixed-width int types

`simlib/uint.hpp` and `simlib/sint.hpp` (extracted from the `firrtl-sig`
upstream project, kept under `simlib/LICENSE.firrtl-sig`) are still used by
`core/tests/lconst_test.cpp` and the simlib examples. Once HLOP lands, these
types will be replaced/rewritten as part of the HLOP simulation runtime —
revisit the simlib int surface and the `firrtl-sig` attribution at that point.
