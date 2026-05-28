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

- **1p** Unknown bits in literals + constprop propagation — **partial**.
  LiveHD-side literal parsing (`0sb?`, `0sb1?01?`, `0ub?1?0`) and AND/OR
  identity folds landed (commit fbbe938fa).
  - **Pending:** `prp-valid_simple` and `prp-valid_unknown_bits` still
    fail — need 3-valued comparison and carry-expanding arithmetic
    (`0sb1? > 0sb01` folds to `true`; `0sb?00 == 0sb?01` folds to
    `false`; `v + 1 == 0sb??` carry expansion). `prp-valid_unknown_bits`
    additionally exercises bit-range reads on partially-unknown values
    (`v#[3..=6] == 0sb1?0`), so its full pass requires [[2q]] as well
    as this entry.
  - Hint: those kernels are in `../hlop`, not LiveHD. Add per-bit
    unknown-mask metadata to HLOP dlop/slop arithmetic ops so LiveHD
    consumes mask-aware results. The LiveHD-side literal + constprop
    surface is ready and waiting for the HLOP side to land.

- **1r** Function-call return destructuring with rename — **partial**.
  `(b, c) = dox(...)` name-driven binding and explicit rename
  `(x=dox.b, y=dox.c) = dox(...)` both lower correctly via a chained
  `tuple_get` emission in prp2lnast (commit 054f78060); `prp-fcall5`
  passes.
  - **Pending:** `prp-fcall5b` and `prp-fcall_rename_deep` still fail.
    LNAST emission is correct (verified via `lnast.dump`) but
    constprop / `upass/func_extract` can't unwrap nested fcall-return
    bundles. Even the non-rename case `(foo, c) = dox(a=3)` then
    `cassert(foo.bar == 4)` ends with `foo.bar` unresolved — the
    fcall result gets flattened (`var:t key:bar key:baz key:c`)
    instead of nesting `foo`'s sub-bundle.
  - Hint: model fcall returns as proper nested tuples in
    `upass/func_extract` so `tuple_get fcall_result foo` resolves to
    the nested bundle. The chained `tuple_get` emission in
    `inou/prp/prp2lnast.cpp::process_lvalue_for_assign` already
    produces what constprop should consume.

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
- **1w** Unify `attr_set` / `tuple_set` variadic-path shape —
  **deferred**. Documented at `docs/contracts/lnast_spec.md §11`.
  Shared layout (root + path elements + terminal):
  `tuple_set root p1…pN value` writes the tuple slot;
  `attr_set root p1…pN attr_key value` writes the attribute.
  `assign` stays distinct (§11.3 decision).
  - **Tried:** Diagnostic only. agent-types confirmed the producer
    side (`inou/prp/prp2lnast.cpp`) already emits the simple shape
    (`attr_set root key value`, `tuple_set root p1..pN value`) for
    the cases that exist in the test corpus; deep `attr_set` with
    intermediate path elements has no test driving it.
  - **Locked rules** (still apply):
    - `attr_set` is declaration-only: `a.[foo] = bar` mid-body is
      a hard compile error. `attr_get` reads are unrestricted.
    - `tuple_set` is freely re-assignable.
    - Write-once enforcement for `attr_set` runs **after** constprop
      (conditional/mutually-exclusive declarations may textually
      look like multiple writes but collapse to one once dead
      branches fold).
    - No `attr_set` after a read of the target.
  - **Needed for landing:**
    - **§10 ST API rework first.** lnastfmt-side enforcement of
      "declaration-only", "no attr_set after read", and write-once-
      per-`(target, attr_key)` all need post-constprop ST queries
      that don't exist yet.
    - Producer change in prp2lnast to emit the variadic
      `attr_set root p1..pN attr_key value` shape for `a.b.[attr] =
      value` declaration sites (only needed once a test drives it).
    - Add `attr_set_path.prp` content that exercises deep paths +
      post-constprop conditional declarations collapsing to one
      effective write.

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

- **2q** Bit-level operators in LNAST + constprop: bit-range read
  (`x#[3..=6]`), bit-range write (`r#[0]=…`, `r#[3..=6]=…`,
  `r#[0..+1]=…`), reductions (`#|[..]`, `#&[..]`, `#^[..]`, `#+[..]`),
  sign/zero extension (`#sext[lo..=hi]`, `#zext[lo..=hi]`), and the
  `.[bits]` attribute query (e.g. `for i in 0..<x.[bits]`).
  - Reads `.[bits]`/`max`/`min` published by the typesystem (see
    `upass/upass.md §2 Typesystem`); bit-range ops also need the
    target's width to validate ranges.
  - Fixes failing comptime tests: `prp-bitreduce`, `prp-bitreverse`,
    `prp-bitset`, `prp-cellmap_comb` (`t#[0..=3]` bit-range read),
    `prp-cellmap_misc` (`a#sext[0..=7]`, `a#[4..=7]`), `prp-formux`
    (`sel#[0]=…` bit-range write), and the bit-range portion of
    `prp-valid_unknown_bits` (`v#[3..=6] == 0sb1?0`).
  - Current frontend symptom on most of these is a `lnastfmt` ref-text
    error (`'t#[0..=3]' is not a valid identifier`) — prp2lnast is
    leaving the bit-range syntax inside the ref instead of lowering
    to a dedicated bit-range op.

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

- **2n** `tree_ios` leaf ABI with `bits`/`min`/`max`/`signed` slots,
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

## Failing-test → TODO mapping (snapshot 2026-05-27)

Each `bazel test -c dbg //...` failure mapped to the TODO entry whose
landing should fix it. When more than one entry is listed, the test
needs all of them.

| Failing test | TODO entry | What it needs |
| --- | --- | --- |
| `prp-bitreduce` | [[2q]] | reductions `#\|`/`#&`/`#^`/`#+` |
| `prp-bitreverse` | [[2q]] | bit-range read/write (`t#[i]=…`) — frontend errors on the lvalue lowering |
| `prp-bitset` | [[2q]] | bit-range write `r#[i]=…`, `r#[lo..=hi]=…` |
| `prp-cellmap_comb` | [[2q]] | `t#[0..=3]` bit-range read (front-end currently errors `'t#[0..=3]' is not a valid identifier`) |
| `prp-cellmap_misc` | [[2q]] | `a#sext[0..=7]`, `a#[4..=7]` plus comptime fold of `__mux`/`__hotmux`/`__get_mask` direct calls |
| `prp-enum_color` | [[2l]], [[1k]] | enum with associated setters/methods |
| `prp-enum_hier` | [[2l]] | nested / hierarchical enums |
| `prp-enum_simple` | [[2l]] | base enum parsing + comptime tags |
| `prp-enum_types` | [[2l]] | typed enum interplay with `does` |
| `prp-fcall5b` | [[1r]] | nested fcall-return bundle unwrap (non-rename) |
| `prp-fcall_rename_deep` | [[1r]] | rename through deep fcall-return bundles |
| `prp-formux` | [[2q]] | `sel#[0]=…` bit-range write |
| `prp-hotmux_unique_if` | [[2r]] | comptime fold of `unique if` chain + `__hotmux` direct call |
| `prp-match_arms_mixed` | [[2r]] | mixed match-arm prefixes + tuple `case` patterns (verifier count mismatch) |
| `prp-setter_complex` | [[1k]] | decorator-init implicit setter dispatch on `x:Tup = (…)` |
| `prp-tuple_decorator_complex` | [[1k]] | tuple-decorator init triggering setter |
| `prp-valid_simple` | [[1p]] | 3-valued comparison + carry-expanding arithmetic |
| `prp-valid_unknown_bits` | [[1p]], [[2q]] | bit-range reads on partially-unknown values + carry expansion |
| `prp-wrap_checks` | wrap-policy reassignment (`x:u4:[wrap] = …` sequential type-changing writes) |
| `prp-wrap_complex` | wrap/sat policy fold on already-typed values |
