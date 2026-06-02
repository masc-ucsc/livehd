# TODO — Verilog input/output

Pending work on Verilog ingress (yosys-based parsing into LGraph) and
egress (`inou/cgen` Verilog emission), plus the synthesis-path closure
that validates `lnast_to_lgraph` against a Verilog golden.

Items use the same Group N letters as the master plan in [TODO.md](TODO.md).
Group letters are shared across [TODO_prp.md](TODO_prp.md),
[TODO_verilog.md](TODO_verilog.md), [TODO_livehd.md](TODO_livehd.md),
and [TODO_hhds.md](TODO_hhds.md), so cross-file dependencies stay visible.

## Group 2 — depends on Group 1

- **2d** Finish the LNAST→LGraph synthesis closure (the rest of `lnast_to_lgraph`
  beyond the combinational milestone). The terminal `tolg` sub-pass + the
  `:type: equiv` harness landed 2026-06-02 and `trivial_if` LEC-matches its
  golden; this entry tracks the **remaining functional phases** (the original
  Group-3 `3a` work, re-homed here). The how-to-lower spec is
  `docs/contracts/lnast2lgraph.md` (cell vocabulary, pin conventions, nil/bool
  model, reg/flop + memory wiring, attributes; phases 4–11). What's already
  done: graph IO, `Sum`/`const`, `Get_mask` bit-slices, `if`/`elif`/`else` →
  binary `Mux`, and the per-function-tree → one-LGraph model (graph/module name
  = `<file-stem>.<entity>`, every function assumed `pub`; see [[1m]]).

  **Remaining slices** (roughly the contract phase order):
  - **Registers / `defer`** → `Flop` (reset/clock pin defaults, last-write-wins
    `din` + OR-of-conditions `enable`). Needs [[3j]]'s declare-folding (init/reset
    value bound on the `declare` node so it survives to `Flop`/`Memory`).
  - **`Memory`** for persistent indexed `reg` arrays (one Memory per logical
    storage, 11-pin port stride).
  - **High-level op lowering**: slice/concat, `!=`/`<=`/`>=`, div/mod
    (power-of-two `SRA`/`Get_mask`, generic `Sub` fallback + non-synth warning).
  - **`uif`** chains (add the `HotMux` FIXME) and direct `__<cell>` calls.
  - **`Sub` submodule calls** against the callee `tree_io` leaf ABI ([[2n]]) +
    import handling ([[1m]]/[[2o]]); until those land `tolg` must hard-error
    (not guess) on any callee with `Inline_reason != none`.
  - **Attributes**: read `max`/`min` HHDS attrs at node creation (LNAST-origin
    path); diagnose leftover LNAST-only attrs.
  - Optional cross-module global/specialization pass (contract phase 10) — the
    natural tail; may split into its own entry if it grows.

  **Test-infra follow-ups**: (a) a bazel CI target for `tests/equiv/*.prp` —
  needs a non-colliding name vs the existing comptime `prp-trivial_if`, and
  `inou/yosys/lgcheck` + `yosys2` reachable from the test sandbox (Yosys may be
  absent there; the manual `pyrope_test.py -i` harness is the runner until
  then). (b) Reconcile `lnast2lgraph.md §1`/`§16`, which still describe a
  standalone `pass.lnast_to_lgraph`, to the landed terminal-`tolg`-sub-pass
  packaging. Implementation notes for the landed milestone live in the memory
  note `task_1l_tolg_landed.md`.

## Group 3 — depends on Group 2

- **3b** Use the stable Pyrope as example for LNAST generation, and migrate
  inou.slang to use the same LNAST structures and keep adding verilog tests
  (inou/prp/tests/equiv)

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
