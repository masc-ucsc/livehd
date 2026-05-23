# upass — LNAST micro-pass pipeline

`pass.upass` runs a sequence of micro-passes (uPasses) over an LNAST. Its job
is to **propagate compile-time-known values, dead-code-eliminate everything
those values make redundant, and emit a new LNAST** that replaces the input.

This document is the contract for how the rewrite works. When a future change
touches this area, update this doc first.

The full architectural rationale (alternatives weighed, why bundle pre-pass,
why no `max_iter` for the current pass set, etc.) lives in
[docs/upass_redesign.md](../docs/upass_redesign.md). This file is the
day-to-day operational contract; the redesign doc is the rationale.

## 0. Context and scope

upass lives downstream of `pass.lnastfmt` and consumes LNAST produced by
`inou/prp` (new), `inou/pyrope` (legacy, sunsetting), and `inou/slang`.
Several LNAST-wide cleanups in `lnast_todo.md` directly constrain upass:

- **§10 — Symbol-table API + value-attr inference.** lnastfmt owns
  `__min`/`__max` computation via interval arithmetic, keyed per-SSA-version.
  upass **reads** these via the symbol table; it does not derive them.
- **§11 — unified `attr_set`/`tuple_set` shape, write-once semantics.** upass
  enforces write-once / comptime-guarded rules; the side-map for category-D
  attributes lives in the Bundle (§5 below).
- **§12 — `$`/`%`/`#` prefixes → ST-backed direction/storage.** upass assumes
  ref text is already bare; direction/storage carried structurally, not
  encoded in the name.
- **§13 — `Tree_index`-identity tmps.** upass keys the symbol table by name
  regardless; the tmp/named distinction never affects fold logic.
- **§15.2 — `delay_assign` offset extensions.** upass never folds across a
  `delay_assign`; emit verbatim.

Keep section references up to date when those sections renumber.

## 1. Pipeline shape

A single `pass.upass` invocation walks each LNAST in `var.lnasts` exactly
once, depth-first, pre-order. For each source node:

```
(1) bundle pre-pass          ─ once
(2) opt passes               ─ each once, in fixed order
                                (constprop, attributes, bitwidth,
                                 coalescer, func_extract)
                              ─ taint gate: if any operand vector entry
                                carries a non-empty pending_import, ONLY
                                constprop is called; the others skip.
(3) vote resolution          ─ drop > toconst > update > keep
(4) SSA post-pass            ─ once; may expand 1→N (tuple flatten,
                                name versioning, dead-arm delete).
                                Skipped on any node whose read-through
                                Bundles carry pending_import.
(5) emit 0..N nodes          ─ append to the dest LNAST.
```

After the source walk completes:

- If **no** Bundle in the symbol table carries `pending_import`, the
  read-only finishers run a separate walk over the dest LNAST:
  `verifier`, then `assert`.
- Otherwise the finishers defer to a later `pass.upass` invocation
  (presumably after the blocking `import` resolves).

### No max_iter for the current pass set

With direct Bundle mutation in fixed pass order, one sweep per source node
suffices. `max_iter` stays as a configuration knob (default 1) for future
passes that genuinely chain (e.g. two strength-reductions). The current set
does not need it; the runner picks the strongest vote and moves on.

### Re-runs

Each `pass.upass` invocation starts clean: symbol table rebuilt from scratch,
scope stack empty. Re-runs (after another lowering stage, after an `import`
resolves, etc.) get a fresh view.

## 2. Per-pass responsibilities

### Bundle pre-pass (new)

Owns *structural* side state. Writes Bundle fields that describe a name's
shape, type, sticky-attribute taint, pending-import poison, declared
policies (wrap/sat), and aliasing. Never folds a scalar; never computes a
value.

Hooks at: every assignment-shaped op (sticky/pending-import migration),
`type_spec`, `range` (records slot; does not fold start/end), `attr_set`
(records `attr_values[name]` slot and policy bits), `tuple_add` /
`tuple_concat` / `tuple_get` (shape, aliases), `assign` alias form
(direct_alias, shape/sticky migration from RHS Bundle), `if`
(cond-comptime check → mark dead arm), `stmts` (scope push/pop),
`func_def` (function-body scope, closure rule).

### constprop

Owns *value* side state. Folds scalars using operand-vector entries;
populates `Bundle.value` and `Bundle.range`'s start/end constants. Applies
wrap/sat narrowing while writing `Bundle.value`. Votes **toconst** when
LHS is fully comptime, **drop** when the assign would re-store the same
value the Bundle already holds.

constprop is the **only** opt pass that runs when an operand carries
`pending_import` — it folds what it can without depending on
attribute/sticky semantics.

### attributes (shrunken)

Owns `attr_set`/`attr_get` *semantics* only. Structural and sticky work
moved to bundle pre-pass.

- `attr_set`: dispatches to per-attribute handlers (category-A `wrap`/`sat`
  policy already set by bundle pre-pass; this is where `type` consumption,
  category-B clock/reset wiring, etc. happen). Votes **drop** when the
  attribute fully lives in the Bundle now.
- `attr_get`: looks up `Bundle.attr_values[name]`, chains through
  `tuple_get_alias` / `direct_alias` / aggregate inheritance. Votes
  **toconst** when the read resolves.

### bitwidth

Range inference using Bundle's type info + range slot + folded value.
Publishes `bits` / `min` / `max` back into Bundle. Typically votes
**keep**; rare **update** for strength-reduction-style cases.

### coalescer

Deferred-emit / DSE. Reads operand vector; uses Bundle's "consumed" bit
(set by the emission walk for materialized refs) to spot dead writes.
Votes **drop** when a write has no live downstream consumer.

### func_extract

Inline-vs-spawn decision at call sites. Inlinable callees (resolved,
small enough, no recursion blowup): vote **update** with the inline body
as the candidate shape; the runner switches its source-walk cursor into
the body (see §4). Otherwise vote **keep** so the call survives and the
callee body is spawned as a separate LNAST in `var.lnasts` for an
independent `pass.upass` walk. `func_def` with no remaining call sites
and no exported semantics → vote **drop**.

### SSA (post-pass)

- Flattens `tuple_*` aggregates into per-field flat names (uses
  `Bundle.shape` and `tuple_get_alias`).
- Renames multi-assigned source names into source-derived SSA names
  (`TODO_prp.md §1d`).
- Deletes dead arms flagged by bundle pre-pass when cond was comptime.
- Skipped on nodes whose read-through Bundles carry `pending_import` —
  those `tuple_*` / `if` nodes survive to the next invocation.

### verifier / assert (read-only finishers)

Walk the **dest** LNAST after the source walk completes. Do not see
Bundle state. Tally and check from what's emitted:

- Casserts are emitted with their cond simplified (`const(true)`,
  `const(false)`, or surviving `ref...`). Verifier tallies
  pass/fail/unknown by reading the cond's emitted form.
- Comptime-false cassert (`const(false)`) → `upass::error` unless the
  test header expects it (`verifier_fail:N`).
- The cassert tally compares to the test header expectations:
  | disposition  | trigger                                       | counter         |
  |--------------|-----------------------------------------------|-----------------|
  | known-true   | cond is `const_true`                          | `pass_count`    |
  | known-false  | cond is `const_false`                         | `fail_count`    |
  | unknown      | cond is a surviving `ref` or const w/ unknown | `unknown_count` |
- `verifier_pass:N` / `verifier_fail:N` labels on `pass.upass` set the
  expected counts; mismatch raises `upass::error`. `prplib.py` reads
  these from the `.prp` header block (`:verifier_pass:`, `:verifier_fail:`)
  and forwards them.
- assume/cover use the same shape with variant-specific semantics
  (future).

**Test mode split** (driven by `prplib.py`):

- `:type: upass` — pipeline smoke test, `pass.upass verifier:false`.
  Asserts only that the pipeline doesn't crash.
- `:type: comptime` — correctness check, verifier on. Every cassert
  must discharge or be clearly unknown; known-false → test failure
  unless expected.

### Reader passes only on the final invocation

When multiple `pass.upass` invocations exist (e.g. one before an
`import` resolves, one after), verifier/assert attach only to the final
one — driven by the pending-import gate described in §1. If the gate
defers them, they run automatically on the next invocation that finds
the symbol table clean of `pending_import`.

## 3. Symbol table & scopes

```
SymbolTable: stack<Frame>
Frame:       absl::flat_hash_map<string, Entry>
Entry:       variant<Runtime_trivial, Const, shared_ptr<Bundle>>
```

- Every `{ }` (every `stmts` node) pushes a frame on entry, pops on exit.
- Reads walk innermost → outermost.
- Writes land in the top frame; subsequent assigns at the same level
  mutate the same Bundle.
- **If-arm merge**: each arm runs with a fresh frame on top of the
  parent. After all arms processed, frames are merged into the parent:
  - `sticky_taint`, `pending_import`: union.
  - `value`: kept iff every arm wrote the same `Const`. Otherwise drops
    to `Runtime_trivial`.
  - `shape`: identical across arms, else hard error.
  - `attr_values[k]`: union; conflicting `k` drops to unknown.
  - `wrap_policy` / `sat_policy` / `decl_kind` / `num_kind` / `bits`:
    arm-local change is a hard error.
- **General `{ }` (not if-arm)**: pop with no merge — locals vanish.
- **Function-body scope**: pop with no merge. Closure-capture rule on
  reads:
  - Outer entry comptime (Bundle has `value`, no `pending_import`) →
    inline the constant at the use site (operand-vector entry is the
    `Const`).
  - Outer entry tainted (`pending_import` non-empty) → leave the ref;
    defer this function's spawn/opt until next invocation.
  - Outer entry non-comptime, untainted → **hard compile error** at the
    read site. (See `TODO_prp.md §2j` for the enforcement work.)

## 4. Inline (runner-level cursor switch)

func_extract's `update` vote with an inline body becomes a runner
mechanism, not an opt-pass concern. Concretely:

- Runner maintains a stack of `(source-LNAST, cursor, scope_frame)`
  records.
- On `update + inline`: push the inline body's LNAST, switch cursor to
  its first node, open a fresh function-body scope.
- Synthesize `assign param_x = caller_arg_x` nodes at the head of the
  inline body so bundle pre-pass sees them as normal assigns — uniform
  pipeline, no special arg-binding path.
- Opt passes see the inline body's nodes one at a time, identical to any
  other source node. No awareness of being "inside" an inline.
- Inline body is a clone of the callee's source LNAST per call site —
  fresh walk every time so per-call-site context affects folding.
- On body exit: pop frame, close scope, advance the caller cursor past
  the original call node.
- Nested inlines just push more frames.

## 5. Bundle layout (per-name symbol-table entry)

Lazily allocated — symbol-table entries default to `Runtime_trivial`;
constprop alone installs a `Const`; bundle pre-pass or the attribute
pass promotes to `Bundle` when *any* structural field needs a slot.

```cpp
struct Bundle {
  // ── Structural — written by bundle pre-pass ────────────────────────
  Decl_kind     decl_kind  : 4;   // mut / const / reg / await / let / unknown
  Numeric_kind  num_kind   : 4;   // unsigned_int / signed_int / boolean / string / none
  bool          wrap_policy : 1;
  bool          sat_policy  : 1;
  bool          is_comptime : 1;  // mirror of (value && !pending_import)
  uint16_t      bits;             // 0 = unspecified
  uint16_t      assign_count;     // pre-SSA, for const single-assign check
  Tuple_shape   shape;            // empty for scalars

  std::optional<std::pair<Const, Const>>          range;
  absl::InlinedVector<std::string, 1>             sticky_taint;
  absl::InlinedVector<ImportRef, 1>               pending_import;

  // ── Value — written by constprop, narrowed by wrap/sat ─────────────
  std::optional<Const>                            value;

  // ── Attribute values — written by attributes (filled by constprop) ──
  absl::flat_hash_map<std::string, Const>         attr_values;

  // ── Aliasing — written by bundle pre-pass ──────────────────────────
  std::shared_ptr<Bundle> direct_alias;
  std::shared_ptr<Bundle> tuple_get_alias_base;
  std::string             tuple_get_alias_field;
};
```

Per-node `Vote_state` (runner-owned, transient to one source node):

```cpp
struct Vote_state {
  enum class Kind : uint8_t { keep, drop, toconst, update };
  Kind          kind = Kind::keep;
  Lnast_subtree update_shape;  // populated only when kind == update
};
```

## 6. Operand vector & dispatch

The runner pre-resolves each source node's operands into a small vector
of:

```cpp
struct Runtime_trivial {};  // explicit, self-documenting
using Operand     = std::variant<Runtime_trivial, Const, std::shared_ptr<Bundle>>;
using Operand_vec = absl::InlinedVector<Operand, 4>;
```

- Built once per source node by per-op-kind helpers in the runner.
- `ref` child → look up Bundle (or `Runtime_trivial` if absent), then
  `Const` if the Bundle's `value` is set.
- `const` child → stored directly as `Const`.
- LHS is NOT in the operand vector; it is the destination `Bundle*`
  passed separately to dispatch (`nullptr` for ops without one, e.g.
  `cassert`).

Per-op-kind shape rules live in the runner:

- **`process_arith`** (27 expr ops: plus, minus, mult, div, mod, shl,
  sra, bit_{and,or,not,xor}, log_{and,or,not}, red_{or,and,xor}, eq,
  ne, lt, le, gt, ge, sext, get_mask, set_mask, assign-non-alias):
  LHS at child 0, operand vector = siblings 1..N.
- **`attr_set`**: LHS = child 0; vector = `[const(attr_name), value]`.
- **`attr_get`**: LHS (dst tmp) = child 0; vector = `[base, const(attr_name)]`.
- **`tuple_add`**: LHS = child 0; vector entries one per field
  (assign-sub-nodes contribute their key + value pair).
- **`tuple_concat`**: LHS = child 0; vector = N refs/consts.
- **`tuple_get`**: LHS = child 0; vector = `[base, field_key]`.
- **`range`**: LHS = child 0; vector = `[start, end]`.
- **`type_spec`**: LHS = child 0; vector = `[prim_type_node, …]`.
- **`if`**: no LHS; vector = `[cond]`. Arm-body `stmts` are handled by
  runner-level scope push/pop and recursion — not in the vector.
- **`cassert`**: no LHS; vector = `[cond]`.
- **`func_def`**: LHS (function name) = child 0; vector layout TBD.

### Dispatch API

```cpp
struct uPass {
  virtual void begin_iteration() {}
  virtual void end_run() {}

  // Hot path — 27 LNAST kinds, single dispatch.
  virtual Vote process_arith(Lnast_ntype kind, Bundle* dst,
                              const Operand_vec& ops) { return Vote::keep(); }

  // Genuinely-shaped ops (non-uniform child layout).
  virtual Vote process_attr_set    (Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
  virtual Vote process_attr_get    (Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
  virtual Vote process_tuple_add   (Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
  virtual Vote process_tuple_concat(Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
  virtual Vote process_tuple_set   (Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
  virtual Vote process_tuple_get   (Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
  virtual Vote process_range       (Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
  virtual Vote process_type_spec   (Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
  virtual Vote process_if          (                  const Operand_vec& ops) { return Vote::keep(); }
  virtual Vote process_cassert     (                  const Operand_vec& ops) { return Vote::keep(); }
  virtual Vote process_func_def    (Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }

  // Scope hooks — runner-driven, no per-node vote.
  virtual void process_stmts_enter() {}
  virtual void process_stmts_exit () {}
};
```

Verifier / assert do **not** derive from `uPass`; they walk the dest
LNAST after emission and own their own simple iterator.

## 7. Vote resolution

For each source node (no-taint case), after every opt pass has run:

```
priority: drop > toconst > update > keep
```

- Any `drop`     → emit nothing.
- Else any `toconst` → emit nothing; `Bundle.value` is committed.
  Downstream consumers materialise via the operand vector.
- Else any `update` → emit the proposed shape from the latest `update`
  vote's `update_shape` (pass order is the tiebreaker; for inline this
  is func_extract's body).
- Else (all `keep`) → emit the source op-kind with operand-vector
  entries materialised:
  - `Const` operand → emit as `const`.
  - `Bundle` operand with `value` set → emit as `const(value)`.
  - `Bundle` operand without `value` → emit as `ref(name)`.
  - `Runtime_trivial` operand → emit as `ref(name)`.

All opt passes run unconditionally in the no-taint case — even if an
earlier pass already voted drop — because later passes may still write
useful state to the destination Bundle (other source nodes will read
it). Vote priority only governs what gets *emitted* for this node.

**Taint case**: when any operand vector entry's Bundle carries
non-empty `pending_import`, only `constprop` runs; its vote (default
`keep`) governs alone. SSA also skips this node.

## 8. Pending-import poison

A Bundle whose value or attribute state depends on an unresolved
`import` carries one or more `ImportRef`s in `pending_import`. The
marker is sticky-style: bundle pre-pass propagates it from operand
Bundles into LHS Bundle through `process_arith` and every assignment-
shaped op, the same path used for `sticky_taint`.

Effect on each phase:

- **Bundle pre-pass**: records structure normally; flags LHS pending if
  any operand is.
- **Opt passes**: only `constprop` runs on tainted-input nodes.
- **SSA**: skips structural rewrites (tuple flatten, dead-arm delete)
  on any node whose read-through Bundles include a pending marker.
- **Verifier / assert**: skipped entirely if any Bundle still carries
  `pending_import` at end of the source walk.
- **Re-runs**: the next `pass.upass` invocation rebuilds the symbol
  table; cleared pending markers (because the import has now resolved)
  let the passes run.

`TODO_prp.md §1b` and `§2j` track the import-side work this hooks into.

## 9. Invariants

These must hold after every walk:

1. **Every ref in the dest LNAST is defined** — either by an emitted
   assignment, a port, a register, or it was folded to `const` at emit
   time. No dangling refs.
2. **Regs are never dropped.** Reg assignments carry timing; constprop
   may fold their RHS but not elide the assignment.
3. **Ports are preserved at the module boundary** — input ports read,
   output ports written — even when a constant flows through.
4. **Deterministic emission order.** Children are emitted in source-tree
   order; the dest LNAST is a topologically-consistent rewrite of the
   source, not a re-scheduled one.
5. **`delay_assign` is emitted verbatim.** Offsets ≠ 0/1 are not yet
   lowered (per `lnast_todo.md` §15.2); upass must not constant-fold
   across them regardless of symbol-table state.
6. **No value-attribute derivation in upass.** `__min`/`__max` and other
   value-level attrs are lnastfmt's responsibility (§10.2). upass reads
   them, does not write. Decl attrs (§11) live in `Bundle.attr_values`
   (written by bundle pre-pass / constprop / attributes per §2).
7. **Spawned LNASTs are self-contained.** Outer-scope comptime values
   are inlined at the use site by bundle pre-pass / constprop before
   func_extract spawns the callee body; spawned LNASTs share no Bundle
   state with their caller.
8. **Single sweep per source node** in the current pass set. `max_iter`
   defaults to 1; raising it is for forward-looking chained-rewrite
   passes that don't exist yet.

## 10. Known gotchas

- **Canonical bare ref text.** Symbol table keys assume `$`/`%`/`#`
  prefixes are already removed. Legacy prefixes from `inou/pyrope` are
  producer bugs, not alternate spellings.
- **`Const::invalid()` overload.** Today it stands for both "not
  tracked" and "tracked but unknown". After `lnast_todo.md §10`
  introduces "absent" distinctly, revisit every `is_invalid()` call
  site in constprop to confirm it still means "don't fold".
- **`assign` vs `tuple_set` collapse (§11.3).** When §11 lands and
  producers emit `tuple_set a v` instead of `assign a v`, the runner's
  per-op-kind helpers must recognise both shapes (or the canonical
  one if `assign` is deleted).
- **Cursor discipline at `if`.** All cursor movement is runner-owned
  via the operand-vector helpers; passes never call `move_to_child` /
  `scan_op` directly. Any pass that does is a bug — it bypasses the
  vector-resolution layer.
- **Bundle direct_alias chains.** Reading through `direct_alias` is
  one hop; chains (`c = b; b = a;` then `c.[size]`) walk multiple
  hops. The bundle pre-pass keeps the chain shallow by collapsing on
  the alias creation, but consumers should still tolerate one or two
  hops.

### 10.x Diagnostic channel (future)

Open issue deferred from prp2lnast chained-compare / ambiguity work:

- **`a and b or c` is ambiguous.** Mixing distinct operators at the
  same precedence level without parens has no defensible
  associativity. Today prp2lnast prints a bare `std::print` line and
  continues lowering as a left-fold, which is wrong and silent in
  tests. Needs a proper compile error attached to the source range.
- **What's missing**: prp2lnast has no diagnostics channel; an
  accept-this-program invariant lives in verifier; parser-side
  ambiguities must reach the user before upass runs.
- **Natural design**: plumb a shared `Diagnostics` object (with
  `.error`, `.warn`, source-range support) through `Prp2lnast` and the
  uPass runner; have `pass.upass` refuse to start if diagnostics
  contain errors.
- **Scope cue**: also applies to `a implies b or c` and any future
  mixed-operator tier the grammar permits but the language prohibits.

## 11. Migration from the old runner

The previous design (staging tree, per-node `max_iter` step loop,
`classify_before_emit` hooks, 27 separate virtual methods per arithmetic
op, parallel side tables in each pass) is being replaced incrementally:

1. **`Handler_registry` for_each_handler micro-fix** — landed as the
   benchmark-driven cleanup (`std::set` per-call → pre-built vector).
2. **`Cursor_state` lightweight save/restore** — landed (depth-only
   snapshot in place of `std::stack` copy).
3. **Bundle pre-pass** — to land: take over structural side state from
   `upass/attributes` (sticky propagation, type_info, range_bounds,
   tuple_shapes, wrap/sat policy, assigned_once).
4. **Operand-vector dispatch** — to land: runner pre-resolves operands;
   `process_*` hooks change signature to `(dst, ops)`. The 27 arithmetic
   wrappers collapse to one `process_arith(kind, dst, ops)` virtual.
5. **Vote resolution** — to land: replace `Emit_decision` enum with the
   four-way `Vote` (drop/toconst/update/keep) and the priority rule.
6. **Inline as runner-level cursor switch** — to land: func_extract's
   `update + body` triggers a source-walk push, not a structural rewrite.
7. **Pending-import poison** — to land alongside `TODO_prp.md §1b`.
8. **Verifier/assert as dest-walk finishers** — to land: drop the
   per-node verifier dispatch, run after the source walk completes when
   the pending-import gate is clear.

See [docs/upass_redesign.md](../docs/upass_redesign.md) for the
end-to-end design these steps converge to.
