# inou/slang — direct SystemVerilog → LNAST reader

The direct `--reader slang` front-end: slang v11 elaborates SystemVerilog and
`Slang_context` lowers the AST straight to LNAST, one Lnast per module in the
extracted unit form (`top -> [io, stmts]`, `lambda_kind="mod"`, exact Verilog
module names) so the standard upass pipeline (SSA → io_meta → tolg) compiles
it like any Pyrope unit. It is NOT a replacement for the `yosys-verilog` /
`yosys-slang` production readers; it exists so `ln:`/`lnast-dump:` flows can
ingest SystemVerilog directly (todo/ 2s).

## Passing raw slang driver args (`-- ...`)

Everything after a `--` on the `lhd` command line rides straight to the slang
driver, so the standard slang options work — in particular `-F`/`-f` command
files (verilog file lists):

```
lhd compile --reader slang --top gcd -- -F ../simplechisel/build_gcd/filelist.f
```

The sources may come entirely from the file list, so a positional
`verilog:`/`*.v` input is optional in that case (`files` and the raw flags are
both accepted; at least one must supply sources). lhd hands the args to
`inou.slang` as the `slang_flags` label (`\x1f`-separated so shell tokens like
`+incdir+a,b` survive). Covered by `tests/slang_filelist.sh`
(`bazel test //inou/slang:slang_filelist`).

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

- `lec` — slang→LNAST→tolg→cgen Verilog, LEC-checked (`lhd lec --set lec.solver=lgyosys`) against
  the source. The strongest tier.
- `verilog` — compiles to Verilog; a known LEC gap is tracked in the ladder
  comment next to the entry.
- `lnast` — LNAST + `ln:` save/reload round-trip only.
- `error` — must fail cleanly with a structured diagnostic (used for the
  non-LRM sources slang correctly rejects, e.g. procedural writes to nets).

Run one: `bazel test //inou/slang:slang_compile-<name>`; all:
`bazel test //inou/slang:all`.

Current tally (2026-06-17, 116 sources): **86 lec**, 7 verilog, 2 lnast,
21 error (9 of those are non-LRM sources slang correctly rejects, e.g.
procedural writes to nets; the rest are tracked feature gaps: instance arrays,
hierarchical punch-through references, `'bx` golden arms, dynamic
mem-element part-selects). The 7 `verilog`-capped entries are LEC-slow or
genuine gaps: four big-memory / wide-arith tests (`long_mem`, `long_mem3`,
`fixme_mem_offset`, `long_nocheck_iwls_square`) and `fixme_sha256`'s wide
reduction are deliberately capped because LEC is slow there (the small-array
coverage simple_rf1/rf2, tuplish, fixme_array carries the memory guarantee);
`mem_sync_init` and `nocheck_slang_foreach` are real memory-lowering gaps.
Correct mixed signed/unsigned arithmetic and narrow (1- and 2-bit) signed
port/temp ranges (`add1`, `issue_047`, …) are now LEC-verified — a signed
operand in an unsigned expression zero-extends, and `materialize_conversion`
plus `int_min_str`/`int_max_str` carry the 1800 §11.8.2 sign rules.

The `tests/verilog/` sky130 cell samples run at the `lnast` tier through the
legacy no-argument `slang_compile.sh` mode (`slang_compile_sky130` target).

## Which Verilog constructs to support (triage rule)

When a SystemVerilog construct fails to lower, decide whether it is a bug to fix
or a test to drop using three reference points — the standalone slang frontend,
the yosys-slang plugin, and our native reader:

1. **slang native frontend rejects it → out of scope, for sure.** slang v11 is
   the strictest valid-SV gate; if its frontend cannot elaborate the construct it
   is either illegal SV or non-elaboratable, so `--reader slang` has nothing
   well-formed to lower. Drop it (or keep it as an `error`-tier test when slang
   rejects it *cleanly*).
2. **yosys-slang handles it → we must support it too.** A construct the
   yosys-slang plugin lowers is a real, synthesizable RTL idiom; both
   `--reader slang` and `--reader yosys-slang` should match it. A failure here is
   a genuine bug to fix.
3. **yosys-slang fails but slang native succeeds → it depends.** The construct is
   valid SV (the frontend accepts it) but the yosys-slang plugin's RTLIL
   conversion cannot handle it. Try to support it in our native lowering —
   unless it is a non-synthesizable construct (`$foo` system tasks,
   simulation-only strangeness), in which case dropping the test is fine.

### Running the three checks

```
# 1. standalone slang frontend  (valid-SV gate; NOT always installed locally)
slang foo.sv

# 2. yosys + the yosys-slang plugin, no LiveHD  (plugin path is bazel-mangled;
#    `find bazel-bin/external -name slang.so` if the +http_archive+ form moves)
./bazel-bin/inou/yosys/yosys2 \
  -m ./bazel-bin/external/+http_archive+yosys_slang/slang.so \
  -p "read_slang foo.sv"

# 3a. LiveHD native reader (slang-library based)
lhd compile --reader slang       foo.v --emit-dir pyrope:out/
# 3b. LiveHD yosys-slang reader
lhd compile --reader yosys-slang foo.v --emit-dir verilog:out/
```

**When standalone `slang` is not installed**, use `--reader slang` as a proxy for
check 1: the native reader embeds the same slang v11 frontend, so a clean
elaboration that merely hits a lowering gap surfaces as an `unsupported-*`
diagnostic (`"pass":"inou.slang","category":"unsupported"`), whereas a frontend
rejection surfaces with slang's own diagnostic code (e.g.
`AssignmentPatternNoContext`). So an `unsupported-*` from `--reader slang` ≡
"slang native succeeds" (case 2/3); a slang frontend code ≡ "slang native
rejects" (case 1).

Worked examples (`inou/prp/tests/equiv/`), both now supported by `--reader slang`:

| test | standalone slang | yosys-slang | `--reader slang` | equiv test |
|---|---|---|---|---|
| `hier_value.v` (`HierarchicalValue`) | ✅ | ✅ | ✅ (read as a `ValueExpressionBase`, `slang_expr.cpp`) | `:type: equiv` (prp-equiv + prp-v2prp round-trip) |
| `assign_pattern.v` (`SimpleAssignmentPattern` lvalue) | ✅ | ❌ | ✅ (`assign_to_pattern`, `slang_lvalue.cpp`) | `:type: equiv_slang` (yosys can't read the `'{...}` lvalue) |

### equiv testing: `:type: equiv` vs `:type: equiv_slang`

`inou/prp/tests/equiv/<name>.{prp,v}` golden pairs are LEC'd top-only. The `.prp`
header's `:type:` selects how the readers are exercised:

- **`:type: equiv`** (default, `prplib.run_equiv`): the `.prp` lowers to Verilog
  and lgchecks against the golden `.v` (yosys `read_verilog`). The matching
  `prp-v2prp-<name>` target additionally round-trips the `.v` through
  `--reader slang` → Pyrope → recompile → LEC, so the native reader is exercised
  too. Use when yosys can read the `.v` (e.g. `hier_value.v`, plain module name).
- **`:type: equiv_slang`** (`prplib.run_equiv_slang`): for a `.v` that yosys's
  `read_slang`/`read_verilog` cannot ingest (e.g. `'{...}` assignment-pattern
  lvalues in `assign_pattern.v`). The `.v` is read by the NATIVE `--reader slang`
  into clean cgen Verilog (implementation) and lgcheck'd against the
  `.prp`-generated Verilog (reference) — so only `--reader slang` is on the hook,
  and no yosys read of the original `.v` is needed.

The `:pyrope_top:`/`:verilog_top:` header tags pin the (differently-named) top on
each side. Pick `equiv` when both readers can read the `.v`; `equiv_slang` when
only the native slang reader can.
