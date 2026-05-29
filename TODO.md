# TODO

High-level pending tasks for LiveHD, split by area:

- [TODO_prp.md](TODO_prp.md) — Pyrope language semantics: frontend, LNAST
  production, attributes, bitwidth, memory/register lowering, test
  infrastructure for the Pyrope path.
- [TODO_verilog.md](TODO_verilog.md) — Verilog input (yosys ingress) and
  output (`inou/cgen` egress), plus the `lnast_to_lgraph` Verilog-golden
  validation that closes the synthesis path.
- [TODO_livehd.md](TODO_livehd.md) — LiveHD internal refactor: CLI, upass
  infrastructure, source-map machinery, LGraph cleanup, simulation/debug
  substrate, test reorg, benchmarks, HHDS-side optimizations.
- [TODO_hhds.md](TODO_hhds.md) — Pending work in the sibling `../hhds`
  repo that LiveHD depends on (forest API redesign, etc.). Implementation
  lands in `../hhds`; tracked here so cross-repo dependencies stay visible.

## Grouping and dependencies

Items are tagged with a Group N letter (e.g. **2b**, **3i**). The group
letters are shared across all four files, so cross-area dependencies stay
visible. Items in the same group can be done in parallel; all letters in
group N must complete before group N+1 starts.

Originally one flat plan ordered the groups 1→5; the split preserves the
original tags so referenced sections in `docs/contracts/` keep matching.

## Failing comptime tests — cluster snapshot (2026-05-27)

After landing **1p**, **1r**, partial **1k**, and partial **1c**, the
`bazel test -c dbg //...` failing-test set sits at **18 real** failures
(2 baseline drops via 1r; 2 transient parallel-build flakes —
`prp-string_hello`, `yosys_compile-cprop` — pass on retry). The residual
failures cluster by root cause:

| Cluster | Tests | Root cause | Blocker / task |
|---|---|---|---|
| **A. Bit-range / bit-level ops** | `prp-bitreduce` ✓, `prp-bitset` ✓, `prp-cellmap_comb`, `prp-cellmap_misc`, `prp-formux`, `prp-bitreverse`, `prp-valid_unknown_bits` | Bit-range read/write, reductions, popcount, and const-mask `#sext`/`#zext` **landed** (2026-05-28/29). Residual: non-const-mask `#sext` + storage width (**1t**), `.[bits]`-driven loops / comb-inline (**1i**), direct builtin `__cell` fold (**2s**, hlop sync) | **1t** + **1i** + **2s** |
| **B. Enum semantics** | `prp-enum_color`, `prp-enum_hier`, `prp-enum_simple`, `prp-enum_types` | Enum declarations / typed-variant matching / hierarchical enum scoping not implemented in prp2lnast or constprop | New entry (not in current TODO_prp.md Group 1); track separately |
| **C. Match / hotmux dispatch** | `prp-hotmux_unique_if`, `prp-match_arms_mixed` | Comptime fold of `unique if` chains + direct `__hotmux(...)` call; mixed match-arm prefixes (`case`+`==` mix) + tuple `case` patterns; verifier count mismatch | **2r** (Group 2) |
| **D. Decorator-init / setter** | `prp-setter_complex`, `prp-tuple_decorator_complex` | 1k partial landed (setter dispatch infrastructure + `cassert(self.v == "")` body folds). Missing: (1) scalar-to-tuple coercion for `x = "Padua"` on a `:Tup` var; (2) positional setter args routed from init tuple `(1, 2)` to setter params `(x, y)`; (3) getter dispatch for `p['x']` and `p.x` field access | **1k** extension (more invasive) |
| **E. Wrap / sat / typed reassignment + storage width** | `prp-wrap_checks`, `prp-wrap_complex`, `prp-typesystem`, `prp-valid_unknown_bits` (`ones == 0xff`) | `wrap`/`sat` on typed writes; `:uN`/`:sN` declarations don't enforce storage width — `v:u8` stays sign-extended so `v \| 0xff` → `-1` not `0xff`. (Former clusters E + F.) | **1t** |

`prp-valid_unknown_bits`'s bit-range casserts now pass (cluster A landed);
its one remaining fail is the `ones == 0xff` storage-width issue under
**1t**.
