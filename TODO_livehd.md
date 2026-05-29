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

- **1c** `pass.lnastfmt` validator — **deferred** (extensions only;
  core is already substantive at `pass/lnastfmt/pass_lnastfmt.cpp`,
  ~396 lines: ref text shape, `delay_assign` arity, `assign` arity,
  tmp-ref unwritten detection, io-shape invariants).
  - **Tried:** Diagnostic in the 2026-05-27 session. agent-types
    confirmed the core validator already covers most §5 invariants.
    Designer-helper pass: opt-in, run after `inou.prp` or
    `pass.upass` to check structural invariants and catch bugs in
    the LNAST producers.
  - **Pending §11.5 extensions** (all gated on [[1w]] landing):
    - Write-once tracking per `(target, attr_key)` for `attr_set`.
    - `attr_set`-after-read freeze (reject `attr_set` once any
      `attr_get` of the same target has emitted).
    - Reject `attr_set` outside declaration scope (mid-body
      `a.[foo] = bar` → hard compile error).
    - Attr-key whitelist enforcement.
  - **Needed for landing:** [[1w]] first (which itself needs the
    §10 ST API rework). The §11.5 checks read post-constprop ST
    state that doesn't exist yet.

- **1d** Bit-range / bit-selection lowering bugs in
  `inou/prp/prp2lnast.cpp`. Pyrope's `#[…]` syntax (bit read,
  bit write, bit reductions) is partially broken on the LNAST
  producer side — multiple cases silently emit wrong code instead
  of either a proper `get_mask`/`set_mask` or a hard error.
  Sibling of [[2q]] (which adds the constprop/upass side); this
  entry covers the prp2lnast-only fixes that can land first.
  - **Symptoms** (observed via `inou.prp |> lnast.dump`):
    - `b#[1..<4] = 1` lowers to `assign ref('b#[1..<4]') 1` —
      bit-range syntax leaks into the ref text. Cause:
      `process_lvalue_for_assign` has no `bit_selection` arm and
      falls through to the text-fallback at `prp2lnast.cpp:827`.
      Expected: a `range` / `minus` setup matching the read path,
      then `set_mask new_word b mask 1`, then plain `assign b
      new_word`. Mirrors how `member_selection` lvalues already
      emit `tuple_set`.
    - `const a.b:u3 = 1` silently lowers to `tuple_set a b 1`,
      dropping the `const` decl and the `:u3` type cast. Cause:
      `process_lvalue_for_assign`'s `member_selection` arm ignores
      `decl_node` and `type_cast_node`. Pyrope has no syntax for
      "declare new tuple field via path" — this must be a hard
      compile error. Same fix applies if the LHS is a
      `bit_selection`.
    - `r = b#[1,4]` silently drops the `,4` in
      `bit_selection_to_node`. Cause: index walker treats the first
      child as the entire mask. Reject comma-lists inside `#[…]`
      (tuple indices are not supported; if we later want them, lower
      to a bitmask OR — but error first).
  - **Plan:**
    1. Lift `make_const_mask` / `make_range_mask` / `emit_range`
       out of `bit_selection_to_node` into a shared helper so both
       read and write paths reuse the same mask synthesis.
    2. Add a `bit_selection` arm to
       `process_lvalue_for_assign`. Shape: walk the bit_selection
       to recover `base` and the `select` subtree, compute
       `mask_ref` via the shared helper, emit
       `set_mask new_word base mask_ref rvalue` followed by
       `assign base new_word`. `set_mask` stays pure-functional
       (matches LGraph Dlop shape: `set_mask dest input mask
       value`), `assign` writes the result back.
    3. In both the `member_selection` and (new) `bit_selection`
       arms, hard-error if `decl_node` or `type_cast_node` is
       non-null: `const`/`mut`/`:type` are only legal on bare
       identifiers.
    4. In `bit_selection_to_node`, error on a comma-list inside
       the `#[…]` index node.
  - **Affects:** `prp-bitreverse`, `prp-bitset`, `prp-formux`,
    `prp-valid_unknown_bits` bit-range portion, and the
    `pp.prp` / `assert.prp` repros from the working tree. The
    write-side fix is a prerequisite for [[2q]] (the constprop
    bit-range work) — without it, set_mask never reaches
    constprop in the first place.

- **1i** Comb-call inliner as a **virtual splice into the runner's
  single linear traversal** — no deep copy. **Goal 1.**

  **Why.** Today `uPass_constprop::evaluate_callee_inline` is not an
  inliner: it's a *constant evaluator* — a second, partial
  reimplementation of the runner's statement semantics over its own
  side maps (`local_values`, `local_attrs`, `local_ranges`,
  `local_decl_widths`). It binds actuals→params, runs a fixed-point
  sweep, and returns the **output `Const`s**. It is **all-or-nothing**:
  if any statement can't fold to a constant it aborts and the `fcall`
  stays runtime. That design (a) duplicates the real `process_*`
  handlers — every construct must be hand-coded twice (the
  2026-05-28 session had to add `attr_get [bits]`, `type_spec`, and
  nested-`stmts` support just to inline `forunrollbits.prp`), and
  (b) can never emit *partially*-folded hardware. The recent fixes are
  a stopgap; 1i supersedes the mini-interpreter.

  **Decision: no deep copy.** The upass is a **single linear pass**
  (an optional 2nd pass runs only when the 1st couldn't because an
  `import` LNAST wasn't ready). Deep-copying the callee body into the
  caller tree would require re-traversing the generated code and
  re-running passes to stabilize — many passes until fixpoint. We do
  not want that. Instead the runner, on hitting an `fcall` whose comb
  LNAST is in `function_registry`, **descends its cursor into the
  callee's stmts as part of the same walk**, so the existing
  `process_*` handlers process the inlined body in-order, once.
  Whatever folds, folds; whatever stays runtime becomes real IR at the
  call site. No second implementation, no all-or-nothing.

  **Mechanism.**
  1. **Inline-frame stack.** Add a stack of frames to the runner;
     each frame = `{const Lnast* callee, rename_prefix,
     return_target(s), ref_writeback_map, depth}`. The tree cursor
     (`move_to_child/sibling/parent`) gains a thin redirection layer:
     when the walk would step past an inlinable `fcall`, push a frame
     and redirect subsequent stepping into `callee`'s `stmts` child;
     when that body is exhausted, run the epilogue and pop, resuming
     the caller at the `fcall`'s next sibling. This is a *virtual*
     splice — the callee tree is read in place, never copied or
     mutated.
  2. **Fake-assign prologue (arg binding).** Before descending, bind
     each actual to its formal as a synthesized `assign`
     (`<prefix>param = actual`), field-wise for tuple/bundle args and
     positional-or-named matching the existing `Call_actual` logic.
     These bindings feed the *same* symbol-table path `process_assign`
     uses — no special evaluator. A `ref` param records a writeback
     entry so the epilogue copies `<prefix>param` back to the caller
     variable.
  3. **α-rename / aliasing (the crux).** Because nothing is copied, the
     callee nodes keep their original names (`x`, `n`, `___1`…). Every
     name read/written while a frame is active must be namespaced by
     that frame's `rename_prefix` so it can't collide with caller
     temporaries or with other call sites of the same callee. Resolve
     names through the frame stack (innermost-first); the prologue's
     RHS actuals resolve in the *enclosing* frame's namespace, the
     LHS params in the *new* frame's. Pick a prefix scheme that's
     stable per call site (e.g. `<callee>$<callsite_id>$`) so a 2nd
     upass pass reproduces the same names.
  4. **Epilogue (output + ref writeback).** Map the callee's declared
     outputs back to the `fcall` destination(s): `dst = <prefix>out`
     (single), or field-wise (`dst.field = <prefix>out.field`) for
     tuple / multi-output, mirroring `try_eval_comb_call`'s current
     bundle splat. Apply `ref` writebacks (`caller_var =
     <prefix>param`). Then pop the frame.

  **Constraints / guards.**
  - **comb only.** Skip `pipe[N]` / `mod` / anything stateful — their
    state/timing is instance-specific and can't be naively spliced.
  - **Depth / cycle guard.** Reuse the `kInlineMaxDepth` seed; detect
    direct/indirect recursion via the frame stack and refuse (a
    recursive comb without a static bound can't inline).
  - **Import not ready.** If the callee LNAST isn't in
    `function_registry` yet (pending `import`), do **not** inline this
    pass — leave the `fcall`; the existing 2nd-pass-after-import
    mechanism picks it up. Ties into the `pending_import` poison work
    ([[2k]] / `docs/upass_redesign.md`).
  - **Idempotence across the optional 2nd pass.** Same call site must
    produce the same rename prefix and the same bindings so a re-run is
    a no-op where the 1st pass already inlined.

  **Retire / fold in.** Once 1i lands, `evaluate_callee_inline` and its
  duplicate handlers go away (or shrink to a thin pure-constant fast
  path for cassert discharge if profiling shows it's worth keeping).
  The verifier's cassert discharge then rides on the normal folded IR
  rather than the evaluator's returned `Const`s.

  **Relationship.** Unblocks the general case behind the
  bit-range/`set_mask` folding in [[2q]] and the `.[bits]`/`.[max]`/
  `.[min]` width reads that [[1v]] (typesystem) publishes — inlined
  bodies query the same type info as caller-scope code. Sibling of
  [[1d]] (producer-side bit lowering) only in that both feed the
  runner clean IR.

  **Affects.** `forunrollbits.prp`, `pp/pp2/pp3.prp` repros, and any
  comptime test that calls a comb with non-constant-foldable internals
  (those abort under the current evaluator and would instead inline to
  real IR). Validate that the inlined IR matches what a hand-flattened
  body would produce, and that multi-call-site renaming stays
  collision-free.

## Group 1-complex — foundation, larger scope

Tasks that are independent of other Group 1/2 work but are large enough
that they warrant their own bucket. Can be done in parallel with regular
Group 1 entries; downstream Groups treat them as Group 1 dependencies.

- **1b** New CLI: `setup/run/status/list/describe`, TOML config, JSONL
  results, error classes — `docs/contracts/future_cli.md`.
- **1f** Source-map indirection (LOC propagation: canonical map + per-cell
  index, alias multi-loc, partition-root fallback) — see "Source location
  (LOC) propagation strategy" below and `docs/contracts/sourcemap.md`.
## Group 2 — depends on Group 1

- **2m** Drop `wrap`/`sat` as attributes; keep only the `wrap`/`sat`
  *statement* form, lowered to a new LNAST `wrap`/`sat` op — see
  "wrap/sat: statement-only via LNAST op" below.
  - Depends on [[1v]] (LNAST typesystem upass in TODO_prp.md): the
    wrap/sat op reads the target's `bits`/`max`/`min`, which the type
    system publishes (unless explicitly set as an attribute). Without
    the typesystem in place, the op has no `max`/`min` to consume.
  - Fixes failing comptime tests: `prp-wrap_checks` (re-type form
    `x:u4:[wrap] = …`), `prp-wrap_complex` (`wrap x = …` and
    `sat x = …` statements). Open question: `prp-wrap_checks`'s
    `x:u4:[wrap] = …` syntax — is `[wrap]` on a type annotation
    considered the kept statement form or the dropped attribute
    form? Resolve before implementation; the test may need a rewrite
    to `wrap x:u4 = …` if the latter.

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
