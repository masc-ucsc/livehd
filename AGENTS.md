# AGENTS.md

## Build & Test

- **Build**: `bazel build -c dbg //...`
- **Test**: `bazel test //...`
- **lgshell**: `./bazel-bin/main/lgshell`
- C++ formatted with `clang-format`

## Key Directories

- `lgraph/`: LGraph IR — nodes (logic, muxes, registers, memories, sub-graphs), pins (driver/sink), edges
- `lnast/`: LNAST high-level IR
- `inou/yosys/`: Yosys integration (`lgyosys_tolg.cpp` = Yosys→LGraph, `inou_yosys_read.ys` = Yosys script)
- `inou/cgen/`: Verilog code generation from LGraph
- `pass/cprop/`: Constant propagation pass
- `ware/rtl/`: Memory RTL modules (`cgen_memory_*.v`, `cgen_memory_multiclock_*.v`)

## lgshell / EPRP

Pipe-based: `command |> next_command`. Example:
```
inou.yosys.tolg top:foo files:foo.v |> pass.cprop |> inou.cgen.verilog odir:out
```

## Compiler Warnings Policy

Always fix source code — never add `-Wno-*` flags to BUILD files. Exception: external deps in `MODULE.bazel`.

## Running Pyrope Tests

- **Single test (harness)**: `python3 inou/prp/tests/pyrope_test.py -i inou/prp/tests/<dir>/<test>.prp`
- **Direct pipeline (comptime)**: `./bazel-bin/main/lgshell "inou.prp files:<test>.prp |> pass.lnastfmt |> pass.upass constprop:1 max_iters:1 verifier_pass:1 verifier_fail:0 |> pass.lnastfmt |> lnast.dump"`
- Test header `:type:` selects the pipeline (`parsing`, `lnast`, `upass`, `comptime`, `lgraph`, `compile`); `:verifier_pass:` / `:verifier_fail:` set expected cassert tallies for `comptime`.

## Debugging Yosys-to-LGraph Flow

### Running Yosys tests
- **Single test**: `rm -rf lgdb_yosys tmp_yosys tmp_yosys_mix && ./inou/yosys/tests/yosys_compile.sh ./inou/yosys/tests/<test>.v`
- **Full suite**: `rm -rf lgdb_yosys tmp_yosys tmp_yosys_mix && ./inou/yosys/tests/yosys_compile.sh`
- **Important**: Always clean `lgdb_yosys` between runs — stale state causes false failures when submodule names collide across tests.

### Inspecting intermediates
- **Yosys RTLIL dump**: After running tolg, check `pp.il` for what Yosys produced (cell types, port connections, parameters).
- **LGraph dump**: `./bazel-bin/main/lgshell "inou.yosys.tolg top:<top> files:<file> |> pass.cprop |> lgraph.dump"` — grep output for cell types (e.g., `grep -i mem`).
- **Generated Verilog**: Check `tmp_yosys/<module>.v` for codegen output; `tmp_yosys_mix/all_<top>.v` for the concatenated file used by LEC.

### Yosys memory pass
- `inou/yosys/inou_yosys_read.ys` controls the Yosys script. The `memory -nomap` pass collects `$memrd`/`$memwr`/`$meminit` into `$mem_v2` cells without decomposing into DFFs. Using plain `memory` decomposes small memories into DFFs+muxes, bypassing LGraph memory handling.

### Memory cell types
- Modern Yosys produces `$mem_v2` (not `$mem`). Match both: `cell->type == "$mem" || cell->type == "$mem_v2"`.
- Do NOT use `strncmp("$mem", 4)` — it catches `$memrd`/`$memwr`/`$meminit` which have different port structures.
- Memory RTL modules are in `ware/rtl/cgen_memory_*.v`. The `multiclock` variants have per-port clock inputs instead of a shared `clk`.
