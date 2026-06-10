# upass — LNAST micro-pass pipeline

`pass.upass` runs a sequence of micro-passes (uPasses) over an LNAST. Its job
is to **propagate compile-time-known values, dead-code-eliminate everything
those values make redundant, and emit a new LNAST** that replaces the input.

This document is the contract for **how the pipeline works today**. The
next-generation design — push-based dispatch, one runner-owned scope-aware
symbol table — lives in [todo/livehd/2b.html](../todo/livehd/2b.html); its
prerequisite (the 1b Bundle/Entry reshape) landed 2026-06-09, and the 2b doc
carries that background. This file describes the current implementation 2b
will replace. When a change lands in this
area, update this doc in the same change.

## 0. Context and scope

upass consumes LNAST produced by `inou/prp` (new), `inou/pyrope` (legacy,
sunsetting), and `inou/slang`. Standing constraints from the LNAST contract:

- **Value ranges (`max`/`min`) are owned by the `bitwidth` pass** (§2), which
  observes stores during the runner walk and publishes ranges into `bw_meta`
  at `end_run` for `lnast_to_lgraph`. No other pass derives or folds on
  ranges (invariant #6).
- **Bare ref text.** upass assumes `$`/`%`/`#` prefixes are already removed;
  direction/storage is carried structurally, not encoded in the name.
- **tmp identity.** The symbol table is keyed by name; the `___N` tmp /
  named-var distinction never affects fold logic.
- **`delay_assign` is a fold barrier.** upass never folds across one; emit
  verbatim.

## 1. Pipeline shape (current)

A single `pass.upass` invocation processes each LNAST in `var.lnasts`:

```
(0) semacheck            ─ one-shot begin_iteration() walk over the source
                           tree (diagnostics point at user-shaped LNAST)
(1) uPass_ssa::run       ─ whole-tree pre-pass: harvests io_meta (the comb
                           inliner reads it mid-walk) and renames
                           multi-assigned vars to SSA-unique names
(2) runner walk          ─ ONE depth-first pre-order walk of the source
                           tree. For each node, every active pass's
                           process_<op>() hook runs in resolved order
                           (§1.1); hooks are nullary today and re-walk the
                           node's children via the cursor. The runner then
                           emits 0..N nodes into the staging tree
                           (Emit_kind: emit | drop_subtree), folding ref
                           operands through try_fold_ref on the way out.
(3) post-walk DCE        ─ dead_code_eliminate_staging: liveness backstop
                           over the staging tree (io-root rule; see 1d)
(4) finishers            ─ verifier, then assert: read-only walks of the
                           emitted (dest) tree; cassert tally (§2)
```

The single walk is the load-bearing performance property: **the source tree
is traversed exactly once per invocation, and the passes are called as the
walk visits each node**. There is no iteration knob and no fixed-point loop;
direct symbol-table mutation in a fixed pass order makes one sweep per node
sufficient.

### Re-runs

Each `pass.upass` invocation starts clean: symbol table rebuilt from scratch,
scope stack empty. Re-runs (after another lowering stage, after an `import`
resolves) get a fresh view.

### Pass dependency map

Pass order is owned by **`depends_on` declarations + a topo sort**, not by
folklore. The mechanism (wired and live today):

- Each pass registers itself via a static
  `upass::uPass_plugin("name", setup_fn, {deps...})`
  (`upass/core/upass_core.hpp:401-416`); the optional third argument is the
  `depends_on` list.
- `uPass_runner::resolve_order` (`upass/runner/upass_runner.cpp:231-282`)
  runs a DFS topo sort over the requested names, pulling declared
  dependencies in first and detecting cycles. Duplicate requests are
  de-duplicated (first completion wins), so a pass appears once in the
  dispatch list no matter how often it is requested.
- The requested-name list is assembled by `pass/upass/pass_upass.cpp`
  (label toggles + the `order=` csv override).

Current default request order, with each placement's reason:

| pass         | declared `depends_on` | placement reason |
|--------------|----------------------|------------------|
| verifier     | —                    | requested first *and* last; resolve_order dedups to one instance — harmless, it is a dest-walk finisher whose slot in the per-node dispatch is irrelevant |
| semacheck    | —                    | one-shot pre-walk; only needs to be registered before the walk starts |
| func_extract | —                    | placed before the value passes (registration order) |
| attributes   | —                    | first value pass: sticky/attr state must exist before constprop folds `.[attr]` reads |
| typecheck    | `{attributes}`       | declared kinds available; fails fast before the value passes do work |
| constprop    | —                    | after attributes + typecheck (registration order today) |
| bitwidth     | —                    | **last opt pass**: no `fold_ref`, read-only, must observe stores pre-DCE (§2) |
| coalescer    | —                    | after constprop so its known-const guard sees an up-to-date `try_fold_ref`; dropped from the default order entirely when `toln:0 && tolg:0` (nothing materializes) |
| assert       | `{constprop}`        | reads the values constprop populates; last before the final verifier slot |

**Pending wiring (small, pre-2b):** only the two edges above are declared;
the rest of the relative order rides the builder's registration order. Move
the placement reasons into declared edges — `constprop {attributes,
typecheck}`, `bitwidth {constprop}`, `coalescer {constprop, bitwidth}` — so
`order=` csv users can't silently drop a prerequisite. One policy decision
rides along: `resolve_order` auto-pulls a declared dependency back in even
when its label disables it, so the existing warn-and-drop special case
(`constprop:0` forces `assert:0`, `pass_upass.cpp:236`) must be generalized
to every new edge: *a disabled pass drops its dependents with a warning*,
never silently re-enables.

Rules that matter more than the exact spelling:

- `semacheck` runs before rewrites mutate the tree, so user diagnostics point
  at source-shaped LNAST.
- SSA naming is complete before the runner walks; `bitwidth` and
  `lnast_to_lgraph` consume SSA-stable names.
- Attribute conflict diagnostics are owned by `attributes`; `typecheck` is
  compile-time-only and propagates nothing to LGraph.
- `bitwidth` must see every store before DCE.
- When a new uPass is added or an existing pass starts reading another pass's
  facts, declare the edge — do not extend the builder's implicit order.

For the 2b push-dispatch redesign of this dispatch core, see
[todo/livehd/2b.html](../todo/livehd/2b.html); update this section when it
lands.

## 2. Per-pass responsibilities

### semacheck (semantic legality, ex-lnastfmt)

Owns the user-facing checks relocated out of `pass.lnastfmt` (now a
compiler-internal structural validator): write to a read-only / derived
attribute (`:[bits=]`, `:[max=]`, size/sign/key — the only enforcement site),
declare-once (same-scope redeclaration), and a shadowing backstop (prp2lnast
enforces no-shadowing first, with a sharper span). One-shot
`begin_iteration()` walk over the source tree — runs before the walk mutates
anything (located via the declare/attr_set loc-carry chain). Read-only; emits
`core/diag` records (`read-only-attr-write`, `redeclaration`,
`variable-shadowing`). Default on.

### constprop

Owns *value* facts. Keeps the (currently constprop-owned, §3) `Symbol_table`
and folds scalars/bundles through it; classification decides which writes are
already-known and droppable. Also owns the import machinery statics
(`pending_imports_`, `ambiguous_units_`, `function_registry`). After 2b the
symbol table moves to the runner and constprop reads pushed-in operand
bundles; its per-name side maps (`range_map`, `typename_of_var`,
`decl_unsigned_max_`, `decl_named_type_`, `reg_decl_names_`,
`mut_decl_names_`, `tuple_slot_ref_`) retire onto the Bundle (see the
`structs.md` inventory at the repo root for the full justified list).

### attributes

Owns `attr_set`/`attr_get` *semantics*, attribute propagation, and
attribute-conflict diagnostics. Attributes are read-only views of facts
inferred by other passes (`bits`, `max`, `min`), or user-visible metadata
that must propagate and may survive to LGraph generation. Sticky attributes
(canonical leading-`_` names; `debug` ⇒ `_debug`) propagate through
assignment-shaped ops via `Sticky_handler`; category-B wiring attributes
(`clock_pin`, `posclk`, …) are validated here and reach LGraph **through the
emitted `attr_set` nodes in the dest tree** (tolg's `lower_attr_set` reads
the tree, never a pass-private map). Dispatch is per-attribute via
`Handler_registry`.

### typecheck

Compile-time type legality only (kind lattice: integer/boolean/string/…).
Checks declaration/store/call compatibility, tuple/scalar shape mismatches,
field membership, and no type change on already-typed destinations. Declares
`depends_on {attributes}`. It may read type/shape facts but does not
propagate attributes to LGraph and does not own attribute-query behavior.
Responsibility boundary, not a forced directory split — sharing
implementation with `upass/attributes` is acceptable where simpler.

### bitwidth (last opt pass in the walk — read-only, no fold_ref)

Runs **last** in the per-node dispatch. No `fold_ref` (never feeds — or
corrupts — constprop), read-only (never rewrites a node). Running inside the
walk lets it observe every store **before DCE** — the init value of a typed
declaration lives in a separate `store` node that DCE removes when the var is
unused, so a post-DCE pass would miss overflow on a dead comptime const. At
`end_run` it (a) emits any `bitwidth-overflow` compile error ("does not
fit"; signedness-aware, wrap/sat-exempt, scalars-only today) and (b)
publishes `max`/`min` into `lnast->bw_meta()` for `lnast_to_lgraph`. Ranges
are `[min, max]` + a single `unbounded` flag; `bits`/`sign` are derived on
demand, never stored. Wrap/sat narrowing math lives here (attributes keeps
only the policy bit). 2b moves the overflow diag to the offending store
(deleting the `end_run` throw, `write_site_`, `pending_overflow_msg_`).

### coalescer

Deferred-emit / dead-store elimination: parks pure scalar defs
(`pending` map), materializes-at-use via the runner's `runner_emit_at_fn`
replay seam, DSE on overwrite, drops const-known defs. Comptime `mut` stores
are parked and never comptime-dropped at flush (see memory
`comptime_mut_dse_coalescer`). Disabled automatically when nothing
materializes (`toln:0 && tolg:0`). The remaining lazy-DCE work (sea-of-nodes)
is [todo/livehd/1d.html](../todo/livehd/1d.html), sequenced after 2b.

### func_extract

Inline-vs-spawn decisions at call sites ride the runner's virtual-splice
inliner (§4); func_extract owns the spawn side — extracting comb/mod/pipe
bodies into separate LNASTs in `var.lnasts` for independent `pass.upass`
walks — plus closure capture of outer comptime scalars/bundles/imports into
the extracted body. 2b retires the capture maps via `function_scope`
read-through on the shared table.

### SSA (whole-tree pass, before the runner)

`uPass_ssa::run` runs once over each LNAST **before** the main runner
(`pass_upass.cpp::work`). It harvests port names/widths into
`lnast->io_meta()` and renames multi-assigned user vars to SSA-unique names.
The runner therefore walks — and emits — post-SSA names. (`reg` names are
never SSA-versioned; they carry timing.)

### Typesystem (split between `typecheck` and `attributes`)

upass owns Pyrope's basic typesystem. The rules are **declaration-driven**: a
value's size envelope and nominal identity come from the declared type (or a
literal's implied type), never from the value itself.

- **Size envelope (`bits`/`max`/`min`)** comes from the declared type:
  `:uN`/`:sN` set width and range; `:bool` is the 1-bit signed envelope
  (`min=-1, max=0, bits=1`); `:string` has no numeric envelope (reads `nil`).
  When only `max`/`min` (or `range=lo..=hi`) is pinned, `bits` derives on
  demand. Unannotated → all three read `nil`; a bare `true`/`false` literal
  is auto-typed `:bool`.
- **Aggregates have no `.[bits]`** — only scalars. Per-field `t.a.[bits]`
  resolves when the field carries a typed annotation.
- **`size` is cardinality, not bit width** (`tup.[size]` = entry count,
  `str.[size]` = character count).
- **`typename`** is diagnostic-only, set for `type Name = …` declarations;
  structural `==` ignores it; nominal identity is `is`'s job (`p is Foo`
  folds iff `p.[typename] == "Foo"`).
- **Comptime is non-sticky on copy.** A `mut` binding holding a known value
  does not read as comptime; only `const`-decl (resolved) or an explicit
  `comptime` marker does.

Implementation today: the type info attaches per variable as
`Type_info { decl_kind, num_kind, bits, is_comptime, has_type_spec }` in
`upass/attributes`; `derive_max`/`derive_min`/`derive_bits` are pure
functions of that info plus explicit attr_sets; per-field types ride the
`tuple_get_alias`/`direct_alias` chains. After 1b these become typed
`Entry`/`Bundle` fields; after 2b the side maps retire.

### verifier / assert (read-only finishers)

Walk the **dest** LNAST after the source walk completes; they see no symbol
table. Casserts are emitted with their cond simplified (`const(true)`,
`const(false)`, or a surviving `ref`):

| disposition  | trigger                                       | counter         |
|--------------|-----------------------------------------------|-----------------|
| known-true   | cond is `const_true`                          | `pass_count`    |
| known-false  | cond is `const_false`                         | `fail_count`    |
| unknown      | cond is a surviving `ref` or unknown const    | `unknown_count` |

Comptime-false cassert → `upass::error` unless the test header expects it.
`verifier_pass:N` / `verifier_fail:N` labels set expected counts (aggregated
across func_extract-spawned lnasts); mismatch raises `upass::error`.
`prplib.py` reads `:verifier_pass:`/`:verifier_fail:` from the `.prp` header.

**Test mode split** (driven by `prplib.py`): `:type: upass` = pipeline smoke
test (`verifier:false`); `:type: comptime` = correctness check, verifier on.

When multiple `pass.upass` invocations exist (e.g. around an unresolved
`import`), the finishers attach only to the final one — deferred while
pending imports remain (§8).

## 3. Symbol table & scopes (current)

`Symbol_table` (`upass/core/symbol_table.{hpp,cpp}` since the 1b landing)
is owned by **constprop** today (`uPass_constprop::st`); 2b moves ownership
to the runner.

- One `shared_ptr<Bundle>` per live name
  (`Scope.varmap: absl::flat_hash_map<string, shared_ptr<Bundle>>`).
- Scope kinds: `Function`, `Block`, `Always`, `Conditional`
  (`symbol_table.hpp:18`); `block_scope(key)` frames are re-entrant (same
  `Scope` object recovered on re-entry). Every `{ }` notifies the table;
  reads walk innermost → outermost; writes land in the declaring scope.
- **COW value semantics**: whole-bundle assignment shares the `shared_ptr`;
  in-place field mutation un-shares first (`unshare_for_write`,
  `use_count()>1` → clone). Re-fetch `get_bundle` after writes.
- **If-arm contract: mark-uncertain + invalidate-on-exit**
  (`symbol_table.cpp:282-357`). A declaration inside an arm lives in that
  arm's scope and dies at `}`. A write to a name declared in an enclosing
  scope under a runtime-uncertain condition invalidates that name's value
  fact on arm exit. There is **no frame-per-arm merge** — the
  both-arms-write-the-same-constant fold is deliberately not attempted. (2b
  keeps exactly these semantics; decided 2026-06-09.)
- **Function scopes are hard visibility barriers** today: `find_decl_scope`
  (`symbol_table.cpp:20-36`) stops at a `Function` scope for reads *and*
  writes — a callee body cannot reach the caller's locals at all, which is
  why func_extract carries closure-capture maps. 2b/B splits this into
  read-through (closure capture of outer comptime consts) / write-block
  (outer write = compile error).

## 4. Inline (runner virtual-splice)

The comb-call inliner is **runner-owned** (task 1i; design rationale and
literature refs in the header comment of
[`upass/runner/upass_runner.cpp`](runner/upass_runner.cpp)). The runner
maintains a splice stack of callee LNASTs (`active_inline_callees_`),
synthesizes `param = actual` bindings at the body head so the passes see
normal assigns, clones the callee per call site (fresh fold context each
time), and caps recursion depth per callee. `Lnast_manager` frames
(`frames_`, `tmp_remap_`, `intern_pool_`) keep inlined names stable and
`___N` tmps collision-free. Vararg callees resolve `args[i]`/`args.NAME`
directly; pipe/mod templates specialize per signature into a `Sub` (task
1p). Passes are unaware they are inside an inline.

## 5. Bundle

`Bundle` (`upass/core/bundle.{hpp,cpp}` since the 1b landing 2026-06-09) is
the comptime value/tuple record threaded through the passes: a data map
(tuple leaves; the bare-scalar leaf lives in an inline root Entry — zero map
nodes for scalars, spilled on whole-map views), a residual attrs map (bare
names, sticky = leading `_`), and typed pass-fact fields on `Entry`
(`trivial`/`decl_max`/`decl_min`/`bw_max`/`bw_min`/`kind`/`comptime`) and
`Bundle` (`mode`/`type_name`) per the 2b per-pass table. Point lookups and
prefix runs are `find`/`lower_bound`-bounded; `non_attr_entries()`/
`get_attrs()` are zero-copy map references. The full per-pass data-structure
inventory (what each side map is for, what 2b retires) is `structs.md` at
the repo root.

## 6. Dispatch (current)

Hooks are **nullary** virtuals generated per node kind
(`upass/core/upass_core.hpp:247-356`, ~35 `PROCESS_NODE` entries): a pass
overrides `void process_plus()` etc. and re-walks the node's children itself
via the `Lnast_manager` cursor. Notable fan-outs in the runner
(`upass_runner.cpp:3554-3584`):

- `store` with 2 children dispatches to `process_assign`; 3+ children
  (field-path store) to `process_tuple_set`. (`assign`/`tuple_set` LNAST
  nodes were deleted in task 1t — these are dispatch-method names only.)
- `declare` dispatches to `process_declare`.

Cross-pass facts flow through **pull seams** wired at construction:

- `runner_fold_fn` → `uPass_runner::try_fold_ref` (first non-nullopt across
  `fold_capable_passes`; attributes' wrap/sat narrowing beats constprop's
  raw value).
- `runner_type_query_fn` → combined KIND (typecheck) + declared envelope
  (attributes) for constprop's `does`/type folds.
- `runner_emit_at_fn` → the coalescer's materialize-at-use replay.
- 9 `provide_*` virtuals / `try_*` runner wrappers (bundle fields, typename,
  decl type, field type, scalar kind, decl storage, range, tuple slot ref,
  tuple shape) over `shared_st_passes_`.

2b replaces all of this with push-based dispatch
(`Vote process_<op>(Bundle& dst, span<const shared_ptr<const Bundle>> src)`,
`Vote = keep|drop`, runner-resolved operands, emit derived from
`dst.scalar()`); `runner_emit_at_fn` survives as the coalescer's emit seam.
See [todo/livehd/2b.html](../todo/livehd/2b.html) — this section describes
what it deletes.

## 7. Emit model (current)

The runner emits into a **staging tree** (`staging`, `dest_forest_`) as the
walk leaves each source node (`emit_push`/`emit_pop`,
`upass_runner.cpp:286+`). Per node:

- `classify_statement` across `classify_capable_passes` → `Emit_kind`
  (`emit` | `drop_subtree`, `upass_core.hpp:31-38`): a pass that proves the
  node redundant (e.g. constprop on a comptime store) drops it.
- On `emit`, ref operands are folded through `try_fold_ref`
  (`emit_ref_or_folded`) so known values materialize as `const`.
- The coalescer may park the node instead and replay it later at first use
  (`runner_emit_at_fn`).
- After the walk, post-walk DCE sweeps the staging tree (io-root liveness
  rule), and the staging tree replaces the source LNAST.

## 8. Pending imports

Unresolved `import`s are tracked by constprop statics (`pending_imports_`;
`ambiguous_units_` for multiply-defined units) rather than per-Bundle
markers. The kernel iterates `pass.upass` until convergence with whole-file
retry (task 1m): an invocation that still has unresolved imports defers the
verifier/assert finishers; the next invocation rebuilds the symbol table
fresh and re-attempts. Generic, un-inlined function bodies have untyped
inputs ⇒ no envelope ⇒ no overflow check; they are realized only by inlining
into a caller, where the concretely-typed copy is checked.

## 9. Invariants

These must hold after every walk:

1. **Every ref in the dest LNAST is defined** — by an emitted assignment, a
   port, a register, or folded to `const` at emit time. No dangling refs.
2. **Regs are never dropped.** Reg assignments carry timing; constprop may
   fold their RHS but not elide the assignment.
3. **Ports are preserved at the module boundary** — input ports read, output
   ports written — even when a constant flows through.
4. **Deterministic emission order.** Children are emitted in source-tree
   order; the dest LNAST is a topologically-consistent rewrite, not a
   re-scheduled one.
5. **`delay_assign` is emitted verbatim**; upass never constant-folds across
   one regardless of symbol-table state.
6. **Only `bitwidth` derives value ranges; nothing else folds on them.**
   `max`/`min` are derived exclusively by `bitwidth` (last opt pass) and
   consumed only by `lnast_to_lgraph` via `bw_meta`. bitwidth is the sole
   owner of overflow (range-vs-declared-envelope) errors.
7. **Spawned LNASTs are self-contained.** Outer-scope comptime values are
   inlined at the use site before func_extract spawns the callee body;
   spawned LNASTs share no symbol-table state with their caller.
8. **Single sweep per source node.** Exactly one walk per invocation; no
   iteration knob, no fixed-point loop.

## 10. Known gotchas

- **Canonical bare ref text.** Symbol-table keys assume `$`/`%`/`#` prefixes
  are already removed. Legacy prefixes from `inou/pyrope` are producer bugs,
  not alternate spellings.
- **`Const::invalid()` is overloaded.** It stands for both "not tracked" and
  "tracked but unknown". When 1b's typed fields land, revisit `is_invalid()`
  call sites in constprop to confirm each still means "don't fold".
- **`store` fans out to two dispatch methods (§6).** The LNAST nodes
  `assign`/`tuple_set` no longer exist (task 1t) — `store` is the single
  write node — but the upass layer still dispatches 2-child stores to
  `process_assign` and 3+-child stores to `process_tuple_set`. Any semantic
  check a pass runs on the LHS of `process_assign` (const-rebind tally,
  type/bitwidth propagation, wrap/sat narrowing) must also fire from
  `process_tuple_set` on the same root — `a.b = 1` arrives as a field-path
  store, and a check living only on the `process_assign` path is silently
  bypassed. Nil-rvalue stores are invalidation, not a binding — exclude from
  bind tallies. Canonical example:
  `uPass_attributes::process_tuple_set` calls `record_assign(target,
  rhs_is_nil)` (`upass/attributes/upass_attributes_tuple.cpp:272`).
- **Cursor discipline.** Hooks re-walk children via `Lnast_manager`
  (`move_to_child`/`move_to_sibling`) and must restore the cursor before
  returning — fold helpers (`try_fold_ref`) may move it
  (`while_nonterm_detection` memory). 2b removes per-pass cursor walking
  entirely.
- **Bundle alias chains.** `direct_alias` reads are one hop; chains
  (`c = b; b = a;` then `c.[size]`) walk multiple hops. Consumers should
  tolerate one or two hops.

### 10.x Open diagnostics issue

`a and b or c` (mixed distinct operators at one precedence level without
parens) has no defensible associativity. prp2lnast lowers it as a silent
left-fold with a bare print. The `core/diag` channel now exists and prp2lnast
emits located compile errors elsewhere (scope checks, decl-init kinds), so
the remaining work is just turning this case into a proper located error.
Also applies to `a implies b or c` and any future mixed-operator tier the
grammar permits but the language prohibits.

## 11. Where this is headed

The migration steps formerly listed here are subsumed by the two task docs:

- 1b — Bundle/Entry reshape (upass/core move, typed pass-fact fields,
  residual attrs, inline root, bounded scans) — **LANDED 2026-06-09**; the
  task doc was deleted, background lives in the 2b doc's "Depends on"
  section.
- [todo/livehd/2b.html](../todo/livehd/2b.html) — push-based dispatch
  flag-day: runner-owned scope-aware symbol table, `(dst, src…)` hooks,
  emit model (a), `Call_resolver`, pull-seam deletion. Freezes other upass
  work until it lands.
- [todo/livehd/1d.html](../todo/livehd/1d.html) — sea-of-nodes demand-driven
  emit; sequenced **after** 2b.

Landed milestones formerly tracked here: `Handler_registry` dispatch-vector
fix, `Cursor_state` lightweight save/restore, and the bitwidth relocation
(read-only, no-fold, last-opt-pass + centralized overflow — goal 1n; its
residuals live in todo/ 1n).
