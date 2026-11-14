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
- **Parallel regression**: Use Bazel's normal CPU/memory scheduling across all
  available CPUs. Keep jobs/test concurrency automatic (or use
  `--jobs=HOST_CPUS --local_test_jobs=0`); do not impose blanket one-test or
  one-synthesis caps on regressions. Preserve per-test resource accounting.
- **Required regression groups**: Frontend covers Pyrope/Verilog parsing and
  semantics, uPass, LGraph/IR, simulation, and pre-placement logic. Backend
  covers placement, physical data, timing, and routing. Synthesis belongs to
  **both**, including pre-placement synthesis and post-placement resynthesis.
  When changing or evaluating a group, add `--config=frontend` or
  `--config=backend` to the regression; use `--config=all` for synthesis or
  changes spanning both. This applies to evaluation-only work with no edits:
  select the config explicitly. About 10% of the most important tests from
  **each** group always run, including when the other group is selected.
  Plain `bazel test //...` runs both smoke sets. See `tools/regression.md`.
- **Test runtime**: Aim for at most 30 seconds per test (soft target, any
  build mode). It is machine-dependent, not a failure threshold. Bazel keeps
  its 300 second timeout (`.bazelrc`) only as a hang safety net; never raise it
  to hide a slow test. Periodically review slow tests and reduce their cost.
  Simplify oversized fixtures and avoid repeated
  compilation; keep large design benchmarks in their own repositories.
  Never skip, exclude, mark manual, or weaken a test because it is slow.
  Fix the implementation or test setup, or split independent checks while
  preserving coverage. Report measured runtimes and use them to guide cleanup.
- **Independent tests**: LiveHD scripts and BUILD rules must not access sibling
  benchmark repositories. Test inputs must be provided by this repository or
  declared build dependencies.
- **Simulator backend**: `lhd sim` defaults to `sim.tune.backend=llvm` (native
  color objects). A test that is about the Slop backend itself (its C++ output,
  slop-only knobs) pins `--set sim.tune.backend=slop`; a general sim test that
  fails under llvm is an LLVM-backend bug to fix in `inou/cgen`, not a reason
  to switch it to slop.
- **Sim flow (LNAST vs inou.cgen.sim)**: `lhd sim` builds the DUT through the
  LNAST flow by default (`pass.lg2lnast` -> `pass/lnast_llvm` native kernels, no
  color plan or cgen_sim; `pass/lg2lnast/README.md` milestone 3). What it does not
  cover (VCD/probe/query runs, `sim.tune.backend=slop`, a design `lg2lnast` or
  `lnast_llvm` declines, a testbench that reads a register/memory/sub-instance of
  the DUT) falls back to `inou.cgen.sim` with a `lnast-fallback` progress note.
  `LHD_SIM_LNAST=0` forces inou.cgen.sim, `=1` requires the LNAST flow (a decline
  is an error: the way to find out why a design is not covered). A test about
  inou.cgen.sim itself (the sim.tune driver, its generated C++) pins
  `LHD_SIM_LNAST=0`. Known semantic difference: with `sim.unknown_zero` off each
  `?` literal is drawn ONCE per run from the seed in both flows, but a `?` the
  LNAST flow meets outside an operand literal is 0. `sim.vcd` is supported (ports
  and registers, one scope per instance path, edge-aligned: no `sim.vcd_fake_delay`
  X windows, memories not traced); probe / break-when / query / list-signals /
  restart runs still fall back. `sim.warn_undefined` reports in both flows (the
  LNAST flow: division/remainder by zero, registered-memory accesses outside the
  array, and the Verilog reader's `lgundef` markers; not inside a rolled loop). When a flow disagrees with the
  other, do not assume the old one is right: compare against the language rules.
- **Sim build**: `lhd sim` generates `<workdir>/sim` and builds `drv.bin` itself
  (built-in parallel, incremental host compile); there is no bazel/ninja project
  and no `sim.ninja` option. The sim dir records its command, so `lhd sim` /
  `lhd sim --setup-only` / `lhd sim --run-only` run inside it re-read the
  original sources and reuse everything unchanged (`--setup-only` regenerates
  only; plain `lhd sim` regenerates, builds and runs; `--run-only` builds and
  runs the existing tree, `sim.compile_only=true` stops after the build).
- **Synth rebuild**: `lhd synth ... --workdir W` records its command and result in
  `W/synth/manifest.json` (the same single manifest.json the unit index lives in:
  entries such as `invocation` and `synth` are kept across rewrites). A bare
  `lhd synth` inside `W/synth` replays it: unchanged inputs mean no work and the
  stored qor is reported again (`--stats` expands it); any change re-runs the
  flow through the usual incremental tiers.
- **External simulators**: the default suite must not need iverilog, vvp
  or verilator. Optional independent event-simulator legs are skipped
  unless `LHD_EXTERNAL_SIM` is set:
  `bazel test --test_env=LHD_EXTERNAL_SIM=1 --test_env=PATH=<dir-with-tools>:$PATH <target>`.
  A requested leg whose tool is missing FAILS rather than skipping.
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
- `../DREAMPlace/` — Native placement and routability. Sources: `dreamplace/native/`.
- `../OpenTimer/` — Static timing engine. Sources: `ot/`.

Do not carry local dependency patches for repositories we own, including
DREAMPlace, OpenTimer, HHDS, HLOP, tree-sitter-pyrope and LiveHD. Make fixes in
the owning sibling repository and consume its published commit in
`MODULE.bazel`. During co-development, use the sibling checkout through a
local repository override; do not edit Bazel's fetched external copies.

**Rule:** if you need anything from one of these libraries (e.g. `grammar.js`, `tree.hpp`, `graph.hpp`, `attr.hpp`), open it directly at the path above. Do **not** start a `find / ...`, `find . ...`, or recursive `grep` to locate them — they are always at these fixed sibling paths. The bazel build fetches them as git-pinned plain repos (`use_repo_rule git_repository` in `MODULE.bazel`, NOT `bazel_dep` + `git_override`, so livehd stays self-contained when consumed as a bazel dependency); each has a commented `local_repository` swap for co-development against the sibling checkout. Running a broad `find` is wasteful and frequently the wrong tool; go straight to the known path.

## Key Directories

- `graph/`: LGraph IR (HHDS-backed) — LiveHD cell semantics + per-pin attributes + graph library over `hhds::Graph` (nodes, pins, edges). See `graph/README.md`. (This is the old `lgraph/`, now migrated onto HHDS.)
- `lnast/`: LNAST high-level IR (HHDS-backed `hhds::Tree` + flat-storage attributes)
- `parser/`: AST built on `hhds::Tree`
- `inou/yosys/`: Yosys integration (`lgyosys_tolg.cpp` = Yosys→LGraph, `inou_yosys_read.ys` = Yosys script)
- `inou/cgen/`: Verilog code generation from LGraph
- `pass/cprop/`: Constant propagation pass
- `pass/aegraph/`: `pass.aegraph`, the acyclic e-graph alternative to cprop, the DEFAULT constant-propagation engine, selected with `--set compile.cprop.engine=aegraph|classic|aegraph+classic` (default `aegraph`; `classic` is the reference engine); it runs rewrite rules through the rule runtime (`pass/aegraph/runtime/match.hpp`; knobs `compile.aegraph.*`, e.g. `rules_disable`, `max_rewrites`, `check`). Pattern-shaped rules are written in `pass/aegraph/rules/*.rules` (S-expression DSL) and generated by `rulegen` via `aeg_rules()` in `tools/aegraph.bzl`; hand-written ones are `AEG_NATIVE_RULE` in `rules/*.cpp`. Adding a rule is ONE `.rules` entry plus `bazel run //pass/aegraph:aeg_rules_refresh` (rewrites the generated `pass/aegraph/RULES.md`); never edit `RULES.md` or an enum/knob list by hand. Rules never encode a target preference: extraction picks among `alt` results by the cost profile (`compile.aegraph.cost=synth|sim|auto`, default `auto`: `sim` under `lhd sim`, `synth` for every other command incl. a bare `lhd compile`; an explicit value overrides; `pass/aegraph/cost/{synth,sim}.cost`, width- and sharing-aware, linted against every `simplify` at build time; `:expect (synth lhs) (sim rhs)` on a rule asserts the choice). A/B numbers and the rule census with `scripts/cprop_engine_sweep.py`; see `pass/aegraph/README.md`
- `pass/synth/`: the shared, ABC-free synthesis pipeline (private copy, ware/memory modules, region driver, `Lnet` translation, region cache, read-back); see `pass/synth/README.md`
- `pass/satopt/`: `pass.satopt`, bounded proof-backed simplification with selectable stages, run only by the compile step (`--set pass.satopt=true`; off by default for every command, including `lhd synth`/`lhd lec`) and `lhd pass satopt`; see `pass/satopt/README.md`
- `pass/abc/`: the ABC backend and `pass.abc` — the only synthesis code that includes or calls ABC
- `pass/usyn/`: native register-rooted XAG synthesis (`pass.usyn`, `synth.mapper=usyn`), including logical region reuse; independent of ABC except optional technology mapping through the ABC provider
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
lhd compile foo.v --top foo --emit verilog:out.v
lhd compile foo.prp --emit-dir lg:foo_lgs/ --emit-dir lnast-dump:dumps/
lhd lec --impl verilog:out.v --ref verilog:foo.v --top foo
lhd synth foo.prp --top foo --workdir W --stats   # compile -> color synth -> abc -> opentimer, one shot
```
Graph compilation always runs constant propagation followed by bitwidth
inference. There is no optimization-level or `--recipe` selection. Comb
inlining defaults off (`compile.upass.inline=false`): comb calls stay module
instances, and upass splices one only when it must (tuple-shaped results the
instance boundary cannot carry, generic/untyped callees); `pass.legalize`
handles combinational loops through an instance.

`lhd synth` is the fused synthesis flow (reports in `W/synth/`; `--emit-dir
lg:` / `verilog:` for the mapped netlist, `report:` for the sidecars); the
individual `lhd pass color|abc|opentimer` steps remain for any other
coloring or for inspecting intermediates. Every persistent reuse tier (the
compile cache, `abc_cache/`, `usyn_cache/` including its optional `tmap/` stage,
`sta_cache/`, the formal verdict cache) lives under a
user-named `--workdir` and follows the ONE switch `--set
lhd.incremental=true|false` (default true) — there is no per-tier cache flag.
Internally lhd drives the registered EPRP methods (conceptually the pipe
`inou.yosys.tolg |> pass.cprop |> inou.cgen.verilog`); pass/inou names in
`--set` and the step logs use that vocabulary.

**Measuring reuse after a rebuild — read this before believing a cache miss.**
Each tier is salted by a build-time content hash of the code that produces what
it stores, so **the first run after you touch that code is a full miss, by
design**: the synthesis salts are layered -- `//pass/synth:synth_salt` hashes
`pass/synth` + the passes a mapped region depends on (graph, memory RTL,
cprop, enableopt, bitwidth, color, the DFF pick, formal, `pass/partition`),
`//pass/abc:abc_salt` hashes `pass/abc` + `synth_salt` + `MODULE.bazel` + the
ABC patch, and `//pass/usyn:usyn_salt` hashes `pass/usyn` + `synth_salt` (no ABC dependency) --
`//pass/opentimer:sta_salt` hashes `pass/opentimer` + `pass/partition` +
`MODULE.bazel`, `//lhd:formal_salt` hashes the prover (`pass/lec`, `pass/semdiff`,
the cvc5 pin) + the proof preparation whose output the verdict cache digests
(`pass/single_edge`, graph, cprop, `lhd/lhd_kernel_formal.cpp`), and
`//lhd:compile_salt` likewise. Even a `clang-format -i` counts. Always run the warm command **twice**
after a rebuild and read the second number, and never rebuild in the middle of a
measurement sweep — a cold pass stored under salt A and a warm pass loaded under
salt B looks exactly like "the cache forgot everything".

## Environment variables (`getenv`)

- LiveHD-owned environment controls are for developer debugging, tracing,
  profiling, or test fault injection only. Use `LHD_<UPPER_SNAKE_CASE>` names;
  update callers, scripts, tests, and documentation together when renaming one.
- User-facing behavior belongs in typed CLI/config options (`--set`), not new
  environment switches. Preserve existing defaults.
- Never call `getenv` in a node/pin traversal, solver loop, emission loop, or
  other hot path. Use `livehd::debug::env<"LHD_NAME">()` from
  `core/debug_env.hpp`: it owns and caches each value once per process,
  including an unset value. Debug controls remain available in optimized
  builds. Set them before starting the process; later environment mutations
  do not update a cached control.
- A debug/test switch intentionally varied between invocations in one process
  may instead be read once at that invocation's setup boundary. Carry the
  snapshot through the operation (and its cache key when it changes output).
  Do not re-read it while processing the design.
- External process interfaces (`PATH`, `CXX`, `SOURCE_DATE_EPOCH`, Bazel
  `RUNFILES_DIR`/`TEST_*`, etc.) retain their standard names and may be read at
  setup boundaries. Existing tool/technology/source-path discovery and the
  host memory safety cap are bootstrap exceptions; keep their LiveHD-owned names
  under `LHD_*`. Do not extend these exceptions to add runtime feature knobs.

## Compiler Warnings Policy

After bitwidth inference, a pass that creates or replaces a value-producing
cell must install its driver width and sign, or run bitwidth on the affected
graph before handing it to a consumer. This includes post-convergence
rewrites inside bitwidth itself. Constants carry their type in their literal;
unsigned drivers use the absence of `pin_signed`. Unresolved driver widths
at codegen, synthesis, simulation, or LEC boundaries are internal errors in
every build mode, not warnings or permission to guess a width.

Always fix source code — never add `-Wno-*` flags to BUILD files. Exception: external deps in `MODULE.bazel`.

## Contracts

### Acknowledgements

When an agent or human consults a paper or tool and uses it to implement any
LiveHD feature, update `docs/acknowledgement.html` with the appropriate credit
as part of that implementation. Do not defer the update to a future scan or
attribution review.

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

### Logging policy (`core/diag.hpp`)

Default stderr is errors, warnings and very short outcome lines, nothing else.
Pick the level when you add a record:

- `diag::outcome(pass, code, cat)`: always shown, one short line (a fallback taken, a file written).
- `diag::stat(pass, code, cat)`: a larger summary, shown with `--stats` (or a matching `-d`).
- `diag::info(pass, code, cat)`: detail, shown ONLY when `-d WORD` matches `pass`.

`-d WORD[,WORD]` (repeatable) names categories: the dotted `pass` string of the record
(`pass.abc`, `synth.place`, `pass.semdiff`, `lhd.sim`). A word matches whole dotted segments
(`abc` matches `pass.abc`, `synth.place` matches `synth.place.legalize`); `all` matches
everything. A disabled builder skips `std::format` and every append, but C++ still evaluates the
arguments, so wrap anything with a loop or a `to_string` in `if (diag::enabled("pass.x"))`.
A test that greps a log record must pass the `-d` word (or `--stats`) that enables it. The
`PROGRESS pass.abc` heartbeat reaches stderr only under `-d pass.abc`; the step log always has it.

### Defaults are a contract

Every user-visible default (a pass/`--set` option default, a compile recipe
default, a CLI flag default) is part of the contract. Do **not** change a
default unless the user **explicitly approves that specific change** in the
conversation. Never auto-approve, and never infer approval from a related
request, a test that "expects" another value, or a performance win. If a fix
seems to need a different default, keep the default, add an opt-in option,
and ask.

Example: `compile.upass.inline` defaults to `false` (comb calls stay module
instances, so hierarchy, LEC per-def checks and debug keep their boundaries).
It was once flipped to `true` without approval; that is exactly what this rule
forbids. (`pass.legalize` inlining a state-free instance on a combinational
false loop is a structural repair, not a default, and stays.)

## Running Pyrope Tests

- **Single test (harness)**: `python3 inou/prp/tests/pyrope_test.py -i inou/prp/tests/<dir>/<test>.prp`
- **Direct pipeline (comptime)**: `./bazel-bin/lhd/lhd compile <test>.prp --set upass.verifier=true --set upass.verifier_pass=1 --set upass.verifier_fail=0 --emit-dir lnast-dump:dumps/ --workdir w` (the post-upass LNAST text lands in `dumps/*.lnast`; uPass stdout diagnostics in `w/logs/*pass_upass*.log`)
- Test header `:type:` selects the pipeline (`parsing`, `lnast`, `upass`, `comptime`, `lgraph`, `compile`); `:verifier_pass:` / `:verifier_fail:` set expected cassert tallies for `comptime`.
- **Expected-failure tests** (`:type: error`, in `inou/prp/tests/errors/`): the program **must** emit a compile error (a diagnostic — see the *Diagnostics* section of the [LiveHD docs](https://masc-ucsc.github.io/docs/livehd/02-usage/)). The header's `:error:` and `:help:` values are matched (Python `re.search`, with a literal-substring fallback when the value is not a valid regex — so `')'` works) against the emitted diagnostic's `message` and `hint`. A compile error exits non-zero cleanly (no abort) in every build mode. To pin the **line**, put a `locate_error_here` comment on the expected-error line — the harness checks the diagnostic's `start_line` matches it (a marker survives inserting/removing lines above, unlike a hard-coded number; the token `locate_error_here` is reserved — don't use it in prose, and only use it when the diagnostic actually carries a span — many `upass` errors don't yet). Diagnostics are read from the JSONL file declared via `lhd --emit diagnostics:PATH` (the hermetic kernel ignores the old `LHD_DIAG` env). Each `tests/errors/*.prp` auto-generates a `prp-<name>` `bazel test` target, except the fixtures listed in `_ERR_MERGED` in `inou/prp/BUILD`, which run together inside one `prp-errors-<group>` target (the harness takes a repeated `-i`, each fixture prints its own result line, and a failing one is named in the log); run any one of them by hand with the single-test command above. Example:
  ```
  /*
  :name: unbalance
  :type: error
  :error: ')'
  :help: unbalance
  */
  comb foo( -> (z) { z = b#[1,4] }   // locate_error_here  (missing ')')
  ```
- **Equivalence tests** (`inou/prp/tests/equiv/`): two axes, and a design can sit on both. `foo.prp` + a hand-written `foo.v` is the Pyrope↔Verilog pair (`:type: equiv`, target `prp-equiv-foo`). `foo.prp` + `foo_1.prp`, `foo_2.prp`, … is a Pyrope↔**Pyrope** group: every numbered sibling is LEC'd against the base by `inou/prp/tests/prplec.py` (target `prp-lec-foo_1`, or one `prp-lec-foo` running every variant when the multi-variant base is listed in `_PRPLEC_GROUPED` in `inou/prp/BUILD`), wired by the file NAME alone — a new claim is one new file, no script and no BUILD edit. A HEADER-ONLY variant means "the base source with MY `:set:` flags" (that is how rolled-vs-unrolled is written without duplicating the design). Per-variant header tags `:lec_expect: proven|refuted`, `:lec_sweep:`, `:lec_grep:` / `:lec_grep_not:`; run one by hand with `./inou/prp/tests/prplec.py inou/prp/tests/equiv/foo.prp`. See `inou/prp/tests/equiv/README.md`.
- **Expected-warning tests** (`:type: warning`, in `inou/prp/tests/warnings/`): the lint counterpart of `tests/errors/`. The program **must compile cleanly** (exit 0, no `error` diagnostic) yet emit at least one **warning** diagnostic. The header's `:warning:` and `:help:` values are matched (same `re.search` + literal fallback) against the warning's `message` / `hint`; a `locate_warning_here` comment pins the warning's `start_line`. Each `tests/warnings/*.prp` auto-generates a `prp-warn-<name>` `bazel test` target. See `inou/prp/tests/warnings/README.md`. Example: a pure expression used as a statement (`a + 1`) triggers `unused-expression`.

## Debugging Yosys-to-LGraph Flow

### Running Yosys tests
- **Paired HDL/Pyrope corpus**: `bazel test --config=frontend //inou/prp:all`.
  General reader/round-trip checks share `inou/prp/tests/equiv`; the old
  `yosys_compile.sh` sweep is removed.
- **Importer and solver checks**: `bazel test --config=frontend //inou/yosys:all`. The dedicated
  `yosys_import-*` cases share `inou/prp/tests/yvr_test.py`; they require both
  native LEC and an independent Yosys proof. Parser-only fixtures live under
  `inou/slang/tests/reader` and retain their Slang ladder checks.
- **Routine versus full groups**: `bazel test //...` runs the unconditional
  frontend and backend smoke sets. `--config=frontend` selects the complete
  frontend plus both smoke sets; `--config=backend` does the same for backend.
  `--config=all` runs both complete groups. Focused non-smoke targets also need
  their group's config. `python3 scripts/regression.py -- -c opt` selects
  groups from local changes; `--base REV --head HEAD` selects committed changes
  in CI. Explicit `--frontend`, `--backend`, or `--all` supports evaluation-only
  work. Missing history selects both groups; see `tools/regression.md`.

### Inspecting intermediates
- **Yosys RTLIL dump**: After running tolg, check `pp.il` for what Yosys produced (cell types, port connections, parameters).
- **LGraph dump**: `./bazel-bin/lhd/lhd compile <file> --reader yosys-verilog --top <top> --emit-dir verilog:out/ --workdir w` then read the per-step logs in `w/logs/` and grep the cgen output in `out/*.v` for cell types (e.g., `grep -i mem`). (The REPL-only `lgraph.dump` text dump has no lhd emit yet.)
- **Generated Verilog**: Check the `--emit-dir verilog:` per-module output; `tmp_yvr_<fixture>/all_impl.v` is the concatenated file used by `yvr_test.py`.

### Yosys memory pass
- `inou/yosys/inou_yosys_read.ys` controls the Yosys script. The `memory -nomap` pass collects `$memrd`/`$memwr`/`$meminit` into `$mem_v2` cells without decomposing into DFFs. Using plain `memory` decomposes small memories into DFFs+muxes, bypassing LGraph memory handling.

### Memory cell types
- Modern Yosys produces `$mem_v2` (not `$mem`). Match both: `cell->type == "$mem" || cell->type == "$mem_v2"`.
- Do NOT use `strncmp("$mem", 4)` — it catches `$memrd`/`$memwr`/`$meminit` which have different port structures.
- Memory RTL modules are in `ware/rtl/cgen_memory_*.v`. The `multiclock` variants have per-port clock inputs instead of a shared `clk`.

### Never modify a driver while it is executing

Bash reads a script incrementally, by byte offset. Editing one that is running
resumes parsing at a stale offset, so the interpreter lands mid-token in the
NEW text and dies on something that was never a syntax error in either version:

    scripts/<driver>.sh: line 341: syntax error near unexpected token `('

Measured on a census driver: it was killed that way after `phase 1 done`, and
four modules' generation was discarded.

Atomic replace does not save you. The session scratchpad under `/tmp` is a
DIFFERENT filesystem from the NFS working tree, so `mv` is a copy, not a
rename -- it falls back to `open(dest, O_TRUNC)` on the same inode, which is
exactly the in-place rewrite the running shell is reading.

A run-directory lock does not save you either: it protects the OUTPUT, not the
driver text.

So: wait for it, or use the driver's own `--resume`. If it truly must stop,
kill it by exact pid (never `pkill -f`, which matches your own command line)
and relaunch into a fresh run directory.
