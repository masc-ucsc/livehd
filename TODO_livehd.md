# TODO — LiveHD internal refactor

Pending work on LiveHD internals: CLI, upass infrastructure, source-map
machinery, LGraph cleanup, simulation/debug substrate, test reorg,
benchmarks, and HHDS-side optimizations.

Items use the same Group N letters as the master plan in [TODO.md](TODO.md).
Items in the same group can be done in parallel; all letters in group N must
complete before group N+1 starts. Group letters are shared across
[TODO_prp.md](TODO_prp.md), [TODO_verilog.md](TODO_verilog.md),
[TODO_livehd.md](TODO_livehd.md), and [TODO_hhds.md](TODO_hhds.md), so
cross-file dependencies stay visible.

## Group 1 — foundation

- **1s** Sanitizer pass — chase a nondeterministic memory bug in the
  comptime/string path. **Goal 1.** **Run on Linux (macOS can't do MSan).**
  - **Symptom.** `string_hello` (and likely other comptime/string tests)
    crash *intermittently* — `Bus error (SIGBUS)` or `Killed (SIGKILL/137)`,
    sometimes `exit 1` after a full dump — at a **few-percent rate with fixed
    input**. Fixed input + nondeterministic crash ⇒ an **uninitialized-memory
    read** (value depends on heap garbage), not a UAF.
  - **It is NOT in committed HEAD.** Pure `HEAD` ran **0/100**. The bug lives in
    the current **working-tree WIP**: the `inou/prp/prp2lnast.cpp` `declare`/
    `store` frontend work + the error-handling changes in `main/main.cpp`
    (catch→`has_errors()`→exit 1) and `parser/elab_scanner.cpp` (dropped the
    dbg-only `I(false)` abort). Crashes ~2–6% even with those two error-handling
    files reverted, so the root is most likely in the `prp2lnast` string /
    interpolation lowering (`"Hello a is {a}"`) — an uninitialized field on a
    node/span/string emitted there.
  - **Amplifier (why this blocks cleanup).** Removing the dead `changed` flag
    (`uPass::mark_changed`/`has_changed`; no production reader — see commit that
    dropped `max_iters`) is **blocked on this**. Editing `store_trivial`
    (`upass/constprop/upass_constprop.hpp`, a hot inlined helper) is provably
    behavior-identical (`Symbol_table::set` always returns `true`) yet perturbs
    codegen/heap layout enough to push the crash rate ~5% → ~20%. So the flag
    removal was reverted; redo it once this is fixed.
  - **ASan does NOT catch it** (built `--config=asan`, ran clean — its redzones
    change layout and it doesn't detect uninitialized reads). Needs **MSan**.
  - **Reproduce.**
    ```
    bazel test //inou/prp:prp-string_hello --runs_per_test=60   # expect a few FAILs
    # or, against a built binary, loop the comptime pipeline:
    for i in $(seq 1 60); do rm -rf /tmp/shh; mkdir -p /tmp/shh; \
      HOME=/tmp/shh bazel-bin/main/lgshell \
      "inou.prp files:$PWD/inou/prp/tests/comptime/string_hello.prp |> pass.lnastfmt \
       |> pass.upass constprop:1 verifier_pass:1 verifier_fail:0 |> pass.lnastfmt |> lnast.dump" \
      >/dev/null 2>&1 || echo "FAIL $i"; done
    ```
  - **MSan setup.** Add an `msan` config to `.bazelrc` mirroring the existing
    `asan` block (`-fsanitize=memory -fsanitize-memory-track-origins=2
    -fno-omit-frame-pointer`); build `--config=msan //main:lgshell` and run the
    loop above. Origins tracking should point straight at the uninitialized
    field. Relates to the `core/diag` diagnostics foundation (landed; the
    error-handling path is half the WIP) and the
    `prp2lnast` frontend.

- **1d** Demand-driven (lazy) emit + dead-code elimination for upass. **Goal 1.**
  **★ Design rationale + literature references live in the header comment block
  at the top of [`upass/runner/upass_runner.cpp`](upass/runner/upass_runner.cpp)
  — read that first; this entry is the implementation plan.** Replace today's
  *emit-everything-then-mark-sweep* DCE with *demand-first* emission that
  materializes each pure value def at its use site (folding to a `const` when
  possible, else copying the def subtree from the source tree), drops undemanded
  defs for free, and sinks defs near their uses. In compiler terms this is late
  scheduling of a sea-of-nodes / partial dead-code elimination (refs in the
  comment block: Click GCM, Knoop-Rüthing-Steffen PDE/LCM, SSA-DCE, SCCP, VDG).

  **Motivating symptom.** A comptime bit-range write leaks an unresolved `range`
  into `lnast.dump`:
  ```
  inou.prp files:inou/prp/tests/comptime/bitset.prp |> pass.upass |> lnast.dump
  ...
  ├── range
  │   ├── ref '___22'      # from x#[5..+5]  ⇒ bits 5..=9
  │   ├── const '5'
  │   └── const '9'
  ```
  `range` (and `declare`) are not in `dce_is_def_producing`, so an unused comptime
  one is never swept; more fundamentally we emit eagerly and sweep afterward
  instead of emitting on demand.

  **Current state (what to evolve).**
  - Eager emit in constprop/runner (`emit_op_with_fold*` in `upass_runner.cpp`).
  - Scalar const-at-use fold already exists: `fold_ref` + `emit_ref_or_folded`
    (`upass_constprop.cpp`) — inline `const zz` for a `ref xx` when `xx` is a known
    trivial scalar. **This is the model to generalize.**
  - Post-walk mark-sweep DCE: `dead_code_eliminate_staging` (`upass_runner.cpp`),
    runs only when constprop is active; touches **temps only** (`Lnast::is_tmp`,
    `___` prefix); `dce_is_def_producing` lists the eligible ops — `range`/`declare`
    are **absent**; `attr_set type='reg'|'mut'` is keepalive
    (`dce_is_keepalive_attr_set`).
  - IR is **SSA** at constprop time (`upass/ssa` renames `mut` writes apart, e.g.
    `x` / `x___ssa_1`) but **straight-line only** — branches are copied verbatim,
    no φ; the lowerer's Mux merges later.

  **Target model — demand-driven liveness from roots.**
  - **Roots (pinned; always emitted, order preserved):** function IO (declared at
    the function boundary via the `io` node — *variables cannot declare outputs*,
    so the IO set is knowable and is the only named-var exception), side-effecting
    statements (`cassert` always; `cputs`/`func_call` unless their dst const-folds),
    and state elements (`attr_set type='reg'|'mut'`).
  - **Floating (deferred):** every pure value def, named or temp. Emitted only when
    transitively demanded by a root. Undemanded ⇒ never emitted (= free DCE).
  - **Materialize at use:** known-const scalar ⇒ inline the `const` (existing
    `fold_ref`); otherwise copy the def subtree from the source ("shadow") tree via
    `emit_subtree_verbatim`. `range` / `declare` / all-const-field statements fall
    out of this one rule uniformly.
  - **Placement:** at the **dominator-tree LCA of all uses** (then sink as deep/late
    as legal), NOT the textual first use — correct when a value is consumed in
    multiple branches. Within a straight-line SSA region, first-use == LCA.

  **Safety property.** Total reorder of pure defs is sound *here* because our side
  effects are clean: no memory references / aliasing, so every read-after-write is
  visible through SSA names. CAUTION (future arrays): an array index reintroduces
  may-alias ordering — `a[i]=` vs `=a[j]` cannot be reordered without proving
  `i != j`; treat aliasing array ops as pinned/ordered when arrays land (D4).

  **Data structures.**
  - Pending map: output-name → handle (source nid) into the shadow tree, stored
    **in the scoped `Symbol_table` entry** so `Symbol_table::leave_scope` recycles
    dead pendings automatically — no parallel map lifecycle, no unbounded growth.
  - Emitted mark: once materialized, record the emitted staging nid so later refs
    dedupe (a shared subexpr emits once — preserves CSE, avoids duplicate work).
  - Transitive demand: resolving a `ref` emits its def first, post-order over
    dependency edges; bounded by #pending-in-scope (the "chain of inserts" — bounded
    but must be deduped via the emitted-mark and LCA-placed).

  **Phases (land in order).**
  - **D1 — interim, proven machinery (do first):** add `range` to
    `dce_is_def_producing` (pure, no side channel) so unused comptime ranges are
    swept by the existing mark-sweep; give `declare` a keepalive guard mirroring
    `attr_set reg/mut` (declare is a *type carrier* consumed by bitwidth/lnastfmt —
    never blanket-drop). Kills the bitset symptom with no architecture change.
  - **D2 — prototype demand-driven on the comptime subset:** statements whose
    fields are all const / const-type (`range`, const `declare`, scalar const
    assign), scoped to temps within one `stmts` block. Validate pending-in-symtab,
    transitive resolution, and shadow-tree copy. Eager emit + post-walk DCE stay in
    place for everything else (both models coexist; the subset is just not emitted
    eagerly).
  - **D3 — generalize:** all pure SSA-temp defs float; add LCA placement for
    cross-branch uses; extend to droppable named vars (non-IO, non-state) using the
    `io`-node root set; then **retire the separate post-walk rebuild** by folding
    DCE into demand-driven emission.
  - **D4 — arrays (future-gated):** classify aliasing array-index ops as
    pinned/ordered; revisit the total-reorder assumption above.

  **Files.** `upass/runner/upass_runner.cpp` (DCE section + emit path),
  `upass/constprop/upass_constprop.{cpp,hpp}` (`fold_ref`, `emit_ref_or_folded`,
  `process_range`, `process_declare`, `classify_statement`), and the scoped
  `Symbol_table` (attach a pending source-nid per scope entry).

  **Validation.** `bitset.prp`: no `range ___22` in `lnast.dump`, results still
  correct (verifier 4/0). Full upass + comptime suites green, net-neutral. New
  tests: unused comptime range dropped; used comptime range folds to a mask const;
  `declare` with no readers kept iff reg/mut/IO else dropped; multi-branch shared
  temp emitted once at the dominator LCA and dominating both uses.

  **Relationships.** Generalizes constprop's scalar `fold_ref`. The `declare`
  handling must match the landed declare/store node shape, and the bit-range LHS
  producer lowering (landed — the comptime bit-ranges it emits, e.g. `x#[5..+5]`,
  are the motivating case). Must keep the landed read-only bitwidth finalization
  pass (runs after SSA) able to see `declare` type info — hence D1's declare
  keepalive.

## Group 1-complex — foundation, larger scope

Tasks that are independent of other Group 1/2 work but are large enough
that they warrant their own bucket. Can be done in parallel with regular
Group 1 entries; downstream Groups treat them as Group 1 dependencies.

- **1y** New CLI: `setup/run/status/list/describe`, TOML config, JSONL
  results, error classes — `docs/contracts/future_cli.md`.
- **1f** Source-map indirection (LOC propagation: canonical map + per-cell
  index, alias multi-loc, partition-root fallback) — see "Source location
  (LOC) propagation strategy" below and `docs/contracts/sourcemap.md`.
- **1n** Pyrope LSP server (`.prp` only — never Verilog): one copyable binary
  (`livehd-lsp`, later a `livehd lsp` subcommand of [[1y]]) over LiveHD's
  front-end (`prp2lnast` + `upass` typecheck/bitwidth + `core/diag` [[1z]]).
  **Primary consumer Claude Code's `LSP` tool** — implement its exact op subset
  first (diagnostics + hover + documentSymbol, then definition/references);
  **secondary neovim**. Reuses the shared `tree-sitter-pyrope` grammar and the
  prp2lnast name maps. Ephemeral, per-buffer, no `lgdb`. **Scope = the semantic
  server only**; the tree-sitter grammar/neovim queries and the `prpfmt` formatter
  (incl. `textDocument/formatting`) are a separate project. **Phase A landed
  2026-06-02**: `lgshell --lsp` (lib `lsp/`) — lifecycle + diagnostics over stdio.
  Phases B–E (hover/nav/workspace) remain; spans sharpen with [[1f]]/[[3f]] —
  `docs/contracts/pyrope_lsp.md`.

## Group 2 — depends on Group 1

(none — prior LiveHD-internal Group 2 items completed or descoped.)

## Group 3 — depends on Group 2

- **3c** `Partition` descriptor as a tree-level HHDS attribute (`kind`,
  `latency_range`, ports, `ext`, `interface_hash`, `state_shape_hash`) —
  `docs/contracts/architecture.md §3`.
- **3d** New upass `lnast_to_slop` (parallel to `lnast_to_lgraph`) producing
  executable slop.
- **3f** Unified compile error/warning surface from `inou/prp`, upass,
  lgraph passes (+ tests for expected diagnostics). **Foundation landed** (the
  former 1z `core/diag` task: record schema + emitter + sink + JSONL + text
  renderer, spans `null`); this entry finishes full-fidelity `source_id` spans
  (via [[1f]]),
  lgraph-pass coverage, line/col resolution, and the CLI roll-up. Design:
  `docs/contracts/diagnostics.md`.
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


## IDAP perf-sweep pass

A correctness-preserving optimization/IDAP pass that toggles individual
upass stages on and off while keeping correctness invariants in place, so
we can attribute perf regressions to a specific pass and confirm each pass
earns its runtime cost. Concretely, we want to drive runs like:

```
time lg "inou.prp files:xx.prp |> pass.upass verifier:0 ssa:0 bitwidth:1 constprop:0 attributes:0"
```

and have the pass framework:

- Enumerate the pass flag matrix (`verifier`, `ssa`, `bitwidth`,
  `constprop`, `attributes`, …) and run the user-selected
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

Sequenced as **5e** so it lands after the partition descriptor (**3c**)
has stabilized — otherwise we'd be optimizing APIs that are still moving.

## simlib fixed-width int types

`simlib/uint.hpp` and `simlib/sint.hpp` (extracted from the `firrtl-sig`
upstream project, kept under `simlib/LICENSE.firrtl-sig`) are still used by
`core/tests/lconst_test.cpp` and the simlib examples. Once HLOP lands, these
types will be replaced/rewritten as part of the HLOP simulation runtime —
revisit the simlib int surface and the `firrtl-sig` attribution at that point.
