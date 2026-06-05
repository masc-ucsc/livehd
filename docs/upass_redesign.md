# `pass.upass` Redesign — Implementation Plan

Pending work to land the redesigned upass pipeline. Pieces already in
place from prior sessions (scaffolding types in `upass/core/upass_bundle.hpp`,
the pre-Bundle `Op_summary` plumbing in the runner, hot-path gates inside
`upass/attributes`, `overrides_fold_ref` / `overrides_classify_statement`
dispatch filters) are **not** part of this plan — most of them will be
removed or replaced as the steps below land.

This document is the working order-of-operations. Each step states what
to do, which files it touches, and what must remain green
(`bazel test -c dbg //inou/prp:all //upass/...` at the baseline 11
failing tests).

---

## 0. Target architecture (one paragraph)

A single LNAST traversal owned by the runner. The runner maintains a
symbol table (stack of scope frames) keyed by name, whose entries are
`Runtime_trivial | Const | shared_ptr<lnast::Bundle>`. For each source
op node, the runner builds an operand vector of those entries (one per
RHS child) plus a `Bundle*` for the LHS, and hands them to each pass in
turn. Each pass mutates the LHS Bundle (writing fields or attributes
into `lnast::Bundle`'s two maps) and returns a `Vote`
(`keep` / `drop` / `toconst` / `update`). The runner reduces votes by
priority (`drop > toconst > update > keep`), emits 0..N nodes into a
fresh dest LNAST that lives in a new forest (`lgdb/optimized`), and
replaces the entry in `Eprp_var::lnasts`. Verifier and assert run as
read-only finishers walking the dest LNAST after the source walk
completes, gated by `pending_import`. No iteration loop; one walk per
invocation. `func_extract` collapses into runner machinery
(`fdef` extracts the body into a new LNAST with closure-captured outer
Consts; `fcall` inlines by switching the cursor into that LNAST).

---

## 1. Pipeline (per `pass.upass` invocation, per source LNAST)

```
runner walks source LNAST depth-first, pre-order:
    for each source node:
        (0) build operand vector + resolve LHS Bundle*
        (1) bundle pre-pass     (one pass, runs first)
        (2) opt passes          (constprop, attributes, coalescer)
                                (if any operand has pending_import:
                                 only constprop runs; others skip)
        (3) vote resolution     (drop > toconst > update > keep)
        (4) emit 0..N nodes into the dest LNAST
                                (tuple expansion + name versioning live
                                 here, both gated on pending_import)

after the source walk completes:
    if no Bundle in the symbol table has pending_import:
        verifier walks the dest LNAST
        assert walks the dest LNAST
    else:
        verifier/assert defer (next pass.upass invocation runs them)
```

`func_extract`/`fcall` cursor moves and `fdef` extraction are runner
mechanics (see §9). They are not opt passes.

> **Revised (2026-05):** `bitwidth` is no longer an opt pass — it moved
> to a standalone **read-only finalization pass** that runs *after* the
> source walk and *after* SSA rename/flatten, gated on `pending_import`
> and on generic (un-inlined) function bodies (no `fold_ref`; exact
> `Dlop` `max`/`min`, no `+inf`/`-inf`). Authority: `upass/upass.md` §2
> "bitwidth" + §8. Step I below (runner-owned SSA) is orthogonal: SSA
> naming — whether a separate pass or folded into the runner's emit —
> must complete *before* bitwidth, on the tree `lnast_to_lgraph`
> consumes.

---

## 2. Step-by-step plan

### Step A — Tear down stale scaffolding

**Why first**: keeps the search space honest. Anything we add should be
on top of the real `lnast::Bundle` and the real runner, not the
parallel scaffolding from earlier exploration.

**Do**:

1. Delete `upass/core/upass_bundle.hpp`. Remove its `#include` from
   `upass/core/upass_core.hpp`.
2. Remove `Op_operand` / `Op_summary` / `runner_op_summary` / related
   plumbing from `upass/core/upass_core.hpp`, `upass/runner/upass_runner.{hpp,cpp}`,
   `upass/attributes/upass_attributes*.cpp`, `upass/bitwidth/upass_bitwidth.cpp`,
   `upass/coalescer/upass_coalescer.cpp`, `upass/constprop/upass_constprop.{hpp,cpp}`.
   The fast-path branches consuming `runner_op_summary` go away; the
   cursor-based fallbacks stay for now.
3. Remove `overrides_fold_ref` / `overrides_classify_statement` and the
   `fold_ref_passes` / `classify_statement_passes` precomputed vectors
   from the runner. Old `try_fold_ref` / `any_pass_drops` loops over
   `upasses` directly.
4. Keep the existing attribute-pass hot-path gates (record_assign
   short-circuit, alias-block skip, sticky empty-state skip, tmp_fold
   empty-erase guard). They are correctness-preserving and shrink the
   diff that the real redesign has to chase.

**Test invariant**: still 11 failing tests in
`bazel test -c dbg //inou/prp:all //upass/...`.

---

### Step B — `lnast::Bundle` two-map split

**Why**: every later step writes per-name state into the Bundle. The
existing single `key_map` with `__attr` prefixes is a leftover from
when fields and attrs shared one map. Splitting them removes the `__`
namespacing dance from every reader/writer.

**Do**:

1. In `lnast/bundle.hpp`:
   - Replace `mutable Key_map_type key_map;` with two maps:
     `mutable Key_map_type fields;` (named/positional tuple entries +
     the scalar at `"0"`) and `mutable Key_map_type attrs;`
     (attribute name → value, with no `__` prefix).
   - Update `Canonical_less` if it special-cased `__` ordering —
     attributes now sort in their own map by plain name.
   - Update `entries()` / `non_attr_entries()` / `get_attrs()` /
     `top_levels()` / counts: `entries()` returns the merged view if
     anyone needs it; `non_attr_entries()` walks `fields`;
     `get_attrs()` walks `attrs`.
2. In `lnast/bundle.cpp`:
   - Migrate every `key_map.find` / insertion site. `set(key, …)` with
     a key whose first segment starts `__` routes to `attrs` (strip
     the `__`); other keys route to `fields`. Add wrappers
     `set_attr(name, val)` / `get_attr(name)` / `has_attr(name)` for
     the new direct-attr API.
   - `normalize_key` no longer accepts `__name` for attrs — callers
     either use `set_attr("name", …)` (preferred) or the legacy form
     (deprecation-warn, then remove once callers are migrated).
3. Audit and convert in-tree callers:
   - `upass/constprop/upass_constprop*.{hpp,cpp}` — uses
     `bundle->set(...)` / `bundle->get_trivial(...)` / `bundle->set(name, sub)`.
     The data-side (positional/named) keeps working unchanged; any
     attribute access (none today inside constprop's own code) goes
     through `set_attr`/`get_attr`.
   - `lnast/bundle_test.cpp` — any `__*` keys become explicit `attrs`.
   - LGraph generation (`pass/lnastfmt`, `pass/lnast_to_lgraph`): same
     audit; cat-D attributes that ride through here come from the
     `attrs` map.

**Test invariant**: 11 failing tests. (The split is a refactor; no
semantics change.)

---

### Step C — Symbol table owned by the runner

**Why**: every downstream step assumes a single `lnast::Bundle` per
name, mutated in place. Today this state is spread across
`uPass_constprop::st` (a `Symbol_table` of bundles), attributes' 9
side maps, bitwidth's `range_map_`, etc. Consolidate now.

**Do**:

1. Reuse the existing `Symbol_table` class that constprop uses (it
   already holds `shared_ptr<Bundle>` per name with a scope frame
   stack). Move it from `upass/constprop/` to `upass/core/` so all
   passes link against the same definition. (If the constprop class
   has constprop-specific helpers, split them out.)
2. Give `uPass_runner` a `upass::Symbol_table st;` member. Wire
   `process_stmts` to push a frame on enter and pop on exit. Wire
   if-arm merge in `process_if` per §3 below.
3. Add to the `uPass` base class: `set_runner_symbol_table(upass::Symbol_table*)`,
   set once at construction (parallel to today's `set_runner_fold_fn`).
   `uPass_constprop`'s private `st` becomes a `const Symbol_table*` view
   (zero copies; still passes its tests).
4. Migrate constprop, attributes, bitwidth, coalescer to read/write
   the shared `st` instead of their private maps. Do this conservatively:
   keep the old side maps alive too at first; have writes mirror into
   both. After each pass migrates, delete its private map.

**Test invariant**: 11 failing tests.

**Note**: this is the largest mechanical refactor. Land it in one focused
commit per pass (constprop first since it already uses `Symbol_table`).

---

### Step D — Scope rules (block, if-arm merge, function-body)

**Why**: the existing scope handling is enough for `trivial_if` to pass
(blocks scope `mut d = 3` correctly today), but it is not centralised in
the runner — passes manage scope themselves. The redesign wants the
runner to own all scope transitions.

**Do**:

1. In `uPass_runner::process_stmts`, push a frame on `st` before
   walking children, pop on exit. Provide `process_stmts_enter()` /
   `process_stmts_exit()` callbacks for opt passes that need to observe
   (rename today's `process_stmts` / `process_stmts_post` to these).
2. In `uPass_runner::process_if`, run each arm with a fresh top frame
   pushed; after all arms processed, merge into the parent per these
   per-key rules:
   - `attrs[sticky_*]`, `attrs[pending_import]`: union.
   - Scalar field (`"0"`): kept iff every arm wrote the same Const;
     otherwise dropped (entry becomes `Runtime_trivial`).
   - Tuple shape (`fields["name"]` / `fields["0"]` / …): identical
     across arms or hard error.
   - Generic `attrs[k]`: union; conflicting `k` drops to "unknown"
     (remove the key from the parent).
   - `attrs[type]`, `attrs[wrap]`, `attrs[sat]`, `attrs[bits]`,
     `attrs[ubits]`, `attrs[sbits]`: arm-local change is a hard error.
3. General `{}` (non-if-arm): pop with no merge — locals vanish.
4. Function-body scope (handled in Step F below, where extraction lands).

**Test invariant**: 11 failing tests. `prp-trivial_if` continues to pass.

---

### Step E — Operand vector + new dispatch API

**Why**: this is the redesign's primary structural change. After this,
no opt pass walks LNAST children itself; the runner pre-resolves
everything.

**Do**:

1. In `upass/core/upass_core.hpp`, define:

   ```cpp
   namespace upass {
     struct Runtime_trivial {};
     using Operand     = std::variant<Runtime_trivial, Const, std::shared_ptr<lnast::Bundle>>;
     using Operand_vec = absl::InlinedVector<Operand, 4>;

     enum class Vote_kind : uint8_t { keep, drop, toconst, update };
     struct Vote {
       Vote_kind   kind = Vote_kind::keep;
       Lnast_subtree update_shape;  // populated only for kind == update
       Const         toconst_value; // populated only for kind == toconst
       static Vote keep()                          { return {Vote_kind::keep,    {}, {}}; }
       static Vote drop()                          { return {Vote_kind::drop,    {}, {}}; }
       static Vote toconst(const Const& v)         { return {Vote_kind::toconst, {}, v }; }
       static Vote update(Lnast_subtree shape)     { return {Vote_kind::update,  std::move(shape), {}}; }
     };
   }
   ```

2. Replace the `uPass` virtual surface. Old per-op `process_*` methods
   stay temporarily as fallbacks until each pass migrates. New surface:

   ```cpp
   struct uPass {
     virtual void begin_iteration() {}
     virtual void end_run() {}

     // Hot path — 27 arithmetic/logic/comparison/bit ops, single dispatch.
     virtual Vote process_arith(Lnast_ntype::Lnast_ntype_int kind,
                                lnast::Bundle* dst, const Operand_vec& ops) {
       return Vote::keep();
     }

     // Genuinely-shaped ops keep distinct hooks.
     virtual Vote process_attr_set    (lnast::Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
     virtual Vote process_attr_get    (lnast::Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
     virtual Vote process_tuple_add   (lnast::Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
     virtual Vote process_tuple_concat(lnast::Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
     virtual Vote process_tuple_set   (lnast::Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
     virtual Vote process_tuple_get   (lnast::Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
     virtual Vote process_range       (lnast::Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
     virtual Vote process_type_spec   (lnast::Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
     virtual Vote process_if          (                     const Operand_vec& ops) { return Vote::keep(); }
     virtual Vote process_cassert     (                     const Operand_vec& ops) { return Vote::keep(); }
     virtual Vote process_func_def    (lnast::Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }
     virtual Vote process_func_call   (lnast::Bundle* dst, const Operand_vec& ops) { return Vote::keep(); }

     virtual void process_stmts_enter() {}
     virtual void process_stmts_exit () {}
   };
   ```

3. In the runner, per-op-kind operand-vec builders. Per-kind shape:

   - `process_arith` (27 LNAST kinds: plus, minus, mult, div, mod, shl,
     sra, bit_{and,or,not,xor}, log_{and,or,not}, red_{and,or,xor}, eq,
     ne, lt, le, gt, ge, sext, get_mask, set_mask, plus the assign
     non-alias form): LHS = child 0. `ops` = siblings 1..N. Each child
     classified at build time:
     - `ref` → look up `st[name]`. If absent → `Runtime_trivial`. If
       present and has a comptime scalar → wrap as `Const`. If present
       and structurally relevant (has any tuple field or any attr) →
       wrap as `shared_ptr<Bundle>`.
     - `const` → parse text once via `Dlop::from_pyrope`, wrap as `Const`.
   - `attr_set`: LHS = child 0; vector = `[Const(attr_name), value_operand]`.
   - `attr_get`: LHS (dst tmp) = child 0; vector = `[base_operand, Const(attr_name)]`.
   - `tuple_add`: LHS = child 0; vector entries one per field
     (assign sub-nodes contribute the key+value pair).
   - `tuple_concat`: LHS = child 0; vector = N operands.
   - `tuple_get`: LHS = child 0; vector = `[base, field_key]`.
   - `range`: LHS = child 0; vector = `[start, end]`.
   - `type_spec`: LHS = child 0; vector = `[prim_type_node, …]`.
   - `if`: no LHS; vector = `[cond]`. Arm bodies handled by runner
     scope push/pop and recursion, not in the vector.
   - `cassert`: no LHS; vector = `[cond]`.
   - `func_def`: LHS (function name) = child 0; vector layout TBD as
     part of Step F.
   - `func_call`: LHS (dst tmp) = child 0; vector = `[ref(callee), arg_operands…]`.

4. Runner dispatch loop becomes a switch on `Lnast_ntype` that builds
   the operand vec, resolves `Bundle* dst` (look up by LHS name, create
   if first-def), then calls each pass's hook in fixed order and
   collects the returned `Vote`. After all passes, run vote resolution
   (Step G) and emit (Step H).

5. Migrate each opt pass to override the new hooks. Per pass:
   - `uPass_constprop`: implement `process_arith(kind, dst, ops)`
     centrally (the 27 cases become a switch on `kind` inside a single
     function; the per-op templates `process_nary` / `process_binary`
     / `process_unary` stay as helpers but take `(dst, ops)`). Implement
     `process_attr_get` (folds via `dst->attrs`) and `process_tuple_*`
     / `process_range`. Delete the old per-op `process_*` overrides
     once everything is on the new surface.
   - `uPass_attributes`: shrinks to `process_attr_set` (cat-A/B
     handler dispatch only — the structural attr writes move to bundle
     pre-pass, Step F), `process_attr_get` (chains through Bundle.attrs
     and aliases), and the if-arm-callback. No more per-arith hook.
   - `uPass_bitwidth`: `process_arith` (range arithmetic on operands'
     `min`/`max`), `process_range`, `process_type_spec`. Writes go to
     `dst->attrs["min"]`, `dst->attrs["max"]`, `dst->attrs["bits"]`.
   - `uPass_coalescer`: `process_arith` for the parking pass; the
     read-flush loop walks `ops` directly. Boundary `process_*`
     overrides (`process_tuple_*`, `process_func_call`, `process_if`,
     `process_cassert`, `process_stmts_*`) keep their flush_all
     semantics on the new surface.

**Test invariant**: 11 failing tests after each pass migrates. Migrate
constprop first (largest), then attributes, then bitwidth, then
coalescer. After each, re-run the test suite.

---

### Step F — Bundle pre-pass

**Why**: structural state belongs in one place. Today `attributes` owns
9 side maps for what is conceptually "the LHS Bundle's shape + sticky +
alias state." Put that work in a single pre-pass that runs first per
node.

**Do**:

1. New pass `upass/bundle_pre/upass_bundle_pre.{hpp,cpp}`, registered
   like the others (plugin name `"bundle_pre"`). Owns these op
   handlers:
   - `process_type_spec`: writes `dst->attrs["type"]` (mut/const/reg/await/let),
     `dst->attrs["bits"]`, `dst->attrs["ubits"]`/`["sbits"]` if width
     given, `dst->attrs["kind"]` (unsigned_int / signed_int / boolean / string).
   - `process_attr_set`: writes `dst->attrs[attr_name] = value_operand`
     (value resolved through the operand entry — if the value operand
     is a `Const`, store it; if `Runtime_trivial` or `shared_ptr<Bundle>`,
     leave the slot allocated and let constprop fill later). Flips
     `dst->attrs["wrap"]`/`["sat"]` presence-only for the policy form.
     For sticky names (`_*`, `debug`) adds the bucket to
     `dst->attrs["sticky_" + bucket]`.
   - `process_range`: allocates `dst->attrs["range_start"]` /
     `["range_end"]` slots (filled by constprop when bounds fold).
   - `process_tuple_add` / `process_tuple_concat`: populates
     `dst->fields[…]` with the per-field bundle entries.
   - `process_tuple_set`: mutates `dst->fields[key] = value` in place.
   - `process_tuple_get`: writes `dst->attrs["alias_base"]` =
     `Const(base_name)`, `dst->attrs["alias_field"]` =
     `Const(field_key)` so attribute reads on the dst tmp chain through.
   - `process_arith` (default): migrates sticky / pending_import keys
     from every operand Bundle into `dst`. Returns `Vote::keep()`.
   - `process_func_def` / `process_func_call`: see Step F (extraction)
     below.

2. Register `bundle_pre` first in the runner's resolved order. It
   always runs before constprop / attributes / bitwidth / coalescer.

3. As bundle_pre takes over each piece of work, remove the corresponding
   logic from attributes:
   - `attr_set_values` / `attr_set_refs` / `type_info_map` /
     `range_bounds` / `tuple_shapes` / `tuple_get_alias` / `shape_source`
     / `direct_alias` / `tmp_fold` / `assigned_once` /
     `const_assign_count` / `wrap_policy` / `sat_policy` — delete.
     `uPass_attributes` keeps only the `Handler_registry` for cat-A
     and cat-B special semantics, plus `process_attr_get` folding.
   - `record_assign`, `migrate_alias`, `migrate_aggregate_attrs_to_fields`,
     `apply_narrowing`, `narrow_for_lhs`, `evaluate_attr_get`,
     `lookup_*` helpers — delete or move to bundle_pre / constprop as
     appropriate.

**Test invariant**: 11 failing tests after each piece migrates. Move
one map at a time (start with `type_info_map`, then `wrap_policy` /
`sat_policy`, then `tuple_shapes`, etc.) so failures localise.

---

### Step G — Vote resolution

**Why**: replaces the binary `Emit_decision` with the four-way `Vote`
from §1; supports `toconst` (commit-and-elide) and `update` (rewrite
shape, for inline).

**Do**:

1. In the runner, after dispatching every pass for a node, reduce the
   votes per the priority `drop > toconst > update > keep`:
   - Any `drop` → emit nothing.
   - Else any `toconst` → emit nothing; commit the Const to
     `dst->fields["0"]` if not already there. **Assert** that every
     `toconst` vote carries the same Const (mismatch = conceptual
     error, hard fail with a diagnostic).
   - Else any `update` → emit the `update_shape` from the (latest)
     `update` vote. **Warn** when more than one pass voted `update`
     (unexpected today).
   - Else (all `keep`) → emit the source op kind with operand-vector
     entries materialised: `Const` → emit as `const`;
     `shared_ptr<Bundle>` with a comptime scalar → emit as
     `const(value)`; `shared_ptr<Bundle>` without one → emit as
     `ref(name)`; `Runtime_trivial` → emit as `ref(name)`.
2. Delete `upass::Emit_decision` and the runner's `any_pass_drops` /
   `classify_statement` plumbing once every pass returns `Vote`.

**Test invariant**: 11 failing tests.

---

### Step H — Dest LNAST in new forest

**Why**: avoids the staging-tree-then-`replace_body` swap and keeps the
parsed tree alive in its own forest.

**Do**:

1. In `pass/upass/pass_upass.cpp`, before running the runner, create a
   new forest named `lgdb/optimized` (resolve from the project lgdb
   path; one forest per `pass.upass` invocation is fine for now).
2. For each `ln` in `var.lnasts`, the runner allocates a fresh dest
   tree in `lgdb/optimized` with the same name as `ln`. The runner
   builds it directly node-by-node during the walk (no batched
   end-of-walk commit).
3. After the runner finishes a source LNAST, **replace the slot** in
   `var.lnasts` so it now points to the optimized LNAST (parsed
   LNAST stays in its forest, no longer referenced by `var.lnasts`).
4. Delete the staging-tree machinery: `staging`, `staging_parent`,
   `staging_parent_stack`, `take_staging`, `replace_body` call in
   `pass_upass.cpp`.

**Test invariant**: 11 failing tests. Downstream consumers
(`lnast.dump`, `pass.lnastfmt`, etc.) read whatever `var.lnasts`
points at — semantics unchanged.

---

### Step I — Runner-owned SSA (tuple expansion + name versioning)

**Why**: SSA disappears as a pass; its work lives in the runner's emit
step where the Bundle shape is already known.

**Do**:

1. Delete `upass/ssa/`. Remove `uPass_ssa::run` calls from
   `pass_upass.cpp`. Remove the IO metadata extraction step that ran
   pre-runner (the runner does it as part of `fdef` extraction in
   Step F; see below).
2. In the runner, add per-LNAST state:
   `absl::flat_hash_map<std::string, int> ssa_counters;`. Cleared when
   a new dest LNAST is started.
3. At emit time for each source op (after vote resolution gave the
   "emit" verdict):
   - If `dst->attrs["pending_import"]` exists → emit source op as-is,
     no expansion, no versioning. SSA work is deferred until a future
     invocation clears the poison.
   - Else if `dst->fields` has tuple shape (multiple keys or non-`"0"`
     keys) → for each field, emit a per-field flat assign
     (`<lhs>.<field> = <field_value>`). The flat name participates in
     SSA versioning too (each new occurrence bumps the counter).
   - Else (scalar) → emit a normal assign. If `ssa_counters[lhs]` is
     0, no suffix; first emission writes counter = 1. Subsequent
     emissions to the same name (`mut` reassignment) write counter+1
     and emit the name with a `_<N>_` suffix appended (LoC-based
     naming is deferred — see `TODO_livehd.md`).

**Test invariant**: 11 failing tests. Tests that exercise mut
reassignment (e.g., `xx.prp`'s 1M `a += 1`) keep working — each `a`
emission gets a fresh `_<N>_` suffix; downstream `lnast_to_lgraph` sees
the SSA-versioned names.

---

### Step J — Verifier / assert as dest-walk finishers

**Why**: today verifier/assert intrude into the per-node dispatch via
`classify_statement` and `runner_fold_fn`. After Step H every cassert
emission carries a simplified cond (`const_true` / `const_false` /
surviving `ref`); verifier just counts emitted nodes.

**Do**:

1. In the runner, drop `process_cassert` from the opt-pass dispatch
   chain. The runner directly handles cassert emission: read the cond
   operand from the operand vector; if `Const` → emit as
   `cassert const_true` / `cassert const_false`; if Bundle has
   `pending_import` → emit as `cassert ref(...)` (surviving ref);
   else `cassert ref(...)`.
2. After the runner finishes the source walk, check the symbol table:
   if any Bundle has `attrs["pending_import"]`, skip the finisher walk
   (defer verifier/assert to next invocation).
3. Otherwise walk the dest LNAST top-to-bottom. For each `cassert`:
   - cond is `const_true` → bump `pass_count`.
   - cond is `const_false` → bump `fail_count`, raise `upass::error`
     unless the test expects it (`verifier_fail:N`).
   - cond is a surviving `ref` → bump `unknown_count`.
4. Aggregate counts across `var.lnasts` (same as today's
   `uPass_verifier::finalize_aggregate`). Compare against
   `verifier_pass:N` / `verifier_fail:N` from the `pass.upass` labels.
5. Remove `uPass_verifier` / `uPass_assert` from the opt-pass list.
   They become runner helpers (or a single `dest_walk_finisher` module
   in `upass/runner/`).

**Test invariant**: 11 failing tests. The `verifier_pass:N` /
`verifier_fail:N` checks continue to gate as today. The
`pending_import` path is exercised once Step K lands.

---

### Step K — Pending-import poison mechanism

**Why**: the propagation/gating mechanism needs to exist even though
the `import` op handler is deferred (so we can test it).

**Do**:

1. Define a presence-only marker: `dst->attrs["pending_import"]`. The
   value (Const) is meaningful only as "this key exists". For the
   first round we don't store the list of blocking `ImportRef`s
   (deferred to `TODO_livehd.md 2k`).
2. **Origination** (placeholder until import op lands): the runner
   stamps `dst->attrs["pending_import"] = Const(1)` when it sees the
   `import` LNAST node (today: just stamp the dst Bundle and emit the
   `import` verbatim; no resolution).
3. **Propagation** (in bundle pre-pass):
   - `process_arith` (default): if any operand Bundle has
     `attrs["pending_import"]`, set it on `dst`.
   - Assignment-shaped ops: same.
   - Inside an if-arm whose cond Bundle has `attrs["pending_import"]`,
     every Bundle written in the arm inherits the marker (runner
     stamps it on each LHS Bundle when entering the arm; the existing
     control-taint mechanism for sticky moves into bundle pre-pass).
4. **Gating**:
   - Runner's per-node opt-pass loop: if any operand Bundle has
     `pending_import`, only `constprop` is invoked; the other opt
     passes are skipped for this node.
   - Runner's emit step (Step I): if `dst->attrs["pending_import"]`,
     skip tuple expansion and name versioning; emit the source op
     verbatim.
   - Runner's finisher (Step J): if any Bundle in the symbol table
     has `pending_import` at end-of-walk, defer the verifier/assert
     dest walk.
5. **Clearing**: on the next `pass.upass` invocation, the symbol table
   is rebuilt from scratch. If the import resolved, no Bundle gets the
   marker.

**Test invariant**: 11 failing tests. Add a small new test file under
`inou/prp/tests/comptime/` that uses an unresolvable `import` and a
cassert that depends on the imported name; the cassert should emit as
a surviving `ref` and the verifier finisher should defer (no error).

---

### Step L — `func_extract` collapses into runner

**Why**: today `func_extract` runs as a separate pre-pass that spawns
sibling LNASTs into `var.lnasts`. The redesign puts extraction and
inlining inside the single walk.

**Do**:

1. Delete the dedicated `func_extract` pre-pass invocation in
   `pass_upass.cpp` (the loop that runs `extract_order = {"func_extract"}`
   over each LNAST before the main runner). Keep the helpers
   `uPass_func_extract` provides (IO node allocation, body
   prep) but call them from the runner.
2. In the runner, when visiting an `fdef` node:
   - Look up an existing optimized LNAST in `lgdb/optimized` with the
     `fdef`'s function name. If none, allocate one and populate its
     `top → io + stmts` shape using the func_extract helpers
     (io_meta harvest from the fdef's input/output tuple_add children).
   - **Continue iterating the fdef's stmts in the current walk** so the
     fdef body sees the caller's scope (per the user's intent — `mut b
     = 3; comb fcall(){...b...}; b = 100` captures `b = 3`).
   - Outer-scope reads inside the fdef body: bundle pre-pass enforces
     the closure-capture rule. If `st[outer_name]` is comptime
     (has scalar Const in `fields["0"]`, no `attrs["pending_import"]`)
     → operand becomes `Const(value)` at the use site (inlined). If
     `attrs["pending_import"]` → operand stays a `ref` and the dst
     inherits the poison. Else → `upass::error("capture of non-comptime
     outer var <name> in fdef <fname>")`.
   - Emit the body's processed nodes into the optimized LNAST for the
     function (NOT the caller's dest LNAST). When the fdef stmts ends,
     close the optimized LNAST and resume emission in the caller's
     dest LNAST.
3. In the runner, when visiting an `fcall` node:
   - Look up the optimized LNAST for the callee. If absent → emit the
     `fcall` verbatim (the callee will be spawned later; this becomes
     an inter-pass dependency the existing cache machinery handles).
   - Inline: push a fresh function-body scope frame on `st`. Synthesize
     `assign param_x = caller_arg_x` LNAST nodes at the head of the
     inline walk so bundle pre-pass sees them as normal assigns
     (uniform pipeline). Switch the cursor into the callee LNAST's
     stmts and walk it as if it were inlined. On exit: pop the frame,
     advance the caller cursor past the `fcall` node. The fcall's
     dst tmp Bundle is whatever the inlined body's last assign to
     `res` (the callee's output) wrote.
   - Nested inlines push more frames; the runner stack just grows.
4. Delete `uPass_func_extract`'s pass-plugin registration so it isn't
   pulled into the main opt-pass loop. The remaining helpers move to
   `upass/runner/func_extract_helpers.{hpp,cpp}` (or inline into the
   runner).

**Test invariant**: 11 failing tests. `prp-fcall1` / `prp-fcall2`
continue to pass. `prp-fcall3` (the new closure-capture test) should
start passing once this step lands.

---

### Step M — Drop `max_iters`; keep per-pass enable/disable — **DONE**

**Why**: single walk per invocation (one of the redesign's primary
guarantees). Per-pass toggles stay because they are useful for debug
(`pass.upass verifier:0 constprop:0 attributes:0 …`).

**Done**:

1. Deleted the `max_iters` label from `Pass_upass::setup` (and the
   constructor's parse/validate block + the `max_iters` member) in
   `pass_upass.cpp`/`.hpp`. Both runners' `run()` take no argument and
   do a single walk; `pass.upass` no longer accepts a `max_iters` label.
2. Deleted the `Runner_fixed_point` helper (`upass_runner_common.hpp`)
   and the lgraph runner's `changed_passes()` convergence check. The
   lgraph runner now does a single walk like the lnast runner.
   `uPass::mark_changed` / `has_changed` / `begin_iteration` are kept as
   diagnostic no-ops — nothing re-dispatches on them.
3. Keep `verifier:0/1`, `constprop:0/1`, `attributes:0/1`,
   `bitwidth:0/1`, `coalescer:0/1`, `func_extract:0/1`, `ssa:0/1`
   labels. After Step I `ssa:0/1` no longer disables a pass — repurpose
   it to a runner flag that skips the runner's SSA emit step (useful
   for debugging the pre-SSA shape). After Step J `verifier:0/1`
   skips the finisher. After Step L `func_extract:0/1` skips the
   runner's fdef/fcall machinery (the runner emits fdef/fcall
   verbatim, treats fcall as opaque, etc.).
4. Keep the `order=...` debug label that lets the user spell out the
   exact pass list to run (still useful for testing).

**Test invariant**: 11 failing tests. The `prplib.py` test driver and
all `pass/upass/tests/*.sh` no longer pass `max_iters:…`; the runner's
completion marker is now `uPass - walk complete` (was `converged at
iteration 1`).

---

### Step N — Cleanup

**Why**: ensures the redesign actually shrinks the codebase rather
than layering on top.

**Do**:

1. Delete dead surface from `upass/core/upass_core.hpp`: old
   `process_*` virtuals once every pass migrated to the new
   `process_arith` / shaped hooks. Old `classify_statement` /
   `fold_ref` helper API. `notify_uncertain_arm_begin` /
   `notify_uncertain_arm_end` (replaced by `process_stmts_enter/exit`
   + scope-aware Bundle merge).
2. Delete `upass/attributes/upass_attributes_phase{2,3,4,5,6}.cpp` and
   the corresponding headers once their logic landed in bundle_pre /
   constprop / attributes-shrunken / runner. The `Handler_registry`
   shrinks to cat-A (`wrap`/`sat`/`type`) and cat-B (pin/signal/mode)
   handlers; the sticky pattern handler is no longer needed (sticky
   propagation moved to bundle_pre).
3. Delete `upass/ssa/` entirely.
4. Delete `upass/verifier/upass_verifier.{hpp,cpp}` and
   `upass/assert/upass_assert.{hpp,cpp}` once their finisher logic
   lands as runner helpers.
5. Delete `upass/func_extract/` once its helpers move into the runner.
6. Audit `upass/runner/upass_runner.cpp` for now-dead branches in
   `process_lnast` (A_OP / C_OP / process_drop_candidate /
   process_verbatim) — replace the macro-driven switch with a single
   switch that calls the operand-vec builder and dispatches.

**Test invariant**: 11 failing tests. Code line count in `upass/`
should drop by roughly 30%+ once the migrations finish (today
~7000 lines; target ~4500).

---

## 3. Recommended landing order

Steps A, B, C are sequential foundations. After C lands, D / E / F can
proceed in parallel-ish (D unblocks E unblocks F, but each is
self-contained). G depends on E. H depends on D + the runner being
cleaned up. I depends on H (needs the dest tree). J depends on I (needs
emitted casserts). K depends on F (needs bundle pre-pass to do
propagation). L depends on D + F. M can land any time after the runner
no longer relies on `mark_changed`. N is the final cleanup pass.

A linear order that always leaves the tree green at 11 failures:

```
A → B → C → D → E (one pass at a time) → F (one map at a time)
  → G → H → I → J → K → L → M → N
```

If a step requires more than a single commit, land it as a sequence
that each preserve the 11-failure baseline.

---

## 4. What is explicitly out of scope for this round

- The `import` op resolution itself (resolving foreign modules,
  loading their LNASTs into the symbol table). The runner stamps the
  poison marker on `import` op outputs so the propagation/gating
  machinery can be exercised; actual resolution is `TODO_prp.md`
  / `TODO_livehd.md` future work.
- The `verifier_pending:N` counter and `:type: top` aggregation rule
  for pending casserts — see `TODO_livehd.md 2k`.
- LoC-based SSA name suffixes — see `TODO_livehd.md` source-map work
  (`1f`). For this round, `_<N>_` numeric suffix only.
- The full `Operand_vec` form with `shared_ptr<Bundle>` operand
  entries carrying narrowed values, narrowed widths, etc., propagating
  back through the `value` field at a later pass — the first pass of
  Step E uses presence-only Bundle pointers (you read what's in the
  Bundle, you don't get the value chained through the variant tag).
  Full optimisation passes that need the narrowed-value chain land
  later.

---

## 5. Test invariant

At every step boundary:

```
bazel test -c dbg //inou/prp:all //upass/...
```

must show exactly the **11 baseline failures** (the same `prp-*` test
names as before the redesign started). Any change that adds or removes
failures gets reverted, root-caused, or both before continuing.

Performance gate (Step H or later, once the dest LNAST is real):

```
bazel build -c opt //lhd:lhd
time ./bazel-bin/lhd/lhd compile xx.prp --workdir /tmp/w -q
```

must be at least as fast as today (~10.0s median on the 1M-op
`xx.prp`). The redesign's perf win comes from bundle pre-pass
eliminating attributes' per-arithmetic-op work; expect the win to
surface after Step F lands.
