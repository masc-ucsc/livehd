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
**Group 0** was added later (2026-06-01) as a stabilization gate ahead of the
lnast→lgraph synthesis-path build-out — its items live in this master file (see
below) rather than the per-area files because they are a one-time prerequisite
referencing existing tasks.

## Group 0 — stabilize the base (prerequisite for the lnast→lgraph push)

Group 0 is the stabilization gate that must close before the LNAST→LGraph
synthesis-path build-out (TODO_verilog.md **3a** + the deferred [[1t]] T6
lowering) starts. These are not new features — they get the tree committed,
green, and on the clean value layer so the new translator builds on solid
ground. The `inou/cgen` Verilog round-trip is the translator's validator, so it
must be trustworthy first.

- **0a** Land the in-flight typesystem/typecheck WIP. ~12 modified files in the
  working tree (new `upass/typecheck/upass_typecheck.{cpp,hpp}` + coupled edits
  in `lnast/lnast_attrs.hpp`, `lnast/lnast.cpp`, `upass/bitwidth`,
  `upass/attributes`, `inou/prp/prp2lnast.cpp`, `upass/runner`, `upass/core`).
  The `upass/typecheck` pass is not yet tracked in any TODO; scope it, finish
  it, confirm `bazel build //...` green, then commit. Don't start
  `lnast_to_lgraph` on a dirty tree — the node/attr shapes the translator reads
  are still moving here.

- **0b** Establish a known-good test baseline. After 0a, re-run
  `bazel test -c dbg //inou/... //upass/... //lnast/... //pass/...` and record
  the pass/fail count (the ~248–249/8 snapshot below predates this WIP) so the
  new translator has a real regression reference. Account for the known-flaky
  [[1s]] intermittent comptime/string crash (~2–6% uninitialized-read on the
  string path) so a spurious fail is not read as a new regression.

- **0c** Finish the [[1g]] `Const`-args migration for the round-trip validator.
  `inou/cgen/cgen_verilog.cpp` still has 10 `to_i()` sites and
  `pass/cprop/cprop.cpp` has 7 (`inou/yosys` is already at 0). These break once
  the hlop pin is bumped to the version that protects the `int` shift/sext
  overloads (current pin `6a4ff795`). Migrate cgen onto the clean `Const` /
  `to_index` value layer *before* the translator leans on the cgen Verilog
  round-trip. Tail of [[1g]] (Goal 1); overlaps [[2t]].

- **0d** Confirm / migrate the remaining [[1g]] value-layer derive sites that
  feed bits/sign at cell creation: `upass/attributes/upass_attributes_read.cpp`
  derive-bits + range guards (~7), `upass/attributes.cpp` range guards,
  `upass/ssa` derive-bits (×4), `prp2lnast` mask positions + `bits_to_bounds`,
  `lnast_manager:150`. These compute the derived `bits`/`sign` the Stage-1
  translator reads, so confirm them correct (migrate the bits/range ones; the
  cosmetic ones can defer). `stringify_one`'s decimal formatter stays blocked on
  an arbitrary-precision decimal in hlop — not Group-0 critical.

**Exit criteria:** clean committed tree (0a), a recorded green baseline (0b),
and `inou/cgen` on the clean value layer (0c) so the round-trip harness is
trustworthy.

## Failing comptime tests — cluster snapshot (2026-05-30, live-verified)

`bazel test -c dbg //inou/... //upass/... //lnast/... //pass/...` sits at
**8 failures / 248 pass**, all in the `//inou/prp` comptime suite. The residual
failures cluster by root cause:

| Cluster | Tests | Root cause | Blocker / task |
|---|---|---|---|
| **A. Bit-range / bit-level ops** | `prp-bitreverse` | comb-inline landed (1i); residual is the `for i in 0..<x.[bits]` unroll (comptime `.[bits]`) + bit / storage-width fold | **1t** |
| **B. Enum semantics** | `prp-enum_color`, `prp-enum_hier`, `prp-enum_simple`, `prp-enum_types` | enum declarations / typed-variant matching / hierarchical scoping not implemented in prp2lnast or constprop | **2l** (`enum_color` also **1k**) |
| **C. Match / hotmux dispatch** | `prp-match_arms_mixed` | mixed match-arm prefixes (`case`+`==`) + tuple `case` patterns; one cassert not counted | **2r** |
| **D. Decorator-init / setter** | `prp-setter_complex`, `prp-tuple_decorator_complex` | decorator-init implicit setter dispatch on `x:Tup = (…)`: scalar→tuple coercion, positional setter-arg routing, `p.x`/`p['x']` getter dispatch | **1k** |
