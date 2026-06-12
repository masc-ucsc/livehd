# inou/slang — direct SystemVerilog → LNAST reader

The direct `--reader slang` front-end: slang v11 elaborates SystemVerilog and
`Slang_context` lowers the AST straight to LNAST, one Lnast per module in the
extracted unit form (`top -> [io, stmts]`, `lambda_kind="mod"`, exact Verilog
module names) so the standard upass pipeline (SSA → io_meta → tolg) compiles
it like any Pyrope unit. It is NOT a replacement for the `yosys-verilog` /
`yosys-slang` production readers; it exists so `ln:`/`lnast-dump:` flows can
ingest SystemVerilog directly (todo/ 2s).

## File map (CIRCT ImportVerilog-shaped split)

| file | concern |
|---|---|
| `inou_slang.cpp` | EPRP entry (`inou.slang` / `inou.verilog`), per-file driver loop, options |
| `slang_driver.cpp` | embedded slang v11 driver; diagnostics → `diag::Sink` |
| `slang_context.{hpp,cpp}` | the one `Slang_context` class: naming, const-eval tiers, bool/int kind discipline, provenance, diags |
| `slang_structure.cpp` | modules, ports/io, process classification (comb/ff/async-reset), dataflow-ordered driver emission, instances, generate |
| `slang_stmt.cpp` | statements: if, case→`if`/`unique_if`, capped slang-side loop unrolling |
| `slang_expr.cpp` | rvalue expressions; reductions expanded inline; selects via shift+const-mask |
| `slang_lvalue.cpp` | assignment targets: vars, packed selects (const→`set_mask`, dynamic→RMW), concat split, memory element writes |
| `slang_types.cpp` | type info + the single materialize-conversion seam (`trunc_to`/`fit_wrap`/`to_pattern`) |
| `slang_location.hpp` | slang SourceRange → diag span + hhds SourceId minting |
| `slang_diag.hpp` | slang DiagnosticClient → LiveHD diag sink |

Key invariants the lowering maintains:

- every lowered value sits in the integer range of its slang type; width/sign
  changes go through `materialize_conversion` only;
- comparison/logical results are LNAST **bool**; anything reaching an integer
  context materializes to 0/1 (`to_int_value`) — and single-bit `get_mask` is
  never used for value extraction (Dlop's `x#[i]` is a −1/0 boolean);
- drivers (assigns/processes/instances) emit in dataflow dependency order so
  wire reads are plain; combinational cycles fall back to settled reads
  (`delay_assign +1`, LNAST-tier only);
- registers are `declare(…,'reg')` hoisted to module start (an `output reg`'s
  q pin IS the output); non-`clk/clock` or negedge clocks ride per-reg
  `clock_pin`/`posclk` attrs; extracted async-reset rungs become
  `initial`/`reset_pin`/`sync`/`negreset` attrs;
- unpacked arrays lower to the `comp_type_array` declare + `store(mem,idx,v)`
  / `tuple_get(d,mem,idx)` memory vocabulary with `fwd=0` (Verilog
  nonblocking reads see old contents).

## The coverage ladder (todo/ 2s subtask E)

`slang_ladder.bzl` pins EVERY `inou/yosys/tests/*.v` source to its
strongest passing tier; `tests/slang_compile.sh <tier> <file>` enforces it
both ways (a regression fails, and an outgrown `error` entry fails until the
ladder is promoted). Tiers:

- `lec` — slang→LNAST→tolg→cgen Verilog, LEC-checked (`lhd check`) against
  the source. The strongest tier.
- `verilog` — compiles to Verilog; a known LEC gap is tracked in the ladder
  comment next to the entry.
- `lnast` — LNAST + `ln:` save/reload round-trip only.
- `error` — must fail cleanly with a structured diagnostic (used for the
  non-LRM sources slang correctly rejects, e.g. procedural writes to nets).

Run one: `bazel test //inou/slang:slang_compile-<name>`; all:
`bazel test //inou/slang:all`.

Current tally (2026-06-12, 110 sources): **70 lec**, 16 verilog, 24 error
(9 of those are non-LRM sources slang correctly rejects, e.g. procedural
writes to nets; the rest are tracked feature gaps: instance arrays,
hierarchical punch-through references, `'bx` golden arms, dynamic
mem-element part-selects). Three LEC-capable big-memory tests (`long_mem`,
`long_mem3`, `fixme_mem_offset`) are deliberately capped at `verilog`:
memory LEC is slow on big arrays, and the small-array coverage
(simple_rf1/rf2, tuplish, fixme_array) carries the equivalence guarantee.

The `tests/verilog/` sky130 cell samples run at the `lnast` tier through the
legacy no-argument `slang_compile.sh` mode (`slang_compile_sky130` target).
