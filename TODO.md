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

## Failing comptime tests — cluster snapshot (2026-06-01, live-verified)

`bazel test -c dbg //inou/... //upass/... //lnast/... //pass/...` sits at
**6 failures / 250 pass**, all in the `//inou/prp` comptime suite. The residual
failures cluster by root cause:

| Cluster | Tests | Root cause | Blocker / task |
|---|---|---|---|
| **A. Enum semantics** | `prp-enum_color`, `prp-enum_hier`, `prp-enum_simple`, `prp-enum_types` | enum declarations / typed-variant matching / hierarchical scoping not implemented in prp2lnast or constprop | **2l** (`enum_color` also **1k**) |
| **B. Decorator-init / setter** | `prp-setter_complex`, `prp-tuple_decorator_complex` | decorator-init implicit setter dispatch on `x:Tup = (…)`: scalar→tuple coercion, positional setter-arg routing, `p.x`/`p['x']` getter dispatch | **1k** |
