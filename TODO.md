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

## Failing comptime tests — cluster snapshot (2026-05-30, live-verified)

`bazel test -c dbg //inou/... //upass/... //lnast/... //pass/...` sits at
**8 failures / 248 pass**, all in the `//inou/prp` comptime suite. Since the
prior snapshot, cluster **E** (wrap/sat/typed storage — `prp-wrap_checks`,
`prp-wrap_complex`, `prp-typesystem`, `prp-valid_unknown_bits`), most of
cluster **A** (`prp-bitreduce`, `prp-bitset`, `prp-cellmap_comb`,
`prp-cellmap_misc`, `prp-formux`), and cluster **C**'s `prp-hotmux_unique_if`
have all gone green. The residual failures cluster by root cause:

| Cluster | Tests | Root cause | Blocker / task |
|---|---|---|---|
| **A. Bit-range / bit-level ops** | `prp-bitreverse` | comb-inline landed (1i); residual is the `for i in 0..<x.[bits]` unroll (comptime `.[bits]`) + bit / storage-width fold | **1t** |
| **B. Enum semantics** | `prp-enum_color`, `prp-enum_hier`, `prp-enum_simple`, `prp-enum_types` | enum declarations / typed-variant matching / hierarchical scoping not implemented in prp2lnast or constprop | **2l** (`enum_color` also **1k**) |
| **C. Match / hotmux dispatch** | `prp-match_arms_mixed` | mixed match-arm prefixes (`case`+`==`) + tuple `case` patterns; one cassert not counted | **2r** |
| **D. Decorator-init / setter** | `prp-setter_complex`, `prp-tuple_decorator_complex` | decorator-init implicit setter dispatch on `x:Tup = (…)`: scalar→tuple coercion, positional setter-arg routing, `p.x`/`p['x']` getter dispatch | **1k** |
