# TODO — Pyrope semantics

Pending work to finish correct Pyrope language handling: frontend, LNAST
production, attribute/type semantics, memory/register lowering, and test
infrastructure for the Pyrope path.

Items use the same Group N letters as the master plan in [TODO.md](TODO.md).
Items in the same group can be done in parallel; all letters in group N must
complete before group N+1 starts. Group letters are shared across
[TODO_prp.md](TODO_prp.md), [TODO_verilog.md](TODO_verilog.md),
[TODO_livehd.md](TODO_livehd.md), and [TODO_hhds.md](TODO_hhds.md), so
cross-file dependencies stay visible.

## Group 1 — foundation

- **1m** Pyrope `import` statement — **deferred**. Substantial new
  infra; not attempted in the 2026-05-27 session.
  - **Tried:** Nothing. Scope was assessed (substantial new
    infrastructure: grammar, prp2lnast lowering, file-wrapper LNAST
    tree synthesis, cross-tree `find_tree` resolution) and skipped
    in favor of other landings.
  - **Tree-naming convention:** for a file `foo/bar.prp`, every
    `comb`/`pipe`/`mod` defined inside gets its own LNAST tree
    named `foo/bar.<entity>` (e.g. `comb xx() …` → tree name
    `foo/bar.xx`). Trees are created for **both** `pub` and
    non-`pub` definitions — every entity always gets a tree
    because every `comb`/`pipe`/`mod` needs one. The `pub` keyword
    controls *exportability*: only `pub`-marked entities can be
    named by `import("foo/bar.xx")` from another file.
  - **Pub constants / tuples** at file scope (`pub const yy = 33`)
    can't ride a per-entity tree, so the import machinery
    synthesizes a single "file wrapper" LNAST tree per source file
    that returns all of that file's `pub` non-function values.
    Roughly equivalent to writing:
    ```
    comb bar() -> (yy, other_pub_var) {
        yy = 33
        other_pub_var = 44
        xx = import("foo/bar.xx")  // only if `pub comb xx ...`
    }
    ```
    Result is read-only at the import site (matches the "imports
    are constants" semantics).
  - **Needed for next attempt:**
    - `import path` / `import path as alias` grammar in
      tree-sitter-pyrope.
    - prp2lnast lowering: emit a `func_call`-style ref against
      the imported tree name, resolved via `find_tree`.
    - File-wrapper LNAST tree synthesizer for `pub` file-scope
      values that have no associated `comb`/`pipe`/`mod` tree.
    - For Group-1 tests, explicit phase ordering — `import_phase1`
      / `import_phase2` test directives, hand-ordering which files
      are processed in which phase. The pending-import / second-
      phase resolution loop belongs to Group-2 [[2o]] and depends
      on this entry landing first.
  - **Tests to add when landing:**
    1. Import a remote already-resolved `comb`/`pipe`/`mod` function
       (single-phase happy path — importer sees a fully-typed
       `tree_ios` ABI from the imported module).
    2. Import a remote `comb`/`pipe`/`mod` that needs the second
       phase (pending-import marker is set, importer can't comptime
       its `if`/sticky propagation until the import resolves on the
       next `pass.upass`). Pairs with [[2o]].
    3. Import a constant (no `tree_ios` ABI, just a bundle entry
       with a comptime value — via the synthesized file-wrapper
       tree).
    4. Import a "flat" Verilog module via `pub`-keyword exports
       (how does `pub` mark Verilog-side exports, and how does the
       Pyrope side bind them — likely a `tree_ios` ABI variant or a
       sub-class of import that bypasses the second-phase path).
- **1k** `self` / `ref self` in tuple methods — **partial**. Basic
  `self`/`ref self` first-parameter binding and chained-receiver
  semantics (`x.a.b.method(...)` → `self = x.a.b`, `ref self` allows
  mutation of self's own fields only) landed (commit fbbe938fa);
  `prp-setter_multi_field` passes.
  - **Pending:** `prp-setter_complex` and `prp-tuple_decorator_complex`
    still fail. Both require **decorator-init semantics**: when
    `x:Tup = (v="")` runs and `Tup` carries a `setter` field, the
    setter should fire implicitly at instance construction with the
    init values as named actuals. No existing infra hooks the
    tuple-typed init to the setter dispatch.
  - Hint: agent-decorator scaffolded a `pending_setter_init` map +
    `maybe_fire_setter_init` method declaration in
    `upass/constprop/upass_constprop.hpp` during the 2026-05-27
    session; the header-only orphan was reverted to keep master
    clean. Real landing needs the .cpp side:
    - Tuple-init detection in `process_assignment` when the RHS is
      a tuple literal and the LHS has a `:Tup` annotation.
    - `pending_setter_init` bookkeeping when the `:Type` annotation
      lands (record the variable + its declared type); clear at
      the first assign so a later `x = …` doesn't refire.
    - Dispatch through the existing method-call inliner with the
      init-tuple's named fields rebound to the setter's named args.

## Group 2 — depends on Group 1

- **2p** Source-derived SSA / tmp names (`foo_l42_a`, drop `___N`) —
  `docs/contracts/lnast_spec.md §13`. May reuse the source-map ID from
  TODO_livehd.md [[1f]] as the encoded location handle.
  - Depends on [[1f]] (source-map indirection): the naming scheme can
    reference the source-map ID rather than re-encoding `file:line:col`
    in every temp name.
  - Cascades into golden-file updates ([[1h]] and TODO_verilog.md
    [[2d]]); land before those goldens are taken.

- **2o** Two-phase `upass/func_extract` (parallel per-LNAST, then top-down
  resolve) — `import.md`. Depends on the `import` feature landing (no
  standalone entry yet — likely needs one); without imports the
  pending-marker machinery has nothing to gate on.
  - Pending-import poison for the planned `bundle` pre-pass: when an `if`
    condition (or any value sticky/attribute propagation reads) depends on
    a name whose Bundle entry is still unresolved because of a pending
    `import`, the entry must carry a `pending: ImportRef` marker. Bundle
    pre-pass treats a pending-tainted cond as non-comptime *and* must NOT
    propagate sticky/attribute state into the if's arms (the import may
    still inject an `attr_set` or change the cond's comptime value).
    Equivalent rule for attribute propagation through expression ops:
    skip the migration when any RHS ref carries the pending marker, since
    the import may change which `_*` attributes flow in. The pending
    marker is propagated by bundle pre-pass through the same expression-op
    migration path used for sticky `_*` attrs (functionally a second
    sticky channel).
  - SSA-flatten gate: the SSA post-pass owns the `tuple_*` → flat-name
    lowering and rewrites the LNAST (the optimization passes need the
    Bundle ptr during their loop, so flattening earlier would complicate
    `does`/`==`/etc.). SSA must NOT flatten a Bundle whose entry carries
    a pending-import marker — the import may still change the shape,
    field values, or attached attributes. Such bundles stay as
    `tuple_*` nodes for the next `pass.upass` invocation to revisit.
    Re-runs of `pass.upass` after the import resolves clear the marker.

- **2n** `tree_ios` leaf ABI with `min`/`max` slots,
  mimicking `GraphIO` for consistency — `import.md §Function signatures`.
  Downstream upasses (func_extract, constprop) read leaf IO via the ABI
  rather than poking into each LNAST.
  - Reads from the typesystem (`upass/upass.md §2 Typesystem`):
    `bits`/`min`/`max`/`sign` are what the ABI carries.

- **2l** Enum support: parse `enum X = (l0, l1, l2)` and `enum X:Base = (...)`,
  lower to LNAST as comptime-tagged tuples; value-assigned variants (`b=5`,
  auto-increment after), the `in` operator (`x in Enum`), typed associated
  values, and the inline-decl form (`const Color = enum(...)`).
  - Basic enums are nearly independent, but complex enums depend on
    [[1k]] (`self` / `ref self`) since associated tuples carry
    setters/methods — so this lands in Group 2.
  - Fixes failing comptime tests: `prp-enum_simple`, `prp-enum_hier`,
    `prp-enum_color` (depends on [[1k]]), `prp-enum_types` (interplay
    with `does`).

- **2j** Function-scope closure-capture rule, **import-aware case**.
  Companion to Group 1 [[1x]] which handles the no-import portion. The
  pending-import path: when a referenced outer-scope name is tainted
  by a pending import, do NOT inline; leave the ref intact and defer
  the function from func_extract spawn until a re-run of `pass.upass`
  (after import resolves) clears the taint. Depends on [[1m]] (import)
  and [[2o]] (pending-import poison + bundle pre-pass).

## Group 3 — depends on Group 2

- **3k** Automate doc-example ingestion: `extract.rb` (or equivalent)
  pulling `cassert` examples from `docs/docs/pyrope` into
  `inou/prp/tests/docs/`, with CI iteration until they pass. Most have
  been imported manually already; this entry is the remaining
  automation + ongoing sync, not urgent.

- **3g** Memory port tuple semantics + `pipestage` `lat`/`num` —
  `docs/contracts/attributes_spec.md §6`.
- **3j** Pyrope Memory / `Fflop` / clock-reset end-to-end: declaration
  syntax, lowering to LGraph `Memory` cell, multi-clock-domain checks,
  sync/async reset trees.
  - **Declare-folding — bind the initial/reset value ON the `declare`.** A
    declaration with an initializer currently lowers to `declare(var, type,
    mode)` *followed by a separate* `store(var, value)` (e.g. `mut c:u8 = nil`
    → `declare(c, u8, mut)` then `store(c, nil)`; confirmed 2026-06-01). For a
    register/flop this split loses the "this is the value bound *at
    declaration*" relationship: a later same-name `store` is structurally
    indistinguishable from the initializer, so the power-on / reset value
    can't be reliably recovered when the `declare` lowers to `Fflop` / `Latch`
    (and `prim_type_register`'s `reset_pin` needs exactly that value). Fold the
    initializer into the optional 4th `[value]` child of `declare` — the slot
    already sketched in the landed `declare( var, <type>, const(mode), [value] )`
    — so the init rides on the declare node and only genuine *re-assignments*
    emit a trailing `store`. Producer: `rewrite_decls_to_declare` /
    `process_lvalue_for_assign` in `inou/prp/prp2lnast.cpp` (the decl-cluster
    merge already has both the `declare` and its value `store` in hand);
    consumers `process_declare` in `upass/attributes` + `upass/bitwidth` and
    the lnastfmt declare-shape validator must accept the 4th child. (Context:
    the `assign-retype` guard landed 2026-06-01 already establishes that a type
    cast may ride only a `declare` or a `wrap`/`sat` write — see
    `inou/prp/tests/errors/assign_retype.prp`.)

## Group 4 — depends on Group 3

- **4i** Golden-output baseline in `inou/prp/tests/equiv/`: feature-broad
  equiv tests with LNAST/LGraph golden dumps so semantic regressions
  surface as diffs. Depends on TODO_verilog.md [[2d]]
  (`lnast_to_lgraph` finished + Verilog round-trip), since these are
  equiv tests for the lnast→lgraph path, not compile-time-constant
  checks. Each failing-comptime-test category (enums, bits, wrap/sat,
  matrix, valid, strings, match-case, fcall-rename, mux/unique-if,
  comptime attribute) gets at least one equiv counterpart added once
  its feature entry lands.

- **4a** Test functionality on top of slop generation (`test::[…]` blocks,
  runtime harness).
- **4b** Emit `assert` / `assume` / `coverpoint` into slop.
- **4h** Compile-error test infrastructure: harness that pins expected errors
  / warnings to expected source spans (golden-error files), consumed by
  `inou/prp`, upass, and lgraph-pass diagnostics from
  [TODO_livehd.md](TODO_livehd.md) **3f**.

## Failing-test → TODO mapping (snapshot 2026-06-01)

Each `bazel test -c dbg //...` failure mapped to the TODO entry whose
landing should fix it. When more than one entry is listed, the test
needs all of them. Current verifier tally is shown as `pass/fail/unknown`.
Tallies re-measured via the `prplib.py` comptime pipeline
(`pass.upass constprop:1 verifier_pass:N verifier_fail:N
[verifier_include_funcs:true]`).

**Live suite = 6 fails / 250 pass** across `//inou //upass //lnast //pass`.

| Failing test | now | TODO entry | What it still needs |
| --- | --- | --- | --- |
| `prp-enum_color` | 0/1/2 | [[2l]], [[1k]] | enum with associated setters/methods |
| `prp-enum_hier` | 0/0/6 | [[2l]] | nested / hierarchical enums |
| `prp-enum_simple` | 0/0/9 | [[2l]] | base enum parsing + comptime tags |
| `prp-enum_types` | 0/0/0 | [[2l]] | typed enum interplay with `does` (casserts unreached) |
| `prp-setter_complex` | 2/2/0 | [[1k]] | decorator-init implicit setter dispatch on `x:Tup = (…)` |
| `prp-tuple_decorator_complex` | 0/0/2 | [[1k]] | tuple-decorator init triggering setter |
