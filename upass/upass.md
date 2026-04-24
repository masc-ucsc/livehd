# upass — LNAST micro-pass pipeline

`pass.upass` runs a sequence of micro-passes (uPasses) over an LNAST. Its job
is to **propagate compile-time-known values, dead-code-eliminate everything
those values make redundant, and emit a new LNAST** that replaces the input.

This document is the contract for how the rewrite works today and how the
deferred pieces (func_call, func_def extraction, attr_set side-map, tuple
flattening, if-branch pruning) plug in later. When a future change touches
this area, update this doc first.

## 0. Context and scope

upass lives downstream of `pass.lnastfmt` and consumes LNAST produced by
`inou/prp` (new), `inou/pyrope` (legacy, sunsetting), and `inou/slang`.
Several LNAST-wide cleanups in `lnast_todo.md` directly constrain upass.
This doc commits to the **target** semantics; some Slice 1 checks are
temporary stand-ins that go away as those tracks land.

- **`lnast_todo.md` §10 — Symbol-table API + value-attr inference.**
  lnastfmt owns `__min`/`__max` computation via interval arithmetic,
  keyed per-SSA-version. upass **reads** these through the ST; it does
  not derive them. Decl-only attrs (`storage`, `direction`, `reset_pin`,
  user-asserted `max`/`min`) are read via `st.get_decl_attr`.
- **`lnast_todo.md` §11 — unified `attr_set`/`tuple_set` shape,
  write-once semantics, `assign` collapse.** upass must respect the
  write-once / comptime-guarded rules; Slice 5 below folds the
  enforcement + side-map into upass. Slice 1 just pass-through-copies
  `attr_set`.
- **`lnast_todo.md` §12 — `$`/`%`/`#` prefixes → ST-backed
  direction/storage.** Slice 1 uses prefix checks (`name.front() == '#'`
  for regs, etc.) as a pragmatic stand-in. Every such check in upass must
  migrate to `st.is_reg(name)` / `st.is_input(name)` once §12 lands.
- **`lnast_todo.md` §13 — `Tree_index`-identity tmps.** Slice 1 uses
  `Lnast::is_tmp(text)` (string prefix). Migrate to `node.is_tmp()` when
  §13 lands. upass never tracks tmps by string outside of the classify
  hook; the symbol table already keys by name regardless.
- **`lnast_todo.md` §15.2 — `delay_assign` offset extensions.** upass
  never folds across a `delay_assign` (timing-carrying); emit verbatim.
  The folding-validity check is "`delay_assign` reads a known reg Q
  value" which is effectively never comptime-known, so Slice 1 treats
  these as opaque side effects.
- **`lnast_todo.md` §14 — migration rollout.** upass's ST consumer side
  flips with §12 migration; until then, the prefix fallbacks documented
  above are the contract.

Keep section references here up to date when the todo sections renumber.

## 1. Problem

The original uPass runner only read the LNAST — it walked it, populated
`Symbol_table` in `uPass_constprop`, and left the tree unchanged. So:

```
livehd "inou.prp files:…/add_trivial.prp |> pass.lnastfmt |> pass.upass |> lnast.dump"
```

emits the original LNAST verbatim. Nothing is propagated into the IR that
`lnast.dump` (or later stages) sees. The plan below fixes that: the runner
owns a **staging LNAST** that it writes into while traversing the input, and
swaps into `var.lnasts` at the end.

## 2. Design

### 2.1 Single runner, single staging tree

- The **runner** (`uPass_runner`) owns a `std::shared_ptr<Lnast>` called
  `staging` plus a write cursor (`staging_stack` of `Lnast_nid`). One tree,
  one cursor — upasses never write directly.
- Each uPass contributes a **classify-before-emit** decision on the current
  read-side node. The runner aggregates decisions (first pass to say
  `drop_subtree` wins) and, if the decision is `emit`, appends a copy of the
  input node to `staging` at the current write cursor.
- Structural nodes (`top`, `stmts`, `if`, future `func_def`) push a new parent
  onto `staging_stack` before recursing and pop after.
- At end-of-traversal, the runner replaces `var.lnasts[i]` (and `lm`'s
  internal `lnast`) with `staging`. The old tree drops when refcount hits 0.

### 2.2 Iteration policy

- **One pass, no fixpoint**, when the input has no `func_call`/`func_def`.
  A forward sweep with a symbol table converges in one pass for straight-line
  code; re-iterating would just duplicate work.
- When func_calls are present (future work), the runner will iterate so that
  a resolved callee can feed values back into a caller's symbol table. This
  is the **only** reason to iterate.
- The `changed`/`begin_iteration` hook in `uPass` stays, but the default
  runner path only calls `begin_iteration` once unless func_calls exist.

### 2.3 Classify-before-emit hook

```cpp
enum class Emit_decision {
  emit,           // copy this node (and recurse into children normally)
  drop_subtree,   // skip this node and all its children
  fold_to_const,  // emit as `const <lconst>` instead of the original op
};
```

`uPass` gets one new virtual:

```cpp
virtual Emit_decision classify_before_emit() { return Emit_decision::emit; }
```

Runner protocol:

1. On each input node, the runner calls `classify_before_emit` on every
   registered pass **in registration order**.
2. `drop_subtree` short-circuits (skip node + children). `fold_to_const`
   overrides `emit` but loses to a later `drop_subtree`.
3. If no pass drops or folds, emit a copy of the input node and recurse.
4. For statement-level op nodes (`plus`, `assign`, `eq`, …), a leading
   `drop_subtree` from constprop means the whole statement (op + operand
   children) is omitted from the staging tree.
5. For `ref` operands **inside** an op that the runner is emitting, constprop
   can rewrite the emitted child to `const <value>` via `fold_to_const`.

### 2.4 What constprop drops (Slice 1)

An assignment `LHS = RHS` (or an op-result `op(LHS, …)`) is dropped iff:

- LHS is not a register — today checked as `LHS.front() != '#'`; migrates
  to `st.is_reg(LHS)` per `lnast_todo.md` §12.
- LHS is not an input/output port — today `LHS.front() != '$' && != '%'`;
  migrates to `st.is_input` / `st.is_output` per §12.
- LHS's resolved value in the symbol table is a known `Lconst` (not
  `invalid`, no unknowns).
- LHS has no live downstream consumer that isn't itself comptime-
  resolvable (covered implicitly in Slice 1 because every comptime-
  resolvable read folds at its use site — see below).

Regs stay because their assignments carry timing semantics, not just value.
Ports stay because later stages need them in the IR boundary.
`delay_assign` stays unconditionally (per §0 / `lnast_todo.md` §15.2).

A `ref X` operand gets folded to `const <v>` iff `st.get_trivial(X)` is a
known `Lconst`. Temporaries (`___N` today, `Tree_index` tmps post-§13) and
user-named comptime-known vars both qualify — they're interchangeable once
folded. This is what makes the "drop the assignment" rule safe: every
surviving read turns into a literal, so the binding is unreferenced after
emission.

**Absent vs. unknown.** Slice 1 treats `Lconst::invalid()` as "not
comptime-known, emit the ref verbatim". Once §10 lands, the ST will
distinguish "attr never set" (absent) from "value not yet inferred"
(unknown) — upass will need both queries. Note this now so the query API
added in Slice 1 is easy to extend (§ 5.a below).

`cassert X` always emits in Slice 1. The **verifier** upass (separate pass,
runs last) is responsible for:

- `X` known true → drop the cassert.
- `X` known false → hard error (comptime assertion failure).
- `X` unknown → keep the cassert for runtime.

Keeping this split matches the eventual `assume`/`cover` story, which will
live in verifier too.

## 3. Slice boundaries

### Slice 1 (this change) — straight-line constprop+DCE

- Staging tree, single traversal, runner-owned.
- `Emit_decision::drop_subtree` and `fold_to_const` wired from
  `uPass_constprop`.
- `attr_set` **copied verbatim**. Not consumed, not dropped. Slice N will
  move these into a side-map keyed by `lnast_nid`.
- `func_call`, `func_def`: **copied verbatim**. No fan-out into separate
  LNASTs yet.
- Tuples: `tup_add`/`tup_get`/`tup_set` copied verbatim unless the whole
  statement is comptime-known and DCE'd. No flattening.
- `if`: both branches traversed. No dead-branch pruning (requires condition
  to be comptime-known **and** both paths to have equivalent live-outs —
  handled in a later slice).
- Verifier: still passes through cassert. Dropping known-true cassert is
  Slice 2.

Expected behavior on `add_trivial.prp` after Slice 1:

```
const x = 1    → dropped
const y = 2    → dropped
mut c = x + y  → dropped (c comptime = 3)
cassert c == 3 → cassert ___1   (still emitted; ___1 known true in symtab)
```

After Slice 2 (verifier drops known-true cassert): empty `stmts`.

### Slice 2 — verifier resolves cassert at comptime (landed)

- `uPass_verifier::classify_statement` detects `cassert`, reads its single
  operand, and resolves it via the **runner-backed fold callback** (see
  §2.5 below). Disposition:
  - known-true → `Emit_decision::drop()` (assertion discharged).
  - known-false → `upass::error(...)` — propagates, pipeline rc becomes
    non-zero. No strict/warn knob: this is the verifier's job. To opt out
    for a specific pipeline, disable the verifier with
    `pass.upass verifier:false`.
  - unknown (operand still a ref after Slice 1 folding, or a const with
    unknowns) → `emit_node()` — kept for downstream / runtime.
- Pass order stays `verifier → constprop → assert → verifier`. The second
  verifier run sees constprop's populated ST through the fold callback and
  is the one that actually discharges casserts. The first-pass verifier
  sees no ST yet and just passes them through (plus the existing shape
  checks on other ops).

**Test mode split.** `prplib.py` distinguishes two test types that drive
this pipeline:

- `:type: upass` — pipeline smoke test. Uses `pass.upass verifier:false`
  so a false cassert in a case constprop can't handle (enum values,
  tuple indexing, string ops, `__wrap`/`__ubits` attrs, …) doesn't
  regress the suite. Asserts only that the pipeline doesn't crash.
- `:type: comptime` — correctness check. Verifier on (default). Every
  cassert must discharge or be clearly unknown; known-false → test
  fails unless expected.

The list of features not yet foldable by constprop is the natural
migration path: as Slices 3–6 and the value-attr inference pass (§10.2
in `lnast_todo.md`) land, each `:type: upass` test becomes a candidate
for promotion to `:type: comptime`.

### 2.6 Cassert tally / expected counts

The verifier classifies every cassert into one of three buckets during
the run:

| disposition  | trigger                                                        | counter         |
|--------------|----------------------------------------------------------------|-----------------|
| known-true   | operand folds to a non-zero, no-unknowns `Lconst`              | `pass_count`    |
| known-false  | operand folds to zero, no-unknowns                             | `fail_count`    |
| unknown      | operand stays a ref, or const has unknowns                     | `unknown_count` |

A comptime-false cassert is **counted, dropped from the output, and its
source operand printed to stderr** — the actual error is raised later
in `end_run` so tests can assert they expect a specific failure count.

**Expected counts.** Two `pass.upass` labels control the assertion at
`end_run`:

- `verifier_pass:N` — expected number of discharged casserts. Unset
  means "don't check". Mismatch → `upass::error`.
- `verifier_fail:N` — expected number of known-false casserts. Unset
  defaults to **0** (any unexpected failure errors). Mismatch →
  `upass::error`.

Mismatch reports include the breakdown and, if any casserts were
unknown, the list of unresolved operand texts. prplib.py scrapes
`:verifier_pass:` / `:verifier_fail:` from the `.prp` header block and
forwards them, so a test file writes:

```
/*
:type: comptime
:verifier_pass: 3
:verifier_fail: 0
*/
```

and the harness builds `pass.upass ... verifier_pass:3 verifier_fail:0`.
This is the primary lever for turning cassert coverage into a growable
metric as constprop gaps close.

- **Future**: assume/cover have the same shape and will use the same
  classify dispatch with variant-specific semantics.

### 2.5 Runner-backed fold callback

Passes cannot easily query each other's symbol tables directly. Rather
than forcing a shared ST, the runner exposes a `Fold_fn` callback
(`std::function<std::optional<Lconst>(std::string_view)>`) that aggregates
every pass's `fold_ref` via the runner's `try_fold_ref`. After
constructing the upass list, the runner wires the callback into each pass
via `set_runner_fold_fn`. Today it is used by:

- `uPass_verifier::classify_statement` — to resolve cassert operands.

Any future pass that needs to consult cross-pass fold state uses the same
callback. Do not invent parallel symbol tables for this — the fold
callback is the single authority.

### Slice 3 — func_call argument reconstruction

- When a `func_call` is emitted, constprop inspects each arg:
  - Arg is a known scalar → emit `const` in place.
  - Arg is a known tuple (bundle) → emit a fresh `tup_add` with
    const/ref children per field, rooted at the arg slot.
- Return tuple from `func_call`: treated as an opaque named var until a
  downstream `tup_get` or single-field `assign` pulls fields out. When the
  callee's LNAST has been processed, constprop re-seeds the symbol table
  with return-value fields so the caller can continue folding — this is the
  reason for iteration in the multi-module case.

### Slice 4 — func_def extraction

- `func_def` inside an input LNAST → runner allocates a **new** staging
  LNAST for the callee body, registers it in `var.lnasts`, and emits
  nothing in place of the `func_def` node in the caller's tree (or a
  lightweight marker — TBD; must round-trip through `lnast.dump`).
- Each callee runs its own full upass pipeline. N func_defs in 1 input
  LNAST → up to N+1 output LNASTs.

### Slice 5 — attr_set side-map + §11 semantics

Gated on `lnast_todo.md` §10 (ST API) and §11 (unified `attr_set`
shape). Pulls attributes out of the emitted LNAST into a side table the
verifier / downstream consumers read via the ST.

- All `attr_set` nodes consumed; none remain in the emitted tree.
- Side structure: `absl::flat_hash_map<decl_name, Attr_set>` keyed by
  the pre-SSA declaration name. (Not keyed by `Lnast_nid` — attrs belong
  to the declaration, not the node position, per §11's write-once
  semantics.)
- upass **enforces** the §11.2 rules at emission time, because it's the
  single pass that visits everything in source order:
  - **Write-once per `(decl, attr_key)`.** Second `attr_set` ⇒ error.
  - **Comptime-guarded only.** Enclosing `if`/`for`/`while`/`match`
    conditions must be comptime-resolvable; runtime-guarded
    `attr_set` ⇒ error. Reuses the Slice 7 condition evaluator.
  - **No `attr_set` after first read.** Tracks a "in use phase" bit per
    decl; any `attr_set` after that bit is set ⇒ error. "Read" = RHS
    of assign/tuple_set, operator arg, call-site arg, guard, or target
    of `attr_get`.
  - **Values must be comptime-resolvable.** Same symbol-table query the
    Slice 1 classifier uses.
- `attr_get` resolves from the side-map, not the LNAST. Unset key ⇒
  "absent" return (not error); consumer decides meaning.
- Derived `__min`/`__max` do **not** flow through this side-map —
  lnastfmt (§10.2) writes them to the per-SSA ST table directly.
- Requires the HHDS-based LNAST attribute work to land first.

### Slice 6 — tuple flattening

- `tup_add`/`tup_get`/`tup_set` whose bundle is fully comptime-known and
  whose consumers are all comptime-resolvable are removed; references fold
  to field values directly.
- Exception: tuples that cross a `func_call` boundary stay as `tup_add`
  (Slice 3 emission pattern) — they encode ABI.

### Slice 7 — if-branch pruning

- If condition is comptime-known: emit only the taken branch's stmts,
  spliced into the parent stmts block.
- Requires phi-resolution awareness (LNAST already has SSA) so live-outs
  of the removed branch don't leave dangling refs.

## 4. Invariants

These must hold after every slice:

1. **Every ref in the emitted tree is defined** — either by an emitted
   assignment, a port, a register, or it was folded to `const` at emit
   time. No dangling refs.
2. **Regs are never dropped.** Reg assignments carry timing; constprop may
   fold their RHS but not elide the assignment. "Is reg" check: prefix
   today, `st.is_reg` post-§12.
3. **Ports are preserved at the module boundary.** Input ports read, output
   ports written — even if a constant flows through. Same prefix-vs-ST
   migration as regs.
4. **Deterministic emission order.** Children are emitted in input-tree
   order; the staging tree is a topologically-consistent rewrite of the
   input, not a re-scheduled one.
5. **`begin_iteration` is called at the start of every sweep.** Upasses
   that accumulate state (symbol tables) keep it across sweeps; upasses
   that accumulate per-sweep diagnostics must clear in this hook.
6. **`delay_assign` is emitted verbatim.** Offsets ≠ 0/1 are not yet
   lowered (per `lnast_todo.md` §15.2); upass must not constant-fold
   across them regardless of symbol-table state.
7. **No attribute derivation in upass.** `__min`/`__max` and other
   value-level attrs are lnastfmt's responsibility (§10.2). upass reads,
   does not write these. Decl attrs (§11) are written only by `attr_set`
   in the input LNAST; upass validates and moves them to the side-map
   in Slice 5.

## 5. Known gotchas carried forward

- `Symbol_table` keys strip `%`/`$` prefixes in `process_assign` (see
  `upass_constprop.cpp:22`). Any new consumer of the symbol table must use
  the stripped form. Removable once §12 lands (no more prefixes in ref
  text).
- `Bundle::set` interns attr keys as `0.__attr`; `Symbol_table::has_trivial`
  does literal match. Don't round-trip attr set/get through the symbol
  table — Slice 5 side-map supersedes this.
- `process_tuple_add` reuses an existing bundle on re-entry to keep
  pointers stable across iterations. When Slice 3 adds iteration, preserve
  this invariant or fold convergence will regress.
- `if` cursor discipline: every pass's `process_if` must leave the cursor
  at the if-node it entered. See comment at `upass_runner.cpp:278`.
- **`Lconst::invalid()` overload.** Today it stands for "not tracked" and
  "tracked but unknown" — upass treats both as "don't fold". After §10
  introduces "absent" distinctly, revisit every `is_invalid()` call site
  in `upass_constprop.cpp` to make sure it still means "don't fold".
- **`assign` vs `tuple_set` collapse (§11.3).** When §11 lands and
  producers emit `tuple_set a v` instead of `assign a v`, upass's
  classify hook must recognize both shapes (or a single canonical shape
  if `assign` is deleted). Today: only `assign`.

### 5.x Diagnostic channel (future slice)

Open issue deferred from the prp2lnast chained-compare / ambiguity work:

- **`a and b or c` is ambiguous.** The updated tree-sitter-pyrope grammar
  puts both tokens into a single `expression_item` at priority 5
  (`binary_logical_op`). Mixing distinct operators at the same precedence
  level without parens has no defensible associativity. Today prp2lnast
  prints a bare `std::print` line and continues lowering as a left-fold,
  which is *both* wrong and silent in tests. This should become a proper
  compile error attached to the source range of the offending operator.
- **What's missing to make it a real diagnostic:**
  - prp2lnast has no diagnostics channel — errors currently go to stdout
    mixed with tree-sitter-dump output.
  - An accept-this-program invariant lives in the verifier pass, but the
    parser-side ambiguity must reach the user *before* uPass runs.
  - Natural design: plumb a shared `Diagnostics` object (with `.error`,
    `.warn`, source-range support) through `Prp2lnast` and the uPass
    runner; have `pass.upass` refuse to start if the diagnostics contain
    errors.
- **Scope cue.** Also applies to other same-priority mixes the grammar
  permits but the language prohibits: e.g. `a implies b or c` would fall
  under the same rule, and any future mixed-operator tier.

### 5.a ST query API additions expected from Slice 1

To keep Slice 1 self-contained while setting up for future slices, the
staging writer uses a thin adapter over `Symbol_table`:

- `st.is_known_const(name) -> bool` — returns true iff a `Lconst` is
  stored and it has no unknowns. Used by the classifier.
- `st.is_reg(name) / is_input(name) / is_output(name) -> bool` — Slice 1
  implementation: prefix check on the raw name. Post-§12: ST lookup. All
  call sites funnel through these two helpers so the §12 migration is
  a one-file diff.
