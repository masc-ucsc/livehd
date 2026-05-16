# TODO — Verilog input/output

Pending work on Verilog ingress (yosys-based parsing into LGraph) and
egress (`inou/cgen` Verilog emission), plus the synthesis-path closure
that validates `lnast_to_lgraph` against a Verilog golden.

Items use the same Group N letters as the master plan in [TODO.md](TODO.md).
Group letters are shared across [TODO_prp.md](TODO_prp.md),
[TODO_verilog.md](TODO_verilog.md), and [TODO_livehd.md](TODO_livehd.md),
so cross-file dependencies stay visible.

## Group 3 — depends on Group 2

- **3i** Finish `lnast_to_lgraph` and validate against a Verilog golden via
  `inou/cgen` round-trip (synthesis-path closure).

## Group 5 — polish / final

- **5e** `inou/cgen` Verilog egress: emit `// src: file:line:col` comments
  derived from the source map (see `docs/contracts/sourcemap.md` and
  [TODO_livehd.md](TODO_livehd.md) **1f**).

## Yosys ingress migration

The yosys-side import (`inou/yosys`) still rides on `@hif` /
`Sub_node`-based Lgraph. The per-file migration plan is in
`docs/contracts/yosys_migration_skeleton.md`: `lgyosys_tolg.cpp`,
`lgyosys_dump`, `lgyosys_fromlg`, `inou_yosys_api`, `yosys_driver`. Gated
on `pass/cprop` having landed on the HHDS substrate; cprop is done, yosys
migration is the next per-pass bite.
