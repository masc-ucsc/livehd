# AGENTS.md

> **Doc map:** [`STRUCTURE.md`](STRUCTURE.md) describes where all documentation
> lives. In short — current/pending work is the [`todo/`](todo/index.html) hub
> (one HTML page per task); the human-readable LiveHD/Pyrope reference (the
> contract) is the [docs site](https://masc-ucsc.github.io/docs/) (`../docs`);
> directory specifics live in `<dir>/README.md`; and this file holds agent
> build/test/debug how-to plus the change-gated coding rules.

## Build & Test

- **Build**: `bazel build -c dbg //...`
- **Test**: `bazel test //...`
- **lhd CLI**: `./bazel-bin/lhd/lhd` — the only driver, for all flows
  (`lhd help`, `lhd describe <cmd>`); `lhd lsp` serves the Pyrope LSP. The
  old `lgshell` REPL was **removed** (2026-06-04)
- C++ formatted with `clang-format`
- **Contract tests/benchmarks**: Any test or benchmark file whose name contains the word `contract` is immutable to the coding agent. Do NOT modify these files — they define the expected behavior contract. If a contract test fails, fix the implementation, not the test.

## Sibling Repositories (DO NOT search the filesystem for these)

LiveHD depends on several sibling repos. **Always look in these exact paths — do NOT run `find`, `fd`, `locate`, or any other filesystem search to find them or files inside them:**

- `../hhds/` — HHDS library (Graph, Tree, Forest, flat_storage attributes). Headers: `hhds/graph.hpp`, `hhds/tree.hpp`, `hhds/attr.hpp`.
- `../hlop/` — HLOP library (dlop / slop helpers).
- `../iassert/` — iassert library (`I(...)`, `GI(...)` invariant macros).
- `../tree-sitter-pyrope/` — Tree-sitter grammar for Pyrope. The grammar lives at `../tree-sitter-pyrope/grammar.js`; generated parser sources are under `../tree-sitter-pyrope/src/`.

**Rule:** if you need anything from one of these libraries (e.g. `grammar.js`, `tree.hpp`, `graph.hpp`, `attr.hpp`), open it directly at the path above. Do **not** start a `find / ...`, `find . ...`, or recursive `grep` to locate them — they are always at these fixed sibling paths and are configured as `local_path_override` in `MODULE.bazel`. Running a broad `find` is wasteful and frequently the wrong tool; go straight to the known path.

## Key Directories

- `graph/`: LGraph IR (HHDS-backed) — LiveHD cell semantics + per-pin attributes + graph library over `hhds::Graph` (nodes, pins, edges). See `graph/README.md`. (This is the old `lgraph/`, now migrated onto HHDS.)
- `lnast/`: LNAST high-level IR (HHDS-backed `hhds::Tree` + flat-storage attributes)
- `parser/`: AST built on `hhds::Tree`
- `inou/yosys/`: Yosys integration (`lgyosys_tolg.cpp` = Yosys→LGraph, `inou_yosys_read.ys` = Yosys script)
- `inou/cgen/`: Verilog code generation from LGraph
- `pass/cprop/`: Constant propagation pass
- `ware/rtl/`: Memory RTL modules (`cgen_memory_*.v`, `cgen_memory_multiclock_*.v`)

## Tree library

LiveHD's tree IRs (LNAST, parser AST) sit on top of HHDS (`@hhds//hhds:core`,
headers `hhds/tree.hpp`, `hhds/attr.hpp`). New tree code should use
`hhds::Tree` + `hhds::Forest` and attach per-node payload via `flat_storage`
attribute tags — see `lnast/lnast_attrs.hpp` for the pattern. Pass-local
state should live in `absl::flat_hash_map<Tree_class_index, T>` side maps
rather than registering throwaway attributes. Legacy `core/lhtree.hpp` is
gone; do not reintroduce `lh::tree` / `lh::Tree_index`.

## lhd CLI (EPRP underneath)

One stateless invocation per flow; pass flags ride `--set pass.flag=value`
(or a `--config lhd.toml`), outputs are typed `--emit`/`--emit-dir` slots,
per-step logs land under `--workdir`. Examples:
```
lhd compile foo.v --reader yosys-verilog --top foo --recipe O1 --emit verilog:out.v
lhd compile foo.prp --emit-dir lg:foo_lgs/ --emit-dir lnast-dump:dumps/
lhd check --impl verilog:out.v --ref verilog:foo.v --top foo
```
Internally lhd drives the registered EPRP methods (conceptually the pipe
`inou.yosys.tolg |> pass.cprop |> inou.cgen.verilog`); pass/inou names in
`--set` and the step logs use that vocabulary.

## Compiler Warnings Policy

Always fix source code — never add `-Wno-*` flags to BUILD files. Exception: external deps in `MODULE.bazel`.

## Contracts

### Compiler warning options

Unless the user explicitly indicates otherwise, do **not** change compiler
warning options to make warnings or errors go away. Always fix the source
code instead.

This includes (non-exhaustive):

- Adding or modifying `-W*`, `-Wno-*`, `-Werror`, `-pedantic`, or `-w` flags
  in `BUILD`, `BUILD.bazel`, `*.bzl`, or any other build configuration.
- Removing warning flags from `tools/copt_default.bzl` or a target's `copts`.
- Disabling diagnostics via `#pragma GCC diagnostic` / `#pragma clang diagnostic`
  pushes around live code.

The only built-in exception is `MODULE.bazel` (external-dep warning
suppression — see "Compiler Warnings Policy" above).

Enforced by `scripts/contracts/diff_no_compile_flags_touched.sh`.

## Running Pyrope Tests

- **Single test (harness)**: `python3 inou/prp/tests/pyrope_test.py -i inou/prp/tests/<dir>/<test>.prp`
- **Direct pipeline (comptime)**: `./bazel-bin/lhd/lhd compile <test>.prp --set upass.verifier=true --set upass.verifier_pass=1 --set upass.verifier_fail=0 --emit-dir lnast-dump:dumps/ --workdir w` (the post-upass LNAST text lands in `dumps/*.lnast`; uPass stdout diagnostics in `w/logs/*pass_upass*.log`)
- Test header `:type:` selects the pipeline (`parsing`, `lnast`, `upass`, `comptime`, `lgraph`, `compile`); `:verifier_pass:` / `:verifier_fail:` set expected cassert tallies for `comptime`.
- **Expected-failure tests** (`:type: error`, in `inou/prp/tests/errors/`): the program **must** emit a compile error (a diagnostic — see the *Diagnostics* section of the [LiveHD docs](https://masc-ucsc.github.io/docs/livehd/02-usage/)). The header's `:error:` and `:help:` values are matched (Python `re.search`, with a literal-substring fallback when the value is not a valid regex — so `')'` works) against the emitted diagnostic's `message` and `hint`. A compile error exits non-zero cleanly (no abort) in every build mode. To pin the **line**, put a `locate_error_here` comment on the expected-error line — the harness checks the diagnostic's `start_line` matches it (a marker survives inserting/removing lines above, unlike a hard-coded number; the token `locate_error_here` is reserved — don't use it in prose, and only use it when the diagnostic actually carries a span — many `upass` errors don't yet). Diagnostics are read from the JSONL file declared via `lhd --emit diagnostics:PATH` (the hermetic kernel ignores the old `LIVEHD_DIAG` env). Each `tests/errors/*.prp` auto-generates a `prp-<name>` `bazel test` target. Example:
  ```
  /*
  :name: unbalance
  :type: error
  :error: ')'
  :help: unbalance
  */
  comb foo( -> (z) { z = b#[1,4] }   // locate_error_here  (missing ')')
  ```

## Debugging Yosys-to-LGraph Flow

### Running Yosys tests
- **Single test**: `./inou/yosys/tests/yosys_compile.sh ./inou/yosys/tests/<test>.v` (or `bazel test //inou/yosys:yosys_compile-<test>`)
- **Full suite**: `./inou/yosys/tests/yosys_compile.sh`
- The script drives `lhd compile`/`lhd check`; each test gets a fresh scratch
  `--workdir` (no shared `lgdb` state to clean — the kernel is stateless).
  Per-step logs (yosys chatter included) are under `tmp_yosys/<top>/logs/`.

### Inspecting intermediates
- **Yosys RTLIL dump**: After running tolg, check `pp.il` for what Yosys produced (cell types, port connections, parameters).
- **LGraph dump**: `./bazel-bin/lhd/lhd compile <file> --reader yosys-verilog --top <top> --recipe O1 --emit-dir verilog:out/ --workdir w` then read the per-step logs in `w/logs/` and grep the cgen output in `out/*.v` for cell types (e.g., `grep -i mem`). (The REPL-only `lgraph.dump` text dump has no lhd emit yet.)
- **Generated Verilog**: Check the `--emit-dir verilog:` per-module output; `tmp_yosys_mix/all_<top>.v` is the concatenated file used by LEC in `yosys_compile.sh`.

### Yosys memory pass
- `inou/yosys/inou_yosys_read.ys` controls the Yosys script. The `memory -nomap` pass collects `$memrd`/`$memwr`/`$meminit` into `$mem_v2` cells without decomposing into DFFs. Using plain `memory` decomposes small memories into DFFs+muxes, bypassing LGraph memory handling.

### Memory cell types
- Modern Yosys produces `$mem_v2` (not `$mem`). Match both: `cell->type == "$mem" || cell->type == "$mem_v2"`.
- Do NOT use `strncmp("$mem", 4)` — it catches `$memrd`/`$memwr`/`$meminit` which have different port structures.
- Memory RTL modules are in `ware/rtl/cgen_memory_*.v`. The `multiclock` variants have per-port clock inputs instead of a shared `clk`.
