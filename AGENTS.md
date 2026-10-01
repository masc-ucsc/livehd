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
  (`lhd help`, `lhd describe <cmd>`); `lhd pyrope lsp` serves the Pyrope LSP
  and `lhd pyrope fmt` formats Pyrope source. The old `lgshell` REPL was
  **removed** (2026-06-04)
- C++ formatted with `clang-format`
- **Contract tests/benchmarks**: Any test or benchmark file whose name contains the word `contract` is immutable to the coding agent. Do NOT modify these files — they define the expected behavior contract. If a contract test fails, fix the implementation, not the test.
- **Test file naming**: use the `_test.cpp` suffix only when a matching `foo.cpp` exists in the same or parent directory (`foo_test.cpp` tests `foo.cpp`); it is a suffix, never a prefix. Standalone checks with no single source counterpart must not carry `_test` (use e.g. `*_smoke.cpp`).

## Sibling Repositories (DO NOT search the filesystem for these)

LiveHD depends on several sibling repos. **Always look in these exact paths — do NOT run `find`, `fd`, `locate`, or any other filesystem search to find them or files inside them:**

- `../hhds/` — HHDS library (Graph, Tree, Forest, flat_storage attributes). Headers: `hhds/graph.hpp`, `hhds/tree.hpp`, `hhds/attr.hpp`.
- `../hlop/` — HLOP library (dlop / slop helpers).
- `../iassert/` — iassert library (`I(...)`, `GI(...)` invariant macros).
- `../tree-sitter-pyrope/` — Tree-sitter grammar for Pyrope. The grammar lives at `../tree-sitter-pyrope/grammar.js`; generated parser sources are under `../tree-sitter-pyrope/src/`.

**Rule:** if you need anything from one of these libraries (e.g. `grammar.js`, `tree.hpp`, `graph.hpp`, `attr.hpp`), open it directly at the path above. Do **not** start a `find / ...`, `find . ...`, or recursive `grep` to locate them — they are always at these fixed sibling paths. The bazel build fetches them as git-pinned plain repos (`use_repo_rule git_repository` in `MODULE.bazel`, NOT `bazel_dep` + `git_override`, so livehd stays self-contained when consumed as a bazel dependency); each has a commented `local_repository` swap for co-development against the sibling checkout. Running a broad `find` is wasteful and frequently the wrong tool; go straight to the known path.

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
lhd lec --impl verilog:out.v --ref verilog:foo.v --top foo --set lec.solver=lgyosys
```
Internally lhd drives the registered EPRP methods (conceptually the pipe
`inou.yosys.tolg |> pass.cprop |> inou.cgen.verilog`); pass/inou names in
`--set` and the step logs use that vocabulary.

## Compiler Warnings Policy

Always fix source code — never add `-Wno-*` flags to BUILD files. Exception: external deps in `MODULE.bazel`.

## Contracts

### Git branches

Do **not** create a git branch unless the user explicitly asks for one. Work
on the current branch by default; only run `git checkout -b` / `git switch -c`
(or otherwise create a branch) when the user clearly indicates to do so.

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
- **Expected-warning tests** (`:type: warning`, in `inou/prp/tests/warnings/`): the lint counterpart of `tests/errors/`. The program **must compile cleanly** (exit 0, no `error` diagnostic) yet emit at least one **warning** diagnostic. The header's `:warning:` and `:help:` values are matched (same `re.search` + literal fallback) against the warning's `message` / `hint`; a `locate_warning_here` comment pins the warning's `start_line`. Each `tests/warnings/*.prp` auto-generates a `prp-warn-<name>` `bazel test` target. See `inou/prp/tests/warnings/README.md`. Example: a pure expression used as a statement (`a + 1`) triggers `unused-expression`.

## Debugging Yosys-to-LGraph Flow

### Running Yosys tests
- **Single test**: `./inou/yosys/tests/yosys_compile.sh ./inou/yosys/tests/<test>.v` (or `bazel test //inou/yosys:yosys_compile-<test>`)
- **Full suite**: `./inou/yosys/tests/yosys_compile.sh`
- The script drives `lhd compile`/`lhd lec`; each test gets a fresh scratch
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

## Never modify a driver while it is executing

A long-running shell or Python driver (`scripts/coreet_d2_census.sh`,
`scripts/run_coreet_module_lean.sh`, `pass/lean/scripts/direct_sweep.py`, the
sweep harnesses) must not be edited, committed over, or replaced until it has
exited.

**Why no replacement trick saves you.** bash reads a script incrementally, *by
byte offset*, as it executes. A driver that is halfway through has not yet read
its later phases, so changing the bytes under it makes it resume parsing at a
stale offset in new content:

    scripts/coreet_d2_census.sh: line 341: syntax error near unexpected token `('
    `echo "phase 1: generating ${#MODULES[@]} module(s), jobs=$JOBS"'

That killed a census driver immediately after `phase 1 done`, discarding the
aggregation and sweep of four modules that had already generated.

"Atomic replace" does not rescue this in general, and the common way of
attempting it is itself broken here: the session scratchpad under `/tmp` is a
**different filesystem** from `/mada` and `/soe`, so `mv /tmp/new scripts/x.sh`
is not a rename — GNU `mv` falls back to `open(dest, O_TRUNC)` and rewrites the
*same inode* in place. Even a true same-filesystem rename only helps a process
that has already read the whole file, which a mid-run driver has not.

**The locking does not cover this.** `<out>/.lock` (and the stable lock under
`generated/census_d2/runtime_locks/`) protects a run *directory* from a second
writer. It says nothing about the executable text of the driver itself.

**What to do instead:** wait for the run to exit, or let it finish and use
`--resume` (which is fail-closed and exists precisely because interrupted
generation should not be regenerated). If an edit is genuinely urgent, stop the
run first — by exact pid and process group, never by pattern — and relaunch
into a *new* run directory.
