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

- **1b** Range-fit enforcement + bit-select/bit-set as the "force" operator —
  **PARTIAL, landed 2026-05-30** (depends conceptually on [[1t]]'s `(max,min)`
  envelope). Example:
  [inou/prp/tests/comptime/range_force.prp](inou/prp/tests/comptime/range_force.prp)
  (7/0). Design: `docs/contracts/typesystem_clean_plan.md` (T4).
  - **Landed (no regressions; full comptime sweep = documented baseline only):**
    - `Lnast_range::contains` — the sign-agnostic fit predicate
      (`upass/bitwidth/lnast_range.hpp`; unit tests in `upass_bitwidth_test.cpp`).
    - `process_get_mask` range = `[0, 2^w-1]` (w = popcount of the const mask;
      single bit → signed `[-1,0]`), so the bit-select **force** operator is
      range-tracked (`upass/bitwidth/upass_bitwidth.cpp`). Strict improvement
      over the old conservative `unbounded()`.
    - **Signed range-fit enforcement** in `attributes::on_assign_like`: a
      first-write comptime value to a concretely-bounded SIGNED LHS (sN /
      `int(max,min)`) that escapes `derive_max`/`derive_min` is now a hard
      `upass::error` (`const x:s4 = 200` → error). Signed types were previously
      unchecked. There is NO reinterpret for signed (genuine overflow).
    - The unsigned side already had (T4, kept unchanged): negative-pattern →
      unsigned reinterpret (implicit force) + positive-overflow error (the
      `bit_test(bits)` heuristic).
  - **Architecture note:** the fit-check lives in `attributes` (where value +
    declared envelope + wrap/sat policy + `was_assigned` all coexist), NOT in
    `bitwidth` as the T4 sketch proposed — that avoids the constprop/attributes
    value-coherence gap. The `bw_meta().type_ranges` plumbing is therefore NOT
    needed and was dropped from the plan.
  - **Gotchas hit (documented so the next agent doesn't):** (1) an unbounded
    `int`/`uint` stores a *present-but-nil* `Const` for max/min whose `is_i()`
    is true — treating it as a bound calls `sub_op(nil)` and aborts in hlop
    `blop.hpp::negn`; guard every bound with `is_i() && !is_nil()`. (2) The
    range-fit block must NOT broaden the UNSIGNED gating — entering it (even as
    a no-op) for an unsigned LHS perturbed comptime-ref folding
    (`comptime const k=5; mut r:u4=k`); keep the unsigned path byte-identical
    to T4 and add signed as a separate branch.
  - **Deferred:** full unsigned positive-overflow generalization (the
    `bit_test` heuristic still misses magnitudes ≥ 2^(N+1)); reassignment
    enforcement (only the declaration's first write is checked); pinning the
    compile-errors as golden diagnostics ([[4h]] — no `:expect_fail:` harness).
  - **Problem.** `@graph/cell*` always operates on SIGNED values (and, except
    `get_mask`, produces signed values). So `0xFF` is `0sb0_1111_1111` = **+255**,
    but `0sb1111_1111` = **-1**, and a sign bit propagates through bitwise ops:
    `v|0xff` with `v` negative is **-1**, not 255. Storing such a value into an
    unsigned destination must be a compile error, but a 9-bit *signed* positive
    (e.g. `0|0xFF` = 255 = `0sb0_1111_1111`) must still fit `u8` — so the check
    can NOT be a bit-width comparison.
  - **The check (two ranges, not one).** Each typed var carries BOTH a fixed
    **type envelope** `[Tmin,Tmax]` (from `declare`'s `prim_type_int(max,min)`,
    never narrowed by propagation) and the tightening **actual** range (today's
    `upass_bitwidth` `range_map_`). Keep propagating actuals (they are tighter →
    drive constprop / narrower cells); do NOT replace them with type ranges. At
    every `store`/`assign` to a typed var, assert the SIGN-AGNOSTIC containment
    `Tmin <= vmin && vmax <= Tmax`; on failure (and no force/wrap/sat) →
    `upass::error`. Set the lhs actual to `actual ∩ envelope`. This dissolves the
    signed/unsigned confusion: `u8 = 0xFF` → `[255,255]⊆[0,255]` ✓;
    `u8 = 0sb1111_1111` → `[-1,-1]⊄[0,255]` ✗.
  - **The force operator (this is the new idea).** When you DO mean the bits,
    spell it as an explicit **bit-select** `e#[lo..=hi]` (read, lowers to
    `get_mask` — the one unsigned-producing cell) or a **bit-set** field write
    `v#[lo..=hi] = e` (lowers to `set_mask`). A select's result range is
    `[0, 2^(hi-lo+1)-1]` by construction, so it fits the destination iff the
    width fits — "force" reinterprets the SIGN but is still range-checked
    (`u8 = ones#[0..=9]` still errors). No node-qualifier slot is needed (the
    invasive part of T4) — `get_mask`/`set_mask` already exist. `wrap`/`sat`
    remain only for modulo / clamp, where the value is not a fixed bit pattern.
  - **upass/ plumbing.**
    - `upass/attributes` flushes the declared `Type_info` range into a new
      `bw_meta().type_ranges` map from `process_declare`; `upass_bitwidth`
      seeds a fixed `type_range_map_` from it in `begin_iteration` (mirrors how
      `range_map_` is seeded from `bw_meta().ranges`,
      `upass_bitwidth.cpp:39-49`). Single parser of `prim_type_int`.
    - Add `Lnast_range::contains(other)` (`upass/bitwidth/lnast_range.hpp`); the
      fit-check + clamp at the store is the existing `meet`/`is_narrower_than`
      machinery. The contradiction block in `process_attr_set`
      (`upass_bitwidth.cpp:486-578`) is the prototype — lift it onto
      `declare`/`store`, flip its `Pass::warn` → `upass::error`, gate on the
      force/wrap/sat presence.
    - Teach `process_get_mask`/`process_set_mask` (`upass_bitwidth.cpp:462-471`,
      currently `unbounded()`) to emit `[0, 2^width-1]` from the const range arg
      — this is what makes the force-operator casserts pass the check
      automatically (strict improvement over today's conservative unbounded).
    - **Read-side force on a negative source — FIXED in hlop (2026-05-30).**
      `ones#[0..=7]` where `ones == -1` was folding to `1`, not `255`:
      `Dlop::get_mask_op` capped extraction at the source's minimal signed width
      (`src_bits`), and a negative const's minimal width is a single sign bit, so
      only bit0 survived (`-2` → 2, etc.). Fixed by extending extraction to the
      positive mask width, reading out-of-word bits as the sign, and sizing the
      output by the mask width (`../hlop` `dlop.cpp::get_mask_op`, commit
      `e970b7c`, pinned in `MODULE.bazel`; regression
      `dlop_test::get_mask_op_negative_source_sign_extends`). `get_mask` returns
      UNSIGNED (matching `#[..]`); `#sext[..]` keeps it signed — cf.
      [[sext_bitrange_lowering]]. So `(-1)#[0..=7] == 255` now folds and the
      example exercises read-side force on the signed value directly.
    - **Source the actual from constprop's folded `Const`, not the text
      parser.** `range_from_const_text` (`upass_bitwidth.cpp:122`) only handles
      `0x`/`0b`/decimal — `0sb…` and unknown-bits literals fall through to
      `unbounded()`, so the check can't even see `v:u8 = 0sb1111_1111` = -1.
      constprop/Dlop already know the true signed value; bridging that closes
      this gap and the `valid_unknown_bits` `v|0xff` value-coherence gap together.
  - **Error message** prints decimal value + both ranges + the sign diagnosis
    (so an agent sees `-1` vs `255` immediately) — see the comments in the
    example for the exact wording 1b should emit.
  - **Compile-error cases are comments in the example** (the harness has no
    `:expect_fail:` yet — [[4h]]). When [[4h]] lands, promote the four
    `FUTURE COMPILE ERROR` blocks in `range_force.prp` to pinned diagnostics.
  - **Fixes / unblocks:** the `:u8` storage-width enforcement noted for
    `prp-valid_unknown_bits` (9/1) and the `wrap`/masking gaps in
    `prp-wrap_checks` / `prp-wrap_complex` — all currently mapped to [[1t]].

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
    [[3a]]); land before those goldens are taken.

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

- **2r** Pyrope comptime corner-case gaps surfaced by the test corpus —
  small features that don't fit the larger Group-1/Group-2 entries but
  each currently break one test.
  - **`unique if` comptime fold + `__hotmux` direct call** — fixes
    `prp-hotmux_unique_if`. Constprop currently leaves `__hotmux`
    direct calls and `unique if` chains unresolved (verifier log:
    `pass:2 fail:0 unknown:6`). Needs: (a) constprop recognition of
    `__hotmux(s=<comptime onehot>, p1=…)` so the matching slot is
    selected at comptime; (b) `unique if` chain folding when all guards
    are comptime-known including the sticky-init fall-through path
    when no guard fires.
  - **Mixed match-arm prefixes + tuple `case` patterns** — fixes
    `prp-match_arms_mixed`. Verifier log shows `pass:5` against the
    declared `:verifier_pass: 6`; one cassert is not being counted.
    Two candidate causes worth confirming: (1) the dead-`else` arm's
    `cassert(false)` is correctly eliminated and the header should be
    `:verifier_pass: 5`; (2) tuple-`case` pattern `case (a=1, b=2) {…}`
    against a tuple selector isn't yet a comptime equality test. Audit
    expected vs. emitted cassert count, then either fix the test
    header or wire the tuple-`case` lowering.
  - **String escapes / hex / raw / interpolation with escaped quote** —
    fixes `prp-string_escapes`. Verifier log: `pass:8 fail:2 unknown:2`.
    Needs scanner/lowering: (a) `\xNN` hex escapes inside cooked
    strings; (b) raw (single-quoted) strings preserving backslashes
    literally so `'line\nstill'.[size] == 11`; (c) interpolated cooked
    strings tolerating an escaped quote inside the body
    (`"tag=\"{tag}\""`); (d) `.[size]` attribute on string values.

- **2s** Comptime-fold direct builtin cell calls — **needs hlop sync**
  (stub; flesh out before implementation). The library-level cell
  invocation form `__sum`/`__sub`/`__mult`/`__and`/`__or`/`__xor`/
  `__shl`/`__sra`/`__get_mask`/`__set_mask`/`__sext`/`__zext`/`__ror`/
  `__rand`/`__rxor`/`__mux`/… currently returns `unknown` in constprop:
  the `#`-syntax LNAST ops fold, but the explicit `__cell(args)`
  func-call form is not evaluated.
  - **hlop sync is the crux.** The fold must mirror each cell's exact
    `Dlop::*_op` semantics (single source of truth — do not reimplement;
    cf. [[constprop_delegate_to_dlop]] rule), and the `__cell`
    name↔Dlop-op mapping plus the arg/pin ABI must stay in lockstep
    with hlop's cell definitions. Pin the hlop commit when this lands.
  - Reads the typesystem envelope ([[1t]]/[[1v]]) for width-correct
    results.
  - Fixes the bulk of `prp-cellmap_comb` / `prp-cellmap_misc` `unknown`s
    (the `cell_*` wrapper-comb halves landed via the comb inliner 1i; this
    owns the direct `__cell` calls).
  - **Overlap to coordinate:** `__hotmux` is also named under [[2r]]
    (unique-if/hotmux dispatch). Decide which task owns `__hotmux` so it
    isn't double-implemented.

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
    already sketched in [[1t]]'s `declare( var, <type>, const(mode), [value] )`
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
  surface as diffs. Depends on TODO_verilog.md [[3a]]
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

## Failing-test → TODO mapping (snapshot 2026-05-30)

Each `bazel test -c dbg //...` failure mapped to the TODO entry whose
landing should fix it. When more than one entry is listed, the test
needs all of them. Current verifier tally is shown as `pass/fail/unknown`.
Tallies re-measured via the `prplib.py` comptime pipeline
(`pass.upass constprop:1 verifier_pass:N verifier_fail:N
[verifier_include_funcs:true]`).

**Landed since the original snapshot (removed from the list):**
`prp-bitreduce`, `prp-bitset` (bit-range read/write, reductions, popcount),
`prp-fcall5b`, `prp-fcall_rename_deep` (1r fcall-return unwrap, now landed),
and — re-verified live 2026-05-30 — the whole wrap/sat/typed-storage cluster
(`prp-wrap_checks`, `prp-wrap_complex`, `prp-valid_unknown_bits`), the direct
`__cell`/cellmap tests (`prp-cellmap_comb`, `prp-cellmap_misc`, `prp-formux`),
`prp-hotmux_unique_if`, `prp-paths_if`, and `prp-typesystem` (26/0/3, green —
its 3 residual unknowns are 1v named-type semantics, tracked under [[1t]]).
**Live suite = 8 fails / 248 pass** across `//inou //upass //lnast //pass`.

| Failing test | now | TODO entry | What it still needs |
| --- | --- | --- | --- |
| `prp-bitreverse` | 3/7/0 | [[1t]] | comb-inline (`reverse()`/`sreverse()`) landed via 1i; residual is the `for i in 0..<x.[bits]` unroll (needs comptime `.[bits]`) + bit / storage-width fold under 1t |
| `prp-enum_color` | 0/1/2 | [[2l]], [[1k]] | enum with associated setters/methods |
| `prp-enum_hier` | 0/0/6 | [[2l]] | nested / hierarchical enums |
| `prp-enum_simple` | 0/0/9 | [[2l]] | base enum parsing + comptime tags |
| `prp-enum_types` | 0/0/0 | [[2l]] | typed enum interplay with `does` (casserts unreached) |
| `prp-match_arms_mixed` | 5/0/0 | [[2r]] | mixed match-arm prefixes + tuple `case` patterns (one cassert not counted) |
| `prp-setter_complex` | 2/2/0 | [[1k]] | decorator-init implicit setter dispatch on `x:Tup = (…)` |
| `prp-tuple_decorator_complex` | 0/0/2 | [[1k]] | tuple-decorator init triggering setter |
