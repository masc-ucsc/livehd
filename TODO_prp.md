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
    (`v#[3..=6] == 0sb1?0`), so its full pass requires [[2q]] and
    [[1v]] (`v.[bits]`) as well as this entry.
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

- **1v** LNAST basic typesystem redesign — **deferred**.
  Replaces the existing `__type` mess (which redundantly tracked
  `bits` / `pitch` / integer-set state, plus parallel signed/unsigned
  bit fields) with a clean, declaration-driven design. Subsumes the old "bitwidth upass" and
  "type-check pass" entries. Downstream consumers: bit-level ops
  [[2q]], `tree_ios` [[2n]], wrap/sat [[2m]] all read from this
  entry. Primary file: `lnast/bundle.hpp`.

  - **Tried:** Three landing attempts in the 2026-05-27 session
    (twice agent-typesys, once agent-1v-final). Each applied the
    corpus rewrite + declared-type-only `derive_max`/`derive_min`/
    `derive_bits` in `upass/attributes/upass_attributes_read.cpp`,
    built clean, hit ≤29 failing on first run; all three got
    reverted before committing. **Land atomically in a single
    commit, with no concurrent agent activity in
    `upass/attributes/` or the corpus `.prp` files** — the
    persistent revert pattern is the reason this didn't land.

  - **Core design** (user-confirmed via Q&A 2026-05-27, refined
    via the type-system redesign note):

    **Five basic types.** No implicit conversion across them;
    explicit only.
    - `integer`
    - `boolean`
    - `string`
    - `complex` — a copied bundle / tuple type
    - `enum` — 5th basic type, details deferred (stub the slot)

    **Integer representation.** Store only the constrained `max`
    and `min` from the declaration; drop tracking of "current"
    max/min along the path (the old bitwidth pass that computed
    current values becomes too complex — remove it).
    - `s<N>` / `u<N>` shorthand expands at parse time:
      `s5 → max=15, min=-16`; `u8 → max=255, min=0`.
    - Bare `integer` → no bound stored (open integer).
    - `.[bits]` is signed-bits-with-sign-bit-included, derived
      from max/min on demand (not stored).
    - **There is no `.[sbits]` / `.[ubits]`.** Signedness is
      encoded by `min` being negative; size queries use
      `.[bits]` only. Any legacy reads of those names must be
      rewritten to `.[bits]` / `.[max]` / `.[min]`.
    - Un-annotated declarations → `.[bits]`/`.[max]`/`.[min]`
      all return `nil` (not a compile error). `int(max=3)` pins
      only `max`; the other two stay nil.

    **String.** Opaque, compile-time only. No max/min, no
    length bound in the type.

    **Complex (tuple) types.** When a bundle is promoted to a
    type, **deep-copy** the entire bundle into `__type` — never
    reference by id (the source can go out of scope). The copy
    includes fields, attributes, default values, and methods
    (setter, getter, eq, lt, ge, …). Recursion: bundles may
    contain bundles; copy is fully recursive. Field-level
    `mut`/`const` declarations inside the copied bundle are
    preserved as authored — do not strip them, even when the
    outer variable is `const`. Constraint enforcement happens
    at **use**, not in the bundle.

    `t.[bits]` on a heterogeneous aggregate (any field with no
    numeric envelope, e.g. a string) is `nil`, not a partial
    sum. Per-field queries `t.a.[bits]` work if the field is
    annotated.

    **Type names.** A type has a stable name only when declared
    via `type Name = ...`. The name is stored inside the type
    bundle and exposed as `.[typename]`. Anonymous tuples have
    no typename. `typename` is **debug-only** — it affects
    diagnostics (better warnings/errors) but never behavior.
    Equality across two variables with different typenames but
    identical structure (e.g. `a:xx == b:yy`) succeeds via
    structural comparison; nominal identity is the job of `is`.

    **Variable-level attributes** (stored outside `__type`,
    four total): `mut`, `const`, `comptime`, `public`. No
    `private` — absence of `public` is the default. Variable-
    level attributes **dominate** field-level declarations: a
    `const` outer freezes all nested fields at runtime, even
    where they were declared `mut`. The reverse direction
    (mut outer, const inner field) preserves the field's
    const-ness — the field's mut/const lives in the type
    bundle's stored field metadata and is consulted on
    assignment.

    Worked example (both directions):
    ```
    const a = (mut a:u3 = 0, const b:u5 = 1)
    a.a = 7              // compile error: const outer freezes mut inner

    mut b:a = (a=3)      // borrows a's type; outer mut
    b.a = 7              // legal: mut outer + mut field
    b.b = 7              // compile error: field declared const
    ```

    **Lambda / function types** (comb, pipe, mod). `__type`
    stores only a reference to the LNAST tree (stream / node
    identifier). The signature (inputs, outputs) is read from
    that tree when needed — never duplicated inside the type.

    **Storage layout — hot vs. attribute-map.** Per-Entry hot
    fields are limited to the two slots the u-pass reads on
    every assignment:
    - `bool const_type` — outer mutability. Read on the
      mutability-chain walk.
    - `shared_ptr<Bundle const> type` — nullptr for basic
      types; populated for complex (a single canonical
      type-bundle borrowed by all `:Name` variables). Use
      `shared_ptr` so assignment copies are O(1) (ptr copy,
      no deep clone). The pointed-at bundle is treated as
      immutable once stamped.

    Everything else (`comptime`, `typename`, `max`, `min`,
    `public`, user attrs) lives in the existing attribute map.
    Two reasons:
    - Most of those are **non-sticky on copy**: assigning
      `mut x = comptime_y` must not make `x` comptime, and
      `x:foo = y:bar` must not retag `x` with bar's typename.
      Centralizing copy semantics in one place — the per-
      attribute "carry / drop on copy" table in the
      assignment helper — keeps that decision inspectable.
    - The `nil` case (no type, no bound, no name) is just
      "key absent" in the map, no `std::optional` plumbing.

  - **Implementation tasks** (in `lnast/bundle.hpp` and friends):

    1. **`lnast/bundle.hpp` — data structure.**
       - Add two hot fields to `Entry`:
         `bool const_type` (replaces / renames the existing
         `immutable`) and
         `std::shared_ptr<Bundle const> type` (nullptr for
         basic types; one shared canonical bundle per named
         complex type; immutable once stamped).
       - Tag the type kind via the bundle pointed-at (or
         nullptr for basic types). For basic types the
         `typename` attribute identifies `integer` / `boolean` /
         `string`; complex is "type ptr non-null"; enum and
         lambda-ref are stub tags inside the type bundle for
         now.
       - For integer variables: store `max`/`min` in the
         attribute map keyed at the variable's path; absent
         means unbounded.
       - Keep type metadata in the `.[attr]` namespace (which
         is syntactically distinct from `.field`) so it cannot
         collide with user field names.

    2. **LNAST traversal — type building.**
       - Build bundles as the tree is traversed.
       - On `type Name = ...`: deep-copy the RHS bundle ONCE
         into a freshly allocated canonical type-bundle
         (`shared_ptr<Bundle const>`), stamp `.[typename] = Name`
         inside it, and freeze it. Source may go out of scope —
         the type-bundle is self-contained.
       - On a typed declaration `mut x:Name = ...` /
         `const x:Name = ...`: set `Entry.type` to a copy of
         the shared_ptr to Name's canonical type-bundle. NO
         deep clone — just refcount bump.
       - On an anonymous-typed declaration `mut x:(a:u3,…) = …`:
         build the structural type-bundle inline, but do not
         set `typename`.
       - Set integer `max`/`min` from `s<N>`/`u<N>` shorthand
         or explicit `max=`/`min=` annotations. Leave both
         absent if unannotated.

    3. **New `upass/typesystem` pass — type/mut checking.**
       New u-pass that traverses the LNAST and populates the
       per-Entry hot fields (`const_type`, `type` ptr) plus
       the relevant attribute-map entries (`comptime`,
       `typename`, `max`, `min`). Subsumes the legacy bitwidth
       upass.
       - On every field assignment: walk the mutability chain
         — read `const_type` on the outer Entry, then walk the
         `type` bundle's stored field metadata along the dotted
         path. Error if any `const` ancestor exists (outer or
         field-level).
       - On every assignment: check RHS type is compatible
         with LHS via `does`.
       - Implement the four comparison operators:
         `does` (structural superset — ignores `typename`),
         `equals` (mutual `does`),
         `is` (nominal — compares `.[typename]`),
         `case` (`does` plus value match).
       - Assignment-copy semantics: copy `const_type` from
         the LHS's own declaration (NOT from the RHS); copy
         the `type` ptr if the LHS has no declared type;
         consult the per-attribute carry/drop table for
         `comptime`/`typename`/`max`/`min`/user attrs.
         Default for `comptime` and `typename` is **drop**
         (non-sticky); `max`/`min` follow the LHS type
         declaration.

    4. **Variable attributes.** Plumb `mut`/`const`/`comptime`/
       `public` through declaration parsing.
       - `const` → `Entry.const_type = true` (hot field).
       - `comptime` and `public` → attribute-map entries
         (non-sticky on copy; per-attribute rule in the
         carry/drop table).
       - `mut` is the absence of `const` — no separate slot.

    5. **Attribute accessors (`.[attr]`).** At minimum support
       reading:
       - `.[typename]`
       - `.[max]`, `.[min]` (integer; stored)
       - `.[bits]` (derived on demand from max/min;
         signed-bits-with-sign-bit-included). No `.[sbits]` or
         `.[ubits]` — neither stored nor derived.
       - `.[fields]`, `.[size]`, `.[id]` (introspection on
         complex types; `.[size]` is entry count, NOT bit
         width)

    6. **Remove / migrate the legacy type system.**
       - Sweep readers of legacy attributes (`__type` string
         values, `__bits`, `__pitch`, the legacy parallel
         signed/unsigned bit fields, …); list them.
       - Migrate each impacted pass to the new attribute model.
       - Remove the legacy bitwidth pass that computed current
         max/min.
       - Decide whether any narrowing/inference is still needed
         for open integers under the new model.

  - **Corpus rewrites** (must land in the same commit):
    - Replace every legacy unsigned/signed-bits corpus read
      with `.[bits]` / `.[max]` / `.[min]` as appropriate. Known
      offenders (grep "ubits\|sbits"): `tuple_simple_attr.prp`,
      `phase2_attr_bits.prp`, `phase2_attr_max_min.prp`,
      `wrap_complex.prp`, `wrap_trivial.prp`, `typesys_range.prp`
      comment text. Adjust expected values for the always-signed
      `.[bits]` semantics; reads on unannotated decls become
      `nil`.
    - Drop the aggregate `t.[bits] == sum-of-fields` assertion
      in `phase3_aggregate_bits.prp`; keep per-field queries
      and add the heterogeneous-aggregate poison case.
    - Wire tuple-literal per-field type info from
      `(a:u3=5, b:u2=2, c:u7=100)` declarations through to
      `t.a.[bits]` lookups (agent-typesys flagged this as the
      `prp2lnast` gap that breaks `phase3_aggregate_bits`).
    - New phase-8 test files already landed in
      `inou/prp/tests/comptime/phase8_*.prp` (basic, partial,
      size-vs-bits, typename+debug, untyped) — keep these
      passing under the new derivation rules.
    - Add a phase-9 corpus covering the const-outer / mut-inner
      and the mut-outer / const-inner-field cases (the
      `const a = (mut a:u3 = 0, const b:u5 = 1)` example),
      plus the structural-equality case `a:xx == b:yy` across
      different typenames.
    - Tests this should fix once landed: `prp-typesys_range`,
      `prp-wrap_checks`, `prp-wrap_complex`, `prp-bitreverse`.

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
  - Depends on [[1v]] (typesystem upass): `.[bits]`/`max`/`min` are
    published by the typesystem; bit-range ops also need the target's
    width to validate ranges.
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
  - Depends on [[1v]] (typesystem upass): typesystem publishes
    `bits`/`min`/`max`/`sign`, which the ABI carries.

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
| `prp-bitreverse` | [[2q]], [[1v]] | bit-range read/write + `.[bits]` |
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
| `prp-string_escapes` | [[2r]] | `\xNN` hex, raw vs cooked, interpolation with escaped quote, `.[size]` |
| `prp-tuple_decorator_complex` | [[1k]] | tuple-decorator init triggering setter |
| `prp-typesys_range` | [[1v]] | typesystem `.[bits]`/`.[min]`/`.[max]` upass |
| `prp-valid_simple` | [[1p]] | 3-valued comparison + carry-expanding arithmetic |
| `prp-valid_unknown_bits` | [[1p]], [[2q]], [[1v]] | bit-range reads on partially-unknown values + carry expansion + `.[bits]` |
| `prp-wrap_checks` | [[1v]] | min/max range checks |
| `prp-wrap_complex` | [[1v]] | min/max wrap composition |
