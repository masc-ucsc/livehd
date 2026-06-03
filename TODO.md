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
**Group 0** was added later (2026-06-01) as a one-time stabilization sweep ahead
of the lnast→lgraph synthesis-path build-out; it has since fully landed (see
below) and carries no open items.

## Group 0 — stabilize the base (complete)

Group 0 was a one-time stabilization sweep ahead of the LNAST→LGraph
synthesis-path build-out (TODO_verilog.md **2d**). It has fully landed — the
typesystem/`upass/typecheck` WIP is committed and wired, the green baseline is
recorded (cluster snapshot below), and the [[1g]] value-layer derive +
`Const`-args migration is done across `upass/attributes/*`, `upass/ssa`,
`prp2lnast`, `inou/cgen`, and `pass/cprop`. No open items; any residual
`to_i()` cleanup at the next hlop pin bump rides under [[1g]] (Goal 1).

## Failing tests — cluster snapshot (2026-06-03)

`bazel test //inou/prp:...` shows **9 failures**: 6 in the comptime suite
(enum + decorator) and 3 in the `tests/errors/` negative suite (expected
diagnostic never emitted). The failures cluster by root cause:

| Cluster | Tests | Root cause | Blocker / task |
|---|---|---|---|
| **A. Enum semantics** | `prp-enum_color`, `prp-enum_hier`, `prp-enum_simple`, `prp-enum_types` | enum declarations / typed-variant matching / hierarchical scoping not implemented in prp2lnast or constprop | **2l** (`enum_color` also **1k**) |
| **B. Decorator-init / setter** | `prp-setter_complex`, `prp-tuple_decorator_complex` | decorator-init implicit setter dispatch on `x:Tup = (…)`: scalar→tuple coercion, positional setter-arg routing, `p.x`/`p['x']` getter dispatch | **1k** |
| **C. Error detection** | `prp-mixed_precedence`, `prp-invalid_descending_range`, `prp-underflow_unsigned_expr` | documented Pyrope errors not yet detected: operator precedence (`3 & 4 * 4`), descending range (`5..=0`), signed-negative-into-unsigned value-fit | **1e** (`underflow_unsigned_expr` extends **1b**) |

Cluster C shrank from 9→3 since the 2026-06-02 snapshot: tuple-parens detection
(`tuple_lhs_requires_parens`, `tuple_rhs_requires_parens`, `drop_tuple_parens_assert`),
`duplicate_tuple_field`, `undefined_read`, and `invalid_binary_prefix` now pass.
`check_parse_errors` also now reports statement-position `ERROR` nodes (stray tokens
before a block, e.g. `i c { … }`) — new passing test `prp-bad_if`; see
[[prp_error_node_syntax_detection]]. Fixing those exposed two stale valid-suite
tests that had only "passed" by error-swallowing — `equiv/accum` (stray `pub`) and
`verification/assert_ifelse` (`match` with no `else` + positional fcall args) — both
repaired and passing.
