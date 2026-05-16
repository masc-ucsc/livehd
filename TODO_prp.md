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
- **2e** Replace `$ / % / #` prefixes with ST-backed direction/storage —
  `docs/contracts/lnast_spec.md §12`.
- **2f** `delay_assign` offsets (`0`=Q, `-1`=past, ref offsets, comptime
  const validation) — `docs/contracts/lnast_spec.md §15.2`.
- **2i** Type-check pass (no int/bool mix; bitwidth size checks; tuple/enum
  consistency).

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
