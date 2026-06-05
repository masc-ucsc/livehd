# LiveHD Future CLI Design

## Goals

LiveHD's CLI serves two consumers from one engine:

1. **Build systems** (Bazel, Make, Ninja) that call LiveHD as a hermetic
   compilation/elaboration action — declared inputs in, declared outputs out,
   deterministic, no ambient state.
2. **LLM coding agents** that drive LiveHD interactively — create a workspace,
   run steps incrementally, inspect intermediate state, re-run with tweaks,
   compare experiments.

These pull in opposite directions: a build action wants *no* persistent state,
while an agent wants persistent, inspectable state. The design reconciles them
with one rule: **the stateless invocation is the kernel; the `@tag` workspace
is sugar on top of it.** Both share the step vocabulary, the `--set key=value`
override syntax, the JSON result schema, the `error.class` taxonomy, and the
`list`/`describe` self-documentation. They differ only in *where state lives*
(argv vs. tag directory) and in *determinism defaults* (hermetic vs.
convenience).

### Two layers, build-first

Implementation order: ship the stateless kernel first (it is the subset both
consumers share), then layer the tag workspace on top.

- **1y-bazel** — the stateless, hermetic kernel: direct invocation with
  explicit inputs/outputs, deterministic results, depfile emission, clean
  stdout. Callable from a Bazel rule or a Makefile. Specified in
  *Stateless build-system mode* and *Dependency files* below.
- **2y-agent** (renamed from 1y-agent; demoted to Group 2 — the landed
  kernel covers every test/build flow) — the stateful workspace: `@tag`
  directories, `setup/run/status/list/describe`, JSONL run logs, multi-tag
  experiments, plus the deferred kernel follow-ups (multi-`lg:`
  linker, per-function units, graphviz/lgraph-dump emits, import-resolver
  depfile, liberty/opentimer verb — see TODO_livehd 2y-agent; the toln
  gate + its emit-derivation landed 2026-06-05). Each
  `run <step> @tag` desugars to a kernel call. Specified in *Tag workspace
  mode* and everything after it.

Principles (shared by both layers):

- One command = one step (no implicit multi-step pipelines)
- Structured JSON/JSONL output on a clean protocol stream — no banners, no
  human chatter, no raw pass logs intermixed on stdout
- `--set key=value` config overrides; TOML config where state persists
- Self-documenting API (`list`, `describe`) for capability discovery and
  tag-scoped introspection
- Raw logs separate from structured results
- Stable schema versions for every machine-readable object

## Stateless build-system mode (1y-bazel)

This is the kernel: the `lhd` binary, invoked as a pure function
`(declared inputs, config) → (declared outputs, exit code)`. It is what a Bazel
rule or a Makefile recipe calls — never a REPL. It reads and writes
no ambient state: no `@tag`, no `~/.cache`, no history file, no lock, no
`latest` symlink.

### Why a build system rejects the tag layer

A Bazel action must be hermetic, deterministic, and parallel-safe. The
agent-oriented features documented later are exactly what it cannot tolerate,
so the kernel omits them:

| Tag-layer feature         | Why a build action rejects it                       |
|---------------------------|-----------------------------------------------------|
| `run_id` = timestamp+rand | Non-deterministic → defeats the action/remote cache |
| `~/.cache` tag dir        | Not hermetic — reads state outside declared inputs  |
| `latest ->` symlink       | Bazel forbids mutable/dangling symlinks in outputs  |
| per-tag `lock` file       | Fights Bazel's sandboxed parallelism                |
| `odir:` tool-named files  | Bazel must predeclare every output path             |

### Two invariants (not flags)

The kernel is **always deterministic** and **always hermetic** — properties of
the binary, not opt-in switches:

- **Deterministic.** Output bytes are a pure function of inputs. `run_id` is a
  content hash of (tool version + input bytes + resolved config), never
  wall-clock+random. Iteration order is stable (module emit order, JSON key
  order); no PID, hostname, or absolute path leaks into an artifact. If a
  timestamp must be embedded it comes from `SOURCE_DATE_EPOCH` — the cross-tool
  [reproducible-builds](https://reproducible-builds.org/specs/source-date-epoch/)
  convention: one Unix-epoch env var that replaces the wall clock so two builds
  of the same inputs are byte-identical — and is omitted/zero when that is
  unset. (The agent layer keeps a timestamp+random `run_id` so concurrent
  run-dirs never collide; the kernel does not.)
- **Hermetic.** A source file the frontend cannot find within its declared
  inputs is a `missing_file` error, never a silent reach into the filesystem.

### Commands

Ten commands. The split rule: **commands that ingest source are
language-qualified; commands that operate on IR are language-agnostic.**

| Command | Stage | Specific arguments |
|---|---|---|
| `lhd elaborate verilog` | frontend (whole-design, one action) | `--reader yosys-verilog\|yosys-slang\|slang` (default `yosys-slang`; `yosys-*` go through yosys into LGraphs, `slang` is the direct `inou.slang` SV→LNAST front-end); `--top <name>` (optional); `--depfile PATH`; `-- <raw slang args>` (yosys readers only) |
| `lhd elaborate pyrope` | frontend (per-function LNAST units) | positional `*.prp` (filelist; `import` resolves within it); `--top <name>` (optional) |
| `lhd synth` | transform / optimize / codegen | `--in design:PATH` (repeat) or positional units; `--top <name>` (optional → cross-unit/LTO); `--recipe <name>`; `--recipe-file <path>`; `--set <pass[.idx].flag>=<val>` (repeat) |
| `lhd check` | equivalence (LEC) | `--impl KIND:PATH [--impl-top <name>]`; `--ref KIND:PATH [--ref-top <name>]` |
| `lhd compile verilog` | fused elaborate+synth | = `elaborate verilog` args ＋ `synth` args |
| `lhd compile pyrope` | fused elaborate+synth | = `elaborate pyrope` args ＋ `synth` args |
| `lhd list` | discovery | positional pattern: `steps`\|`recipes`\|`emit-kinds`\|`error-classes` |
| `lhd describe` | discovery | positional name: command / recipe / emit-kind |
| `lhd version` | meta | — |
| `lhd help` | meta | optional positional: a command name |

Shared arguments (the I/O contract; honored by every execution command):

| Argument | Applies to | Meaning |
|---|---|---|
| `--emit KIND:PATH` (repeat) | elaborate · synth · compile | declared typed output (the KIND fixes the format) |
| `--emit-dir KIND:DIR/` (repeat) | elaborate · synth · compile | variable-cardinality output → a TreeArtifact dir + `manifest.json` |
| `--in KIND:PATH` (repeat) | synth · check | declared typed IR input (source commands use their filelist) |
| `--in-dir KIND:DIR/` (repeat) | synth · check | all units in a directory, indexed by its `manifest.json` (counterpart of `--emit-dir`) |
| `--result-json PATH` | all execution | the structured result object (JSON) → a declared file (else stdout) |
| `--workdir DIR` | all execution | scratch + ephemeral lgdb; never the global cache |
| `-j`/`--jobs N` | elaborate · synth · compile | intra-action parallelism |
| `-q`/`--quiet`, `--verbose` | all execution | stderr verbosity; never pollutes the stdout protocol |
| `-h`/`--help`, `--version` | global | help / version on any command |

`--depfile` is **not** shared: it lives only on the Verilog frontend, because
only Verilog `` `include `` + `+incdir` reads files off the command line (see
*Depfiles*). Pyrope `import` is a module import resolved against the explicit
filelist — no hidden inputs. `synth`/`check` consume self-contained blobs.

### I/O model — one interchange format, two granularities

There is **one interchange format**: a serialized container of partitions — the
file form of today's `lgdb`, written as `--emit design:foo.lhd`. How many
partitions go in one file is a build-time choice, not a format change:

- **Blob — the default for Verilog and whole-design synth stages.** One
  `design` file per stage; N partitions live inside it. Every action is
  one-file-in, one-file-out → trivially declarable and cacheable. This is the
  model OpenROAD/ORFS uses (it passes the whole design as one serialized
  `.odb` between stages), and it fits Verilog elaboration, which *must* see
  all files at once and emits a single blob. In Bazel a blob is a normal file
  — or, since `lgdb` is a directory today, a single `declare_directory`
  reusing the existing save/load.
- **Exploded — the default for Pyrope.** A `.prp` file defines an unknown
  number of functions, and each function elaborates to its own LNAST — so
  `elaborate pyrope` emits one unit file per function (named by the function)
  via `--emit-dir lnast:DIR/`. The directory is a Bazel TreeArtifact; a
  `manifest.json` inside enumerates each entry (kind, logical name,
  `interface_hash`/`state_shape_hash` once [3c] lands, source map once [1f]
  lands, content hash). Downstream commands take the directory back with the
  symmetric `--in-dir KIND:DIR/` (the manifest is the index); an LTO `synth`
  reads it to pick the units it co-optimizes. Per-function units are also the
  caching win: an unchanged function cache-hits downstream even when a sibling
  in the same file changed.

Multiple typed inputs/outputs of different kinds is just repeating the typed
slots: `--in design:a.lhd --in design:b.lhd … --emit verilog:net.v --emit
metadata:net.meta.json --emit results:r.json`. Variable *cardinality* is the
only thing that needs `--emit-dir`+manifest; the blob default avoids it for
whole-design stages.

KIND vocabulary — inputs: `design`, `lnast`, `verilog`, `pyrope`; outputs:
those plus `graphviz`, `metadata`, `results`, `diagnostics`. An unknown kind is
a `usage` error. `check`'s `--impl`/`--ref` accept any input kind (a `design`
blob or a `verilog` file).

#### Worked example — unknown output cardinality (the Pyrope default)

Two Pyrope files that together define four functions; the caller cannot know
the count or names ahead of time (how many functions a `.prp` file holds is a
property of its contents). A fixed-path `--emit lnast:PATH` cannot express
that, so `elaborate pyrope` defaults to the exploded form — one LNAST file per
function, plus the index:

```bash
lhd elaborate pyrope file1.prp file2.prp --emit-dir lnast:out/ \
    --workdir tmp --result-json r.json
#   out/{fn_a.ln, fn_b.ln, fn_c.ln, fn_d.ln, manifest.json}

# downstream takes the directory back via the symmetric input slot
lhd synth --in-dir lnast:out/ --top fn_a --recipe O1 --emit verilog:net.v

# blob alternative: one file out, the four LNASTs inside it
lhd elaborate pyrope file1.prp file2.prp --emit design:out.lhd
```

The produced names are discovered *after* the run: `manifest.json` enumerates
them and the result's `outputs` array repeats them. In Bazel the exploded form
is one `declare_directory` TreeArtifact (never four files the rule cannot
name); the blob is one declared file. When one *known* function is wanted, a
fixed path works again because `--top` makes the cardinality 1:

```bash
lhd elaborate pyrope file1.prp file2.prp --top fn_c --emit lnast:fn_c.ln
```

Both `.prp` files go in one invocation: `import` resolves against the explicit
filelist, so files related by import must be elaborated together — the
parallel fan-out applies only to files with no import relationship.

### Recipes — the transform pipeline, defined as data

A recipe is the ordered pass chain (with per-pass flags) between the frontend
and the terminal `--emit`. It is named — not a `|>` string, not C++ control
flow:

- `--recipe NAME` — a built-in recipe (ships with the binary, versioned with
  it: e.g. `O0`/`O1`/`O2`, `roundtrip`). Stable public API, decoupled from
  internal pass names (which churn). Introspect via `lhd describe recipe:O2`
  (prints the expanded pass list) and `lhd list recipes`.
- `--recipe-file PATH` — a custom recipe as a small TOML file. Under Bazel this
  is a *declared input*, so a custom pipeline stays hermetic and reproducible:

  ```toml
  [recipe.O2]
  description = "aggressive: double cprop with bitwidth refinement"
  passes = [
    { pass = "upass" },
    { pass = "cprop" },
    { pass = "cprop", args = { flag = 3 } },
  ]
  ```
- `--set pass[.idx].flag=value` (repeat) — override one knob without forking the
  recipe (gcc's `-O2 -fno-inline`); a repeated pass is addressed by index
  (`cprop.1.flag=5`).

`--recipe`/`--set` apply only to commands that run passes (`synth`, `compile`);
`elaborate` has a fixed frontend. The result records the **expanded** recipe
(the passes+flags that actually ran), so an artifact is self-describing even if
`O2` is later redefined.

### Depfiles (Verilog frontend only)

Verilog `` `include "x.vh" `` resolved through `+incdir` reads files that are
**not** on the command line. The blob bakes them in and is self-contained for
downstream `synth`, but the *build system* still needs to know `x.vh` was read —
to rebuild `elaborate` when `x.vh` changes (incrementality) and to declare it in
a sandbox (hermeticity). This is the gcc split exactly: the `.o`/blob is
self-contained for the linker/synth, but you still need the `.d`/depfile so the
build planner sees the frontend's hidden header reads.

`--depfile PATH` writes a **Make-syntax** file — the primary `--emit` output(s)
as target(s), every file the frontend actually opened as prerequisites:

```make
foo.gen.v: foo.v bar.v rtl/inc/defs.vh
```

Consumed by Make (`-include foo.d`), Ninja (`depfile =`), or a Bazel `cc_*`-style
rule. Content is deterministic (sorted, workdir/exec-root-relative). Pyrope
needs no depfile (`import` is filelist-resolved); neither do `synth`/`check`
(self-contained blob inputs).

### Exit-code contract

A build action's success is *only* the process exit code:

- `0` — pass.
- non-zero — fail; the `error.class` in the result / `--result-json`
  disambiguates (`usage`, `syntax`, `missing_file`, `equiv_fail`, `internal`,
  `unsupported`, …).

No partial success: a fused `compile` exits 0 only if every constituent step is
`pass`.

### Clean stdout / no REPL chatter

The `lhd` binary emits nothing on stdout except the selected protocol (or
nothing at all when `--result-json` is given). REPL-style chatter
(welcome banner, echoed input line, `command aborted…`) does not exist on this
path. Diagnostics go to the `diagnostics` emit, the `--result-json` error
block, and/or stderr — never intermixed on stdout.

### Bazel integration

Ship a thin Starlark ruleset (`//tools:lhd.bzl`). Blob targets chain as plain
declared-file deps (exactly how HighTide/ORFS chains `.odb` between OpenROAD
stages); exploded targets carry a provider (`LhdDesignInfo { units: depset,
manifest, top, hashes }`) over TreeArtifact directories.

```python
load("//tools:lhd.bzl", "lhd_verilog")

lhd_verilog(
    name    = "foo_net",
    top     = "foo",
    srcs    = ["foo.v", "bar.v"],
    incdirs = ["rtl/inc"],
    recipe  = "O2",
    out     = "foo.gen.v",     # also writes foo.d (depfile) + foo.result.json
)
```

The macro sets `--workdir`, `--depfile`, `--recipe`, and `--emit` so callers
don't reconstruct the contract by hand. A `--print-inputs` / `--dry-run` mode
lets a Starlark aspect discover the input set when sources are not all listed.

### What this reuses (already in tree)

The kernel is new argv plumbing over existing machinery, not new compiler
internals:

- **Frontends** — `inou/yosys` tolg (`--reader yosys-verilog` native,
  `--reader yosys-slang` via the slang.so plugin — both to LGraphs),
  `inou/slang` (`--reader slang`, direct SV→LNAST), `inou/prp` prp2lnast
  (`elaborate pyrope`).
- **Passes / recipes** — `pass.upass`, `pass.cprop`, `upass/*`.
- **Codegen** — `inou.cgen.verilog`, `pass.prp_writer` (the `--emit` kinds).
- **Equivalence** — `inou/yosys/lgcheck` (`lhd check`).
- **Diagnostics / results** — `core/diag` (JSONL `Sink` with a settable path,
  stable `code`/`category`, text renderer) backs `--emit diagnostics:` and the
  `--result-json` error block; the CLI adds the process-level `error.class`.
- **Blob I/O** — the existing `lgraph.save`/`lgraph.match` with a caller-set
  `path:` (today `path:lgdb_yosys`) redirected to `--workdir`; the lgdb
  directory *is* the `design` blob.

Deferred to follow-ups (the kernel ships without them): `--recipe-file` TOML
(no TOML dep in `MODULE.bazel` yet — built-in recipes ship first);
`--emit-dir`+manifest hashes (need [3c]); sharper diag/depfile spans (need [1f]).

### Implementation status (1y-bazel v0, landed)

The kernel lives in `lhd/` (`bazel build //lhd:lhd` → `bazel-bin/lhd/lhd`).
It drives the registered EPRP methods programmatically
(`Eprp::run_method_now`, no `|>` strings, stdout captured per step into
`--workdir/logs/`) plus the direct C++ seams (`Lnast::export_into`/`adopt`,
`uPass_tolg::run`, `hhds::Forest::save/load`, `livehd::Hhds_graph_library`).

**Kind vocabulary (revised 2026-06-04):** the IR kinds are `ln:` (the
design's LNAST units — an `hhds::Forest::save` directory: `forest.txt` +
binary tree bodies, attrs included, plus an lhd `manifest.json` unit index)
and `lg:` (the design's LGraphs — an `hhds::GraphLibrary::save` directory:
`library.txt` + binary graph bodies). `lnast:`/`design:` remain accepted as
aliases. Because a design always holds *many* units/graphs, `ln:`/`lg:`/
`pyrope:` are directory containers (`--emit-dir` only — no single-file form);
`--emit verilog:PATH` stays as the one single-file output (the name-sorted
netlist concatenation). The language word on `elaborate`/`compile` is
optional (inferred from `.prp` vs `.v`/`.sv`), and `ln:`/`lg:` inputs may be
given positionally. The canonical per-file → top flow:

```bash
# per pyrope file, in parallel (no imports)
lhd elaborate f1.prp --emit-dir ln:f1_lns/ --emit-dir lg:f1_lgs/
# a file importing f1: its pre-elaborated ln: rides along
lhd elaborate f2.prp ln:f1_lns/ --emit-dir ln:f2_lns/ --emit-dir lg:f2_lgs/
# top target: aggregate ln: units into ONE library (gid-consistent), then synth
lhd elaborate ln:f1_lns/ ln:f2_lns/ --top foo --emit-dir lg:top_lgs/
lhd synth lg:top_lgs/ --recipe O1 --emit-dir lg:top_opt_lgs/ --emit verilog:top.v
```

**Dependency discovery:** `lhd scan f1.prp f2.prp` parses each file and
reports its `import(...)` strings in the result's `"scan"` member — imports
are comptime string literals (import.md), so the list is exact without
elaborating. Raw strings today; resolved paths land with the import resolver,
which will also feed `--depfile` (Make/Ninja) and an `unused_inputs_list`
Bazel rule so BUILD dep chains are machine-maintained, never hand-edited.

**Emit-driven lowering:** the kernel derives the tolg gate from the requested
emits — `--emit-dir lg:` / `--emit verilog:` run the LNAST→LGraph lowering,
anything else skips it (the CLI-level `tolg:0|1`). The symmetric toln gate is
also derived (2026-06-05): when neither the tolg lowering nor any post-upass
LNAST emit (`--emit-dir ln:/pyrope:/lnast-dump:`) consumes the rewritten tree,
the kernel passes `pass.upass toln:0` — the runner then skips the whole
staging build, the post-walk DCE, and the coalescer (every pass still
dispatches, so diagnostics are unchanged). An explicit `--set upass.toln=…`
overrides the derivation.

**Reader trichotomy (2026-06-04):** `--reader yosys-verilog|yosys-slang|slang`
(old `yosys|slang` two-value spelling rejected). The `yosys-*` readers go
through yosys (native verilog frontend / the slang.so plugin) into LGraphs;
`--reader slang` is the **direct `inou.slang` SV→LNAST front-end** — the
design becomes LNAST units and the rest of the flow is the pyrope one, so
`.sv → ln:/pyrope:` are now supported matrix cells. Its `lg:/verilog:` emits
stay locked `unsupported`: inou.slang still emits the pre-typesystem LNAST
conventions (module-level `__ubits` stores, no comb-lambda io_meta), which
tolg cannot lower — modernizing inou.slang is the prerequisite engine work
(also what a `slang_compile.sh` migration needs). Default is `yosys-slang`
(unchanged behavior). `//lhd/tests:lhd_reader_test` locks the contract.

**Test/script drivers migrated; lgshell REMOVED (2026-06-04):** every
sh/py harness that drove flows through the lgshell REPL now drives the kernel
(`prplib.py`, `yosys_compile.sh`, `slang_compile.sh`, the `pass/upass` +
`prp_writer` sh tests, `check_doc.sh`, `constprop_contract.py`,
`bench_upass.sh`). **`lhd lsp`** serves the Pyrope LSP (the `lsp/` lib's
`run_stdio()`; no result envelope — stdio belongs to JSON-RPC; the
`scripts/prplsp` wrapper execs it). With nothing left depending on it, the
`main/` REPL (and its `@replxx` terminal dependency) was **deleted**; lhd is
the only driver. Casualty to revive: the liberty/opentimer power flow
(docs/power.md) needs an lhd verb — `inou.liberty`/`pass.opentimer` stay in
the tree but no binary links them. The old `upass_order_parse_test` (the REPL
scanner's label-value validation, which `run_method_now` bypassed) was
removed as superseded; lhd's own usage validation is covered by
`lhd_reader_test` / `lhd_config_test`.

**`--config lhd.toml` (2026-06-04):** pass-flag defaults as a declared input
file — a strict TOML *subset* (`#` comments, `[upass]/[cprop]/[bitwidth]`
tables, `key = value` with quoted strings / booleans / integers; top level
takes only `recipe`); anything else is a `config` error, and a typo'd pass
table errors rather than no-ops. File entries are defaults: explicit
`--set`/`--recipe` always win, and the `recipe` key is ignored by commands
with no recipe slot (one lhd.toml can serve every step of a flow). Folded in
before run_id hashing, so a config file and the equivalent explicit flags
hash identically. No third-party TOML dep (`lhd describe config` documents
the schema); `//lhd/tests:lhd_config_test` locks it. This is kernel-level
per-invocation config — the *persistent tag* `lhd.toml` of the agent layer
below remains 2y-agent work.

Validated flows (all covered by `//lhd/tests`):

- The full 4×4 (input × output) kind matrix over the `inou/prp/tests/equiv`
  golden pair (`lhd_flow-<in>-to-<out>`): supported cells run end-to-end,
  impossible cells (`lg→ln`, `lg→pyrope`, `verilog→ln/pyrope`: no
  LGraph→LNAST decompiler) are locked to the `unsupported` error contract.
- `lhd_equiv_test`: `prp→ln→lg→v1`, `prp→lg→v2`, `verilog0→lg→v3`, each
  LEC-equivalent (lgcheck) to the golden verilog0.
- Reloaded `ln:` units re-run upass (io_meta/bw_meta are not serialized) and
  produce byte-identical Verilog vs. the in-memory pipeline; same inputs →
  same `run_id` (content hash); timestamps only from `SOURCE_DATE_EPOCH`.
- `//tools:lhd.bzl` (`lhd_verilog`, `lhd_pyrope_lnast`) runs the kernel as a
  hermetic genrule action (smoke target `//lhd/tests:trivial_net`). The lhd
  binary rides in `srcs` (target config) because the exec config lacks the
  repo's `-std=c++23`.

v0 deviations (beyond the deferrals above): Pyrope units are per-*file*
today, not per-function (`inou.prp` yields one LNAST per file; function-level
splitting wants the func_extract seam); `--emit graphviz|metadata` answer
`unsupported`; `--emit-dir pyrope:DIR/` re-emits post-upass LNAST units as
`.prp` via `pass.prp_writer` (needs ln:/pyrope inputs); **linking multiple
`lg:` libraries is `unsupported`** (gids are library-scoped and live inside
`body.bin` — a real gid-remapping linker is future work; aggregate from `ln:`
units instead, which re-lowers everything into one fresh library and so
discards any per-file local synthesis — the derivation-hash manifest plan
above is the path to keeping it); `elaborate f.prp ln:imports/` makes the
imported units visible on the pipe, with resolution fidelity tracking the
import work (`import.md`); `check` shells out to `inou/yosys/lgcheck` (repo
root, runfiles, or `LHD_LGCHECK`; run from `--workdir` with an absolute
`--yosys` so lgcheck's trace/log droppings stay out of the caller's cwd);
`--set pass.idx.flag` index addressing is rejected until a recipe repeats a
pass.

The full (input kind × output kind) flow matrix — supported cells end-to-end,
unsupported cells locked to the `unsupported` error contract — is covered by
`//lhd/tests:lhd_flow-<in>-to-<out>`, and `//lhd/tests:lhd_equiv_test` proves
`prp→ln→lg→v1`, `prp→lg→v2`, and `verilog0→lg→v3` all LEC-equivalent to the
golden verilog of the `inou/prp/tests/equiv` pair.

Note on `check` soundness (2026-06-04): lgcheck's terminal bounded-miter
stage had run `yosys -q`, which silences the SAT report — the result grep saw
an empty log and *any* design pair "passed" once the proof stages were
inconclusive. The stage now uses the `sat … -prove trigger 0` formulation
(after `hagent/tool/equiv_check.py`) with explicit FAIL/SUCCESS markers, treats
a yosys hard error as non-equivalent, and never passes on an inconclusive run.
This unmasked pre-existing verilog→LGraph→verilog miscompiles (28
`yosys_compile.sh` tests; `0sb?` unknown-const leaks and unset-width wrapper
truncation in cgen) that are tracked as engine follow-up work, plus a real
cprop/tolg sign bug in the pyrope flow that is fixed (`get_mask(a,-1)` tposs
bypass).

## Tag workspace mode (2y-agent)

Everything below is the agent layer. It is sugar over the kernel above: a
`@tag` is a named, persistent bundle of (inputs, config, workdir, outputs),
and `lhd run <step> @tag` resolves that bundle and calls the same stateless
step engine, then records the result in the tag's run log. An agent can always
drop to the kernel for a quick one-shot.

The `<step>` vocabulary is the kernel's (`elaborate`/`synth`/`check`/`compile`).
The `parse` / `cprop` / `cgen-verilog` step names in the examples that follow
predate the kernel design and are illustrative only; they will be reconciled to
the kernel verbs when 2y-agent lands.

## @tag Sigil

Tags are always prefixed with `@`. This makes them unambiguous in any
position — the CLI parser, and the calling agent, never confuse a tag
with a command or option.

A tag is either a plain name or a path:

- `@tst1` → stored under `<cache>/tags/tst1/`
- `@./my_build` → stored at `./my_build/`

Plain tag names must not contain `.` or `/`.

## CLI Commands

### setup — create or modify a tag

```bash
lhd setup @tst1 --top foo --files foo.v bar.v
lhd setup @tst1 --set top=bar                    # modify existing
lhd setup @tst2 --top foo --files foo.v --input original=@tst1
lhd setup @tst1 --reuse                           # reuse if compatible
```

`setup` is create-or-update and must not destroy an existing tag by default.
Destructive recreation is explicit:

```bash
lhd tag reset @tst1 --yes
```

Agents should prefer non-destructive updates through `setup` or `config set`.

### run — execute a step or recipe

```bash
lhd run parse @tst1
lhd run cprop @tst1
lhd run cgen-verilog @tst1
lhd run check @tst1 --reference @tst2
lhd run recipe:roundtrip @tst1
lhd run recipe:roundtrip @tst1 --stop-after cprop
lhd run recipe:roundtrip @tst1 --dry-run
```

Step-specific options:

```bash
lhd run cprop @tst1 --set aggressive=true
lhd run cgen-verilog @tst1 --set odir=custom_out
```

`--dry-run` emits a machine-readable plan without changing the tag. The plan
includes resolved steps, input artifacts, output artifacts, config values,
cache/skip decisions, and the exact commands that would run.

### status — inspect a tag

```bash
lhd status @tst1
```

### config — inspect, edit, and validate tag config

```bash
lhd config show @tst1
lhd config set @tst1 top=bar
lhd config set @tst1 steps.cprop.aggressive=true
lhd config validate @tst1
lhd config schema
```

`setup --set key=value` is allowed as convenience sugar, but `config` is the
canonical agent API for config edits and validation.

### list — discover patterns or inspect tag-scoped design data

```bash
lhd list
lhd list steps
lhd list recipes
lhd list modules @tst1
lhd list hierarchy @tst1 --top foo
lhd list modules @tst1 --from original
```

### describe — get step or list-pattern details (JSON)

```bash
lhd describe parse
lhd describe cprop
lhd describe recipe:roundtrip
lhd describe modules
lhd describe hierarchy
```

### help — human-readable (calls describe internally)

```bash
lhd run parse --help
lhd --help
```

Machine-readable commands support `--format json|jsonl|text`. Agent defaults
are JSON for single-object commands (`list`, `describe`, `status`,
`config show`) and JSONL for streaming commands (`run`). `text` is for humans.

## Tag Directory Structure

A tag directory is shared between runner and lhd. Each tool prefixes
its own logs and results to avoid collisions. Runs are stored under unique
run directories so concurrent agents do not race on log names or JSONL
append order.

```
tst1/
  lhd.toml                      # lhd config — agent-editable
  runner.toml                      # runner config — agent-editable
  lgdb/                            # LGraph database (LiveHD internal state)
  out/                             # generated outputs (verilog, reports, etc.)
  lock                              # per-tag lock for mutating commands
  latest -> runs/20260409T100000Z_7f3a
  runs/
    20260409T100000Z_7f3a/
      lhd_results.jsonl          # lhd structured results for this run
      runner_results.jsonl          # runner structured results for this run
      logs/
        001_lhd_parse.log        # lhd step 1
        002_lhd_cprop.log        # lhd step 2
        001_runner_synth.log        # runner step 1
        003_lhd_cgen-verilog.log # lhd step 3
        002_runner_sim.log          # runner step 2
```

### Log file naming

Files under a run's `logs/` use the pattern `NNN_<tool>_<step>.log`:

- Counter is per-tool (lhd and runner each maintain their own sequence)
- `ls logs/ | sort` gives a useful view; tool prefix disambiguates origin
- Running the same step twice increments the counter: `002_lhd_cprop.log`,
  `004_lhd_cprop.log`

### Locking

Commands that mutate a tag (`setup`, `config set`, `tag reset`, `run`) take a
per-tag lock. Read-only commands (`status`, `list`, `describe`, `config show`,
`config validate`) do not require the mutation lock unless they need a
consistent snapshot while a run is active. Lock failures return a structured
`lock_timeout` error with the owning `run_id` when known.

## Config: lhd.toml

The tag config is TOML. It is created by `lhd setup` and can be edited
directly by agents or modified via `lhd config set @tag key=value`.

```toml
top = "foo"
files = ["foo.v", "bar.v"]

[inputs]
original = "@tst1"           # named read-only reference to another tag

[steps.cprop]
aggressive = true            # step-specific defaults

[recipes.roundtrip]
steps = ["parse", "cprop", "cgen-verilog", "check"]

[recipes.compile]
steps = ["parse", "cprop", "cgen-verilog"]
```

### Config precedence

1. CLI `--set key=value` (ephemeral, not written to toml)
2. `lhd.toml` in the tag directory
3. Built-in defaults

### Setup snapshotting

`lhd setup` is a snapshot. Once a tag is created, changes to
project-level defaults do not affect it. The tag's `lhd.toml` is
self-contained.

## Output: lhd_results.jsonl

One JSON object per line, appended after each step in the current run
directory. JSONL format: append-only, crash-safe, agents read individual
lines.

### Result schema

Every result object includes a stable core envelope:

```json
{
  "schema_version": 1,
  "tool": "lhd",
  "command": "run",
  "step": "parse",
  "status": "pass",
  "tag": "@tst1",
  "run_id": "20260409T100000Z_7f3a",
  "invocation_id": "7f3a8e6c",
  "seq": 1,
  "exit_code": 0,
  "started_at": "2026-04-09T10:00:00Z",
  "ended_at": "2026-04-09T10:00:01Z",
  "elapsed_ms": 1200,
  "inputs": ["foo.v"],
  "outputs": ["lgdb"],
  "log": "runs/20260409T100000Z_7f3a/logs/001_lhd_parse.log"
}
```

Step-specific fields are allowed, but the core keys above are stable. New
fields may be added under the same `schema_version`; incompatible changes
require a new `schema_version`.

Under `--deterministic` (the build-system default), the non-reproducible
envelope fields change: `run_id` is a content hash of (tool version + input
bytes + resolved config) rather than `<timestamp>_<rand>`, and `started_at` /
`ended_at` are either omitted or pinned to `SOURCE_DATE_EPOCH`. Everything else
in the object is already a pure function of inputs. The tag layer keeps the
timestamp+random `run_id` so concurrent agents never collide on a run dir.

Valid `status` values are `pass`, `fail`, `skipped`, and `pending`. A single
step exits 0 only for `pass`. A recipe exits 0 only when every emitted step is
`pass`; dependency skips and pending work make the process non-zero unless the
command explicitly documents a different policy.

### Successful step

```json
{"schema_version":1,"seq":1,"tool":"lhd","command":"run","step":"parse","status":"pass","tag":"@tst1","run_id":"20260409T100000Z_7f3a","invocation_id":"7f3a8e6c","exit_code":0,"started_at":"2026-04-09T10:00:00Z","ended_at":"2026-04-09T10:00:01Z","elapsed_ms":1200,"args":{"files":["foo.v"],"top":"foo"},"log":"runs/20260409T100000Z_7f3a/logs/001_lhd_parse.log","inputs":["foo.v"],"outputs":["lgdb"]}
```

### Failed step

```json
{"schema_version":1,"seq":3,"tool":"lhd","command":"run","step":"cgen-verilog","status":"fail","tag":"@tst1","run_id":"20260409T100000Z_7f3a","invocation_id":"7f3a8e6c","exit_code":1,"started_at":"2026-04-09T10:00:06Z","ended_at":"2026-04-09T10:00:06Z","elapsed_ms":300,"error":{"class":"internal","message":"unknown node type Flop_async","hint":"check the full log for the producer of this node","tail":["cgen_verilog.cpp:142: unknown node type Flop_async","  in module: foo"],"repro":"lhd run cgen-verilog @tst1"},"log":"runs/20260409T100000Z_7f3a/logs/003_lhd_cgen-verilog.log"}
```

### Error classification

Every failure includes `error.class`:

| error.class   | Meaning                                        |
|---------------|------------------------------------------------|
| `usage`       | Invalid CLI usage or unsupported argument       |
| `syntax`      | Input file has syntax errors                    |
| `internal`    | LiveHD bug or unhandled case                    |
| `equiv_fail`  | Equivalence check failed                        |
| `signal`      | Process killed by signal (segfault, abort)      |
| `timeout`     | Step exceeded time limit                        |
| `missing_file`| Required file or path does not exist            |
| `config`      | Missing or invalid configuration                |
| `dependency`  | Required external tool or prior artifact absent |
| `stale_state` | Tag state is inconsistent with requested action |
| `lock_timeout`| Could not acquire the tag lock in time          |
| `unsupported` | Requested feature is known but not implemented  |
| `skipped`     | Step did not run because a dependency failed    |

### Failure diagnostic block

On failure, the JSONL entry includes an `error` object:

- `class` — machine-parseable classification
- `message` — one-line summary
- `tail` — last 10 lines of stderr (immediate context)
- `hint` — actionable suggestion
- `repro` — exact command to reproduce

```json
{"schema_version":1,"seq":2,"tool":"lhd","command":"run","step":"parse","status":"fail","tag":"@tst1","run_id":"20260409T100000Z_7f3a","exit_code":1,"elapsed_ms":100,"error":{"class":"missing_file","message":"file not found: missing.v","hint":"check files list in lhd.toml","tail":["Error: cannot open missing.v: No such file or directory"],"repro":"lhd run parse @tst1"},"log":"runs/20260409T100000Z_7f3a/logs/002_lhd_parse.log"}
```

## CLI stdout

Stdout is reserved for the selected output protocol. No banners, progress
messages, raw tool logs, or human commentary are written to stdout in JSON or
JSONL mode. Raw output goes to log files; `--verbose` may mirror raw output to
stderr only.

`lhd run` prints the JSONL line(s) to stdout. For single steps, one line.
For recipes, one line per step as it completes (streaming):

```bash
$ lhd run recipe:roundtrip @tst1
{"schema_version":1,"seq":1,"tool":"lhd","step":"parse","status":"pass","elapsed_ms":1200,"log":"runs/20260409T100000Z_7f3a/logs/001_lhd_parse.log"}
{"schema_version":1,"seq":2,"tool":"lhd","step":"cprop","status":"pass","elapsed_ms":2100,"log":"runs/20260409T100000Z_7f3a/logs/002_lhd_cprop.log"}
{"schema_version":1,"seq":3,"tool":"lhd","step":"cgen-verilog","status":"fail","elapsed_ms":300,"error":{"class":"internal","message":"..."},"log":"runs/20260409T100000Z_7f3a/logs/003_lhd_cgen-verilog.log"}
```

`--format text` is the human-friendly mode and may print summaries, progress,
and abbreviated diagnostics to stdout.

## List Patterns

`lhd list` is the single entrypoint for enumeration. It can either:

- enumerate what patterns exist
- enumerate objects inside a specific tag

There is no separate `--list` flag. `list` is its own command.

This keeps the meaning clean:

- `list` = enumerate available patterns or matching objects
- `describe` = what does one step/recipe/list-pattern do?
- `status` = what is the high-level state of this tag?

Examples:

```bash
$ lhd list
{"schema_version":1,"patterns":[
  {"name":"steps","scope":"global"},
  {"name":"recipes","scope":"global"},
  {"name":"modules","scope":"tag"},
  {"name":"hierarchy","scope":"tag"},
  {"name":"stats","scope":"tag"},
  {"name":"history","scope":"tag"}
]}

$ lhd list steps
{"schema_version":1,"pattern":"steps","items":["parse","cprop","cgen-verilog","check","lnast-tolg","lnast-fromlg","graphviz"],"aliases":{"parse":"parse.verilog","cprop":"pass.cprop","cgen-verilog":"emit.verilog","check":"check.equiv"}}

$ lhd list recipes
{"schema_version":1,"pattern":"recipes","items":["roundtrip","compile"]}

$ lhd list modules @tst1
{"schema_version":1,"pattern":"modules","tag":"@tst1","items":["foo","alu","regfile"]}

$ lhd list hierarchy @tst1
{"schema_version":1,"pattern":"hierarchy","tag":"@tst1","top":"foo","tree":{"name":"foo","children":[{"name":"alu"},{"name":"regfile"}]}}

$ lhd list hierarchy @tst1 --top alu
{"schema_version":1,"pattern":"hierarchy","tag":"@tst1","top":"alu","tree":{"name":"alu","children":[]}}

$ lhd list modules @tst1 --from original
{"schema_version":1,"pattern":"modules","tag":"@tst1","source":"input:original","items":["foo","alu","regfile"]}

$ lhd list partitions @tst1
{"schema_version":1,"pattern":"partitions","tag":"@tst1","items":[
  {"name":"add","kind":"comb","latency":0,"interface_hash":"0x1a2b…",
   "state_shape_hash":"0x0","clock":null},
  {"name":"mac","kind":"pipe","latency":2,"interface_hash":"0x3c4d…",
   "state_shape_hash":"0x5e6f…","clock":"clk"},
  {"name":"cpu","kind":"mod","interface_hash":"0x7a8b…",
   "state_shape_hash":"0x9c0d…","clock":"clk","ext":["imem","dmem"]}
]}
```

Recommended initial tag-scoped patterns:

- `modules` — list module names known in the tag
- `partitions` — list partitions (`comb` / `pipe` / `mod`) with kind,
  latency, clock domain, and `interface_hash` / `state_shape_hash`.
  See `architecture.md §3`.
- `hierarchy` — instance/module tree top at top or `--top`
- `stats` — summary counts (modules, cells, flops, memories, edges)
- `history` — which LiveHD steps have been run for the tag

Minimal selector options:

- `--top <module>` — top the query at one module
- `--from <input-name>` — list against a named input tag from `[inputs]`
- `--format <name>` — optional alternate rendering such as `tree` or `flat`

Patterns that do not need a tag:

- `steps` — available LiveHD execution steps
- `recipes` — available named recipes

If a pattern requires a tag and none is provided, the CLI should return a
structured `config` error explaining that the pattern is tag-scoped.

## Discovery Protocol

Agents use `list` and `describe`:

### Step 1: list

```bash
$ lhd list
{"schema_version":1,"patterns":[
  {"name":"steps","scope":"global"},
  {"name":"recipes","scope":"global"},
  {"name":"modules","scope":"tag"},
  {"name":"hierarchy","scope":"tag"},
  {"name":"stats","scope":"tag"},
  {"name":"history","scope":"tag"}
]}

$ lhd list steps
{"schema_version":1,"pattern":"steps","items":["parse","cprop","cgen-verilog","check","lnast-tolg","lnast-fromlg","graphviz"],"aliases":{"parse":"parse.verilog","cprop":"pass.cprop","cgen-verilog":"emit.verilog","check":"check.equiv"}}

$ lhd list recipes
{"schema_version":1,"pattern":"recipes","items":["roundtrip","compile"]}
```

### Step 2: describe

```bash
$ lhd describe parse
{"schema_version":1,"name":"parse","canonical":"parse.verilog","description":"Parse Verilog/SystemVerilog into LGraph via Yosys","args":{"required":[{"name":"files","type":"path[]","repeatable":true},{"name":"top","type":"string"}],"optional":[{"name":"path","type":"path","default":"lgdb"}]},"inputs":["verilog"],"outputs":["lgdb"],"cache":"writes-tag-state","examples":["lhd run parse @tst1"]}

$ lhd describe cprop
{"schema_version":1,"name":"cprop","canonical":"pass.cprop","description":"Constant propagation and optimization pass","args":{"required":[],"optional":[{"name":"aggressive","type":"bool","default":false},{"name":"max_iterations","type":"integer","default":10}]},"inputs":["lgdb"],"outputs":["lgdb"],"cache":"mutates-tag-state","examples":["lhd run cprop @tst1 --set aggressive=true"]}

$ lhd describe recipe:roundtrip
{"schema_version":1,"name":"recipe:roundtrip","steps":["parse","cprop","cgen-verilog","check"],"description":"Full compile + equivalence check","supports_dry_run":true}

$ lhd describe modules
{"schema_version":1,"name":"modules","description":"List module names available from a tag","scope":"tag","selectors":{"optional":[{"name":"from","type":"string"}]},"outputs":["items"]}

$ lhd describe hierarchy
{"schema_version":1,"name":"hierarchy","description":"Return module/instance hierarchy top at top or --top","scope":"tag","selectors":{"optional":[{"name":"top","type":"string"},{"name":"from","type":"string"},{"name":"format","type":"enum","values":["tree","flat"],"default":"tree"}]},"outputs":["tree"]}

$ lhd describe partitions
{"schema_version":1,"name":"partitions","description":"List partitions with kind, latency, clock domain, and content hashes","scope":"tag","selectors":{"optional":[{"name":"kind","type":"enum","values":["comb","pipe","mod"]},{"name":"top","type":"string"},{"name":"from","type":"string"}]},"outputs":["items"]}
```

`describe` should expose enough structure for agents to build a valid command
without scraping prose: argument type, default, enum values when applicable,
repeatability, required prior artifacts, produced artifacts, cache/state
behavior, examples, and aliases.

### Hot-reload reporting

Steps that may hot-reload (`run sim @tag --hot`) report the tier in
the JSONL result (`simulation.md` hot-reload tiers). Agents read
`tier` to decide whether the run is verification-grade or
exploration-only:

```jsonl
{"schema_version":1,"seq":4,"tool":"lhd","step":"sim","status":"pass","tier":"hot-debug","elapsed_ms":200,"at":"…"}
{"schema_version":1,"seq":5,"tool":"lhd","step":"sim","status":"pass","tier":"hot-approx","warning":"checkpoint stale; not for LEC","elapsed_ms":300,"at":"…"}
{"schema_version":1,"seq":6,"tool":"lhd","step":"sim","status":"pass","tier":"cold","reason":"state_shape_hash changed","elapsed_ms":4100,"at":"…"}
```

### --help (human rendering of describe)

```
$ lhd run cprop --help
cprop — Constant propagation and optimization pass

Optional:
  aggressive       Enable aggressive optimizations (default: false)
  max_iterations   Maximum optimization iterations (default: 10)

Reads: lgdb
Writes: lgdb
```

## Multi-tag Workflows

Using `@tag` references, agents compose multi-tag flows:

```bash
# Compile original
lhd setup @orig --top foo --files foo.v
lhd run recipe:compile @orig

# Experiment with different optimization
lhd setup @opt_exp --top foo --files foo.v --input baseline=@orig
lhd run parse @opt_exp
lhd run cprop @opt_exp --set aggressive=true
lhd run cgen-verilog @opt_exp

# Compare outputs
lhd run check @opt_exp --reference @orig
```

## Relationship to lgshell / EPRP

The `lhd` CLI is a structured argv-based frontend. It calls the
existing EPRP/pass machinery internally, but agents never need to quote or
generate `|>` command strings. The legacy `lgshell` REPL (and its replxx
terminal dependency) was **removed 2026-06-04** — lhd is the only driver.

The stateless kernel replaced the `printf 'inou.prp files:… |> pass.upass
…' | lgshell` pattern that test and `*_compile.sh` scripts used (e.g.
`pass/prp_writer/tests/prp_writer_roundtrip_test.sh`,
`inou/yosys/tests/yosys_compile.sh`): those pipe a `|>` string into stdin and
grep stdout for success/error markers. A hermetic `lhd compile … --emit …
--result-json …` invocation gives those scripts (and any Bazel rule that
wraps them) declared outputs and a parseable result instead of grepping mixed
stdout.

Implementation rule: the agent-facing `lhd` binary must not print existing
REPL banners, prompts, command echoes, or raw pass output on stdout in JSON or
JSONL mode. Current EPRP labels can seed `describe`, but the metadata needs to
grow beyond `required/default/help` to include typed arguments and artifacts.

## Relationship to Runner

LiveHD's CLI follows the same tag-based model as `hagent/runner/`. The
key difference: runner orchestrates external tools (build systems, simulators),
while lhd wraps internal compiler passes. Both can share a tag directory.

| Concept             | runner                        | lhd                        |
|---------------------|-------------------------------|-------------------------------|
| Tag creation        | `runner setup @tag`           | `lhd setup @tag`           |
| Step execution      | `runner run <api> @tag`       | `lhd run <step> @tag`      |
| Config file         | `runner.toml`                 | `lhd.toml`                 |
| Results file        | `runner_results.jsonl`        | `lhd_results.jsonl`        |
| Log naming          | `NNN_runner_<step>.log`       | `NNN_lhd_<step>.log`       |
| Tag sigil           | `@tag`                        | `@tag`                        |
| Discovery           | `runner list` / `describe`    | `lhd list` / `describe`    |
| Tag introspection   | not defined yet               | `lhd list <pattern> @tag`  |
| Test orchestration  | `runner run @tag` (parallel)  | `lhd run recipe:X @tag`    |
