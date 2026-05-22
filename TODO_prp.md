# TODO — Pyrope semantics

Pending work to finish correct Pyrope language handling: frontend, LNAST
production, attribute/type semantics, memory/register lowering, and test
infrastructure for the Pyrope path.

Items use the same Group N letters as the master plan in [TODO.md](TODO.md).
Items in the same group can be done in parallel; all letters in group N must
complete before group N+1 starts. Group letters are shared across
[TODO_prp.md](TODO_prp.md), [TODO_verilog.md](TODO_verilog.md), and
[TODO_livehd.md](TODO_livehd.md), so cross-file dependencies stay visible.

## Group 1 — foundation

- **1a** `tree_ios` leaf ABI with `bits`/`min`/`max` populated from the
  bitwidth upass — `import.md §Function signatures`. Mimic GraphIO for consistency.

- **1b** Two-phase `upass/func_extract` (parallel per-LNAST, then top-down
  resolve) — `import.md`.
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

- **1d** Source-derived SSA / tmp names (`foo_l42_a`, drop `___N`) —
  `docs/contracts/lnast_spec.md §13`.
- **1g** Import `docs/docs/pyrope` `cassert` examples into
  `inou/prp/tests/docs/` (`extract.rb` helper); iterate doc/Pyrope until they
  pass.
- **1h** Golden-output baseline in `inou/prp/tests/equiv/` adding more feature
  tests.
- **1i** Enum support (fix the failing tests).

## Group 2 — depends on Group 1

- **2b** LNAST bitwidth upass: ranges, wrap/saturate, publish `bits`/`signed`
  on demand — `lnast_bitwidth.md`.
- **2d** Unify `attr_set` / `tuple_set` variadic-path shape —
  `docs/contracts/lnast_spec.md §11`.
- **2f** `delay_assign` offsets (`0`=Q, `-1`=past, ref offsets, comptime
  const validation) — `docs/contracts/lnast_spec.md §15.2`.
- **2i** Type-check pass (no int/bool mix; bitwidth size checks; tuple/enum
  consistency).
- **2j** Enforce function-scope closure-capture rule (depends on imports
  landing in Group 1): a function body may reference an outer-scope name
  **only** when that name's bundle entry is comptime-constant by the time
  the bundle pre-pass reaches the function body. Three cases the scope
  lookup must distinguish:
  - Outer name is comptime → inline the constant at the use site
    (bundle/constprop already does this for regular comptime refs, so
    "captured from outer scope" needs no separate path).
  - Outer name is tainted (pending import) → do NOT inline, leave the
    ref intact, defer the function from func_extract spawn until a
    re-run of `pass.upass` (after import resolves) clears the taint.
  - Outer name exists but is non-comptime and not tainted → hard compile
    error at the function body's read site (do not silently inline,
    do not let func_extract spawn it).
  Verifier should also catch any lingering outer-scope ref inside a
  spawned function body that survived to the read-only phase.

## Group 3 — depends on Group 2

- **3g** Memory port tuple semantics + `pipestage` `lat`/`num` —
  `docs/contracts/attributes_spec.md §6`.
- **3j** Pyrope Memory / `Fflop` / clock-reset end-to-end: declaration
  syntax, lowering to LGraph `Memory` cell, multi-clock-domain checks,
  sync/async reset trees.

## Group 4 — depends on Group 3

- **4a** Test functionality on top of slop generation (`test::[…]` blocks,
  runtime harness).
- **4b** Emit `assert` / `assume` / `coverpoint` into slop.
- **4h** Compile-error test infrastructure: harness that pins expected errors
  / warnings to expected source spans (golden-error files), consumed by
  `inou/prp`, upass, and lgraph-pass diagnostics from
  [TODO_livehd.md](TODO_livehd.md) **3f**.
