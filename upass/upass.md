# upass — LNAST micro-pass pipeline

`pass.upass` runs a sequence of micro-passes (uPasses) over an LNAST. Its job
is to **propagate compile-time-known values, dead-code-eliminate everything
those values make redundant, and emit a new LNAST** that replaces the input.

This document is the contract for how the rewrite works. When a future change
touches this area, update this doc first.

The full architectural rationale (alternatives weighed, why bundle pre-pass,
why a single walk per invocation, etc.) lives in
[docs/upass_redesign.md](../docs/upass_redesign.md). This file is the
day-to-day operational contract; the redesign doc is the rationale.

## 0. Context and scope

upass lives downstream of `pass.lnastfmt` and consumes LNAST produced by
`inou/prp` (new), `inou/pyrope` (legacy, sunsetting), and `inou/slang`.
Several LNAST-wide cleanups in `lnast_todo.md` directly constrain upass:

- **§10 — Symbol-table API + value-attr inference.** Value ranges
  (`max`/`min`) are **owned by the standalone `bitwidth` finalization
  pass** (§2 "bitwidth", §8), which derives them by walking the
  optimized LNAST after the runner. This supersedes the older plan in
  which `lnastfmt` computed `__min`/`__max` via interval arithmetic; the
  iterative opt passes neither derive nor depend on ranges (invariant
  #6). See `docs/contracts/lnast_spec.md §10.2`, now redirected here.
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
                                (constprop, attributes, coalescer,
                                 func_extract).  bitwidth is NOT here —
                                 it is a post-walk finalization pass (§2).
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

After the source walk completes, a **finalization phase** runs over the
freshly-built dest LNAST. It is gated per-LNAST (§8):

- **Gate A — unresolved `pending_import`.** If any Bundle still carries
  `pending_import`, finalization is **deferred entirely** to a later
  `pass.upass` invocation (after the blocking `import` resolves). None of
  the steps below run — running SSA/bitwidth under an unresolved import
  is wasted work (it must be redone once the import resolves) and risks
  import-resolution-order non-determinism.
- **Gate B — generic (un-inlined) function body.** A function body whose
  inputs have no declared type has no width envelope and no concrete
  operand ranges, so it cannot be SSA-named, range-analyzed, or lowered
  on its own. It is **skipped** here (for that LNAST only; its
  fully-typed siblings proceed). It is realized solely by inlining into a
  caller, where the call site supplies concrete argument types and the
  inlined copy is range-analyzed in the caller. A generic callee that
  survives un-inlined at a real call site is a compile error — "generic
  callee must be inlined" — per `docs/contracts/lnast2lgraph.md`.

When neither gate fires, finalization runs in order:

  1. **SSA rename/flatten** — rename multi-assigned names and flatten
     `tuple_*` aggregates to per-field flat names. This is the second
     half of the split `uPass_ssa`; the first half (I/O-metadata harvest)
     runs *before* the runner because the comb-call inliner needs
     `io_meta` mid-walk (see §2).
  2. **bitwidth** — the standalone, **read-only** range pass (§2):
     derive each result's exact `max`/`min`, check declared-type
     envelopes (compile error on *provable* overflow), and publish
     `max`/`min` as per-node HHDS tree attrs for `lnast_to_lgraph`.
  3. **read-only finishers** — `verifier`, then `assert`, walk the dest
     LNAST and tally casserts.
  4. handoff to `pass.lnast_to_lgraph`, which reads the `max`/`min` tree
     attrs at node creation to set bits/sign.

### No iteration loop

With direct Bundle mutation in fixed pass order, one sweep per source node
suffices. The runner does exactly one walk per `pass.upass` invocation — there
is no iteration knob and no fixed-point loop. The runner picks the strongest
vote at each node and moves on.

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

### bitwidth (standalone finalization pass — not an iterative opt pass)

`bitwidth` is **not** part of the per-node opt-pass walk and does **not**
participate in const-folding (no `fold_ref`). It runs once, in the
finalization phase (§1), over the optimized dest LNAST after SSA
rename/flatten, gated per §8.

- **Read-only.** It never rewrites nodes and never alters declared types
  (`prim_type_int(max,min)`); it only *reads* the tree and *annotates*
  it.
- **Exact `max`/`min` only.** Each result's range is an exact
  arbitrary-precision (`Dlop`) `[min, max]` — no `+inf`/`-inf` flags
  (Dlop is unlimited precision, so finite bounds never overflow; a value
  with no derivable bound is simply *absent*). `bits` and `signed` are
  **derived on demand from `max`/`min`**, never stored.
- **Output = per-node HHDS tree attrs.** It attaches each result's
  `(max, min)` as an HHDS `flat_storage` tree attribute keyed by the
  result node — a persistent node→data map, distinct from Pyrope
  attributes — which `lnast_to_lgraph` reads at node creation to set
  bits/sign. This replaces the old name-keyed `lnast->bw_meta()` side
  map (which had no consumer and could not survive SSA renaming).
- **Overflow = compile error, but only when provable.** For a typed
  target, error iff the result's range *provably* exceeds the declared
  envelope and the write carries no `wrap`/`sat` policy. An unknown /
  unbounded result into a typed target is **not** an error — the
  declared type is the width. Errors go through the `core/diag` channel
  (stable `code`, `category=source`, source span; collect-and-continue).
- **Declared envelope is itself `max`/`min`.** `uN` lowers to `min>=0` +
  a max; `sN`/`bool` to a signed range; the runner already emits the
  canonical `prim_type_int(max,min)` and never drops it
  (`upass_runner.cpp` — type_spec/attr_set "emit verbatim, never drop"),
  so the envelope survives into the tree bitwidth reads.
- **Wrap/sat narrowing math lives here** (moved out of `attributes`,
  which keeps only the *policy* bit; see
  `upass/attributes/upass_attributes_wrap_sat.cpp`, math now under
  `//upass/bitwidth`).
- **Lattice completeness.** For the overflow check to be both sound and
  free of false positives, range ops must be tightened where derivable
  (`a/d ≤ a`, `a%d < d`, `sext`, `set_mask`); ops that return
  `unbounded` today are a known follow-up.

> **Status (2026-05-31):** Goal 1n N1–N5 landed (green, net-neutral).
> `bitwidth` is out of the runner order and out of `fold_capable_passes` (no
> `fold_ref`); `pass.upass` runs it standalone after the runner + SSA, read-only
> (discards its staging tree). The lattice/`BitwidthEntry` dropped the
> `neg_inf`/`pos_inf` flags for a single `unbounded` flag (no half-bounded);
> `div`/`mod`/`sext` ranges are tightened; the unsatisfiable-envelope case now
> emits a `core/diag` `bitwidth-overflow` error. Two residuals (TODO_livehd 1n):
> bounds are still int64 (exact-`Dlop` swap deferred — no >64-bit path, and
> `lnast.hpp` stays `Dlop`-free) and the channel is still the name-keyed
> `bw_meta` member (per-node re-keying waits on the `lnast_to_lgraph` consumer);
> the fuller typed-store overflow check waits on a `process_declare` + wrap/sat
> policy read (overlaps 1t T6).

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

### SSA (split: pre-runner harvest + post-runner rename/flatten)

SSA does two jobs with different timing requirements, so it is split:

- **I/O-metadata harvest — *before* the runner.** Harvests port
  names/widths into `lnast->io_meta()`. Must run before the runner
  because the comb-call inliner reads `io_meta` mid-walk
  (`upass_runner.cpp`). It is also how Gate B (§8) is detected: an input
  with unknown width (`bits==0`) marks a generic function body.
- **Rename + flatten — *after* the runner, in finalization (§1).**
  Renames multi-assigned names to SSA-unique names, flattens `tuple_*`
  aggregates into per-field flat names (uses `Bundle.shape` and
  `tuple_get_alias`), and deletes dead arms flagged when a cond was
  comptime. Runs only when neither §8 gate fires, immediately before
  `bitwidth`, so bitwidth and `lnast_to_lgraph` see the final
  single-assignment names.

> The long-term redesign (`docs/upass_redesign.md` Step I) folds the
> rename/flatten half into the runner's *emit* step (so the dest LNAST is
> already SSA-named). Either mechanism satisfies the same invariant: SSA
> naming is complete **before** bitwidth, on the tree `lnast_to_lgraph`
> consumes.

### Typesystem (handled inside `attributes`)

upass owns Pyrope's basic typesystem. The rules are **declaration-
driven**: a value's size envelope and nominal identity come from the
type that was declared on the binding (or implied by a literal), never
from the value itself. There is no separate type-check pass; the
attributes pass reads the type info as it walks and folds the relevant
attribute queries (`bits`, `max`, `min`, `typename`, `size`, `comptime`,
`is`).

What upass guarantees, in plain terms:

- **Size envelope (`bits` / `max` / `min`)** is determined by the
  declared type:
  - `:uN` / `:sN` set width and range; `:bool` is the 1-bit signed
    envelope (`min=-1, max=0, bits=1`); `:string` has no numeric
    envelope (reads as `nil`).
  - When only `max`/`min` (or `range=lo..=hi`) is pinned, `bits` is
    derived on demand from those bounds.
  - **Unannotated → all three read as `nil`.** No value-based
    fallback. A bare `true`/`false` literal is auto-typed `:bool`.
- **Aggregates have no `.[bits]`.** Only scalars do. A tuple-typed
  variable reads `.[bits] == nil`; per-field `t.a.[bits]` resolves
  when the field carries a typed annotation.
- **`size` is cardinality, not bit width.** `tup.[size]` /
  `arr.[size]` is the entry count; `str.[size]` is the character
  count.
- **`typename`** is a debug/diagnostic attribute (not behavior-
  affecting). It's set for named types declared via `type Name = …`;
  anonymous tuples have no typename. Structural `==` ignores
  `typename`; nominal identity is the job of `is`.
- **`is`** folds at compile time when the LHS carries a `typename`
  attribute: `p is Foo` is true iff `p.[typename] == "Foo"`.
- **Comptime is non-sticky on copy.** A `mut` binding that happens
  to hold a known value does *not* read as comptime; only `const`-
  decl (with resolved value) or an explicit `comptime` marker does.

Implementation notes (high level — see `upass/attributes/` for code):

- The attributes pass attaches type info to each variable as
  `Type_info { decl_kind, num_kind, bits, is_comptime, has_type_spec }`.
  `has_type_spec` is set when the LNAST traversal sees either a
  `type_spec` node or an attr_set that pins a width (`ubits`/`sbits`).
- `derive_max` / `derive_min` / `derive_bits` are pure functions of
  the stored type info plus any explicit `max`/`min`/`range`
  attr_sets. They fold to `nil` when no envelope can be derived;
  the runner emits `const(nil)` so consumers compare cleanly.
- Per-field types on tuple literals (`(a:u4=3, …)`) lower as
  `tuple_get` + `attr_set("ubits"/"sbits", N)` against a tmp;
  `lookup_type_info` walks the `tuple_get_alias` and `direct_alias`
  chains so `t.a.[bits]` finds the right entry. `migrate_alias`
  mirrors the per-field type info onto `lhs.field` paths on
  assignment.

Residual follow-ups (deferred; not blocking the current invariants):

- **Variable-as-type** (`mut b:a_var = (…)` borrowing field defaults
  and field-level `mut`/`const` from a tuple variable `a_var`) is
  not wired — needs the type-bundle deep-copy / shared-immutable-
  bundle path from the design.
- **`type Name = (…)` field metadata propagation.**
  `process_type_statement` is still a stub; the RHS tuple isn't
  walked to populate `Name.a`/`Name.b` type info, so
  `g:Name = (a=…, b=…)` doesn't carry field types onto `g.a`/`g.b`.
- **Field-level `const` enforcement on assignment** to a `mut`-outer
  tuple is not raised as a compile error yet.
- **`does` / `equals` / `case`** operators have no fold hooks; only
  `is` lands today.
- **Nil-overwrite-is-not-a-write** (allow `const x:u8 = nil; x = 42`)
  isn't tracked.

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
- **Finalization (Gate A)**: the entire finalization phase (§1) — SSA
  rename/flatten, **bitwidth**, verifier/assert, and the handoff to
  `lnast_to_lgraph` — is skipped if *any* Bundle still carries
  `pending_import` at end of the source walk. The whole program defers to
  a later invocation; see §1 for why (wasted rework + non-determinism).
- **Generic function bodies (Gate B)**: independent of imports, a
  function body whose inputs have no declared type is removed from
  finalization for *that LNAST only* (fully-typed siblings proceed). It
  is realized solely by inlining into callers; an un-inlined survivor at
  a real call site is a compile error.
- **Re-runs**: the next `pass.upass` invocation rebuilds the symbol
  table; cleared pending markers (because the import has now resolved)
  let finalization run.

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
6. **The iterative opt passes do not derive or depend on value ranges.**
   `max`/`min` are derived exclusively by the standalone `bitwidth`
   finalization pass (§2), after the opt-pass walk, and are consumed only
   by `lnast_to_lgraph`. constprop/attributes/coalescer neither read nor
   write ranges, and nothing folds on a range (bitwidth has no
   `fold_ref`). Decl attrs live in `Bundle.attr_values` (written by
   bundle pre-pass / constprop / attributes per §2). This supersedes the
   older "lnastfmt owns `__min`/`__max`" rule.
7. **Spawned LNASTs are self-contained.** Outer-scope comptime values
   are inlined at the use site by bundle pre-pass / constprop before
   func_extract spawns the callee body; spawned LNASTs share no Bundle
   state with their caller.
8. **Single sweep per source node.** The runner does exactly one walk per
   invocation; there is no iteration knob or fixed-point loop.

## 10. Known gotchas

- **Canonical bare ref text.** Symbol table keys assume `$`/`%`/`#`
  prefixes are already removed. Legacy prefixes from `inou/pyrope` are
  producer bugs, not alternate spellings.
- **`Const::invalid()` overload.** Today it stands for both "not
  tracked" and "tracked but unknown". After `lnast_todo.md §10`
  introduces "absent" distinctly, revisit every `is_invalid()` call
  site in constprop to confirm it still means "don't fold".
- **`assign` and `tuple_set` are sibling write ops (§11.3).** Per the
  LNAST spec, `assign root v` and `tuple_set root p1..pN v` are kept as
  distinct node kinds, but both are *writes through the root*: any
  semantic check a pass runs on the LHS of `assign` (const-rebind tally,
  type/bitwidth propagation, wrap/sat narrowing, type-mixing rejection)
  must also fire from `process_tuple_set` on the same root. Producers
  freely choose between the two — `a.b = 1` lowers to
  `tuple_set a b 1`, not `assign a.b 1` — so any check living only on
  the `assign` dispatch path is silently bypassed. Concrete rules for
  new upasses:
  1. If your pass overrides `process_assign`, also override
     `process_tuple_set`. The terminal-value child is the RHS; path
     elements identify the slot inside the root.
  2. Nil-rvalue (`assign x nil` / `tuple_set t p... nil`) is
     invalidation, not a binding — exclude both from bind tallies.
  3. The current canonical example is `uPass_attributes::process_tuple_set`
     (`upass/attributes/upass_attributes_tuple.cpp`), which calls
     `record_assign(root, rhs_is_nil)` so the const-single-bind check
     catches `tuple_set` writes (TODO_prp 1e). Mirror that pattern.
  4. Known still-asymmetric pass: `pass_lnastfmt` validates `assign`
     arity but not `tuple_set` arity (≥2 children: root + value). Fix
     when you touch it. (`upass_bitwidth` is being reworked into a
     read-only whole-tree finalization pass — §2 — that observes both
     write forms uniformly during its own walk, so its old "no
     `process_tuple_set`" asymmetry goes away with the relocation.)
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

The previous design (staging tree, per-node iteration step loop,
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
9. **bitwidth → read-only finalization pass** — to land. Concretely:
   (a) remove `bitwidth` from the runner's pass list and from
   `fold_capable_passes` (delete `overrides_fold_ref`/`fold_ref` on
   `uPass_bitwidth`); (b) run it as a standalone whole-tree pass in the
   finalization phase (§1), after SSA rename/flatten, behind both §8
   gates; (c) replace the name-keyed `lnast->bw_meta()` flush with
   per-node HHDS `(max,min)` tree attrs; (d) drop the `neg_inf`/`pos_inf`
   lattice flags — store exact `Dlop` `max`/`min`, absent when unbounded;
   (e) promote the unsatisfiable-constraint `Pass::warn`
   (`upass_bitwidth.cpp`) to a `core/diag` compile error fired only on
   *provable* envelope overflow (unless `wrap`/`sat`); (f) tighten the
   `div`/`mod`/`sext`/`set_mask` lattice entries so the check is sound
   without false positives. `pass/bitwidth` (the LGraph-level MIT pass)
   is untouched — it remains the Verilog-ingress fallback.

See [docs/upass_redesign.md](../docs/upass_redesign.md) for the
end-to-end design these steps converge to.
