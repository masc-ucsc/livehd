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
- **1y-agent** — the stateful workspace: `@tag` directories, `setup/run/
  status/list/describe`, JSONL run logs, multi-tag experiments. Each
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

This is the kernel: a single hermetic invocation that behaves as a pure
function `(declared inputs, config) → (declared outputs, exit code)`. It is
what a Bazel rule or a Makefile recipe calls. It never reads or writes ambient
state — no `@tag`, no `~/.cache/livehd`, no history file, no lock, no `latest`
symlink.

### One-shot invocation

```bash
livehd compile \
  --top foo \
  --files foo.v bar.v \
  --include-dir rtl/inc \
  --emit verilog:$(RULEDIR)/foo.gen.v \
  --emit diagnostics:$(RULEDIR)/foo.diag.jsonl \
  --depfile $(RULEDIR)/foo.d \
  --result-json $(RULEDIR)/foo.result.json \
  --workdir $(TMPDIR) \
  --deterministic
```

`compile` is the stateless form of `recipe:compile`: the whole front-end runs
in one action. Individual steps (`parse`, `cprop`, `cgen-verilog`, …) are also
callable directly in this mode with the same flags; the step vocabulary is
identical to the tag layer.

### Why a build system needs this (and rejects the tag layer)

A Bazel action must be hermetic, deterministic, and parallel-safe. The
agent-oriented features documented later are exactly what it cannot tolerate,
so the kernel omits them:

| Tag-layer feature         | Why a build action rejects it                       |
|---------------------------|-----------------------------------------------------|
| `run_id` = timestamp+rand | Non-deterministic → defeats the action/remote cache |
| `~/.cache/livehd` tag dir | Not hermetic — reads state outside declared inputs  |
| `latest ->` symlink       | Bazel forbids mutable/dangling symlinks in outputs  |
| per-tag `lock` file       | Fights Bazel's sandboxed parallelism                |
| `odir:` tool-named files  | Bazel must predeclare every output path             |

### Flags specific to the kernel

- `--emit KIND:PATH` (repeatable). Produce exactly the named artifact at the
  caller-chosen path. Replaces the tag layer's `odir:` (tool-chosen filenames
  a build rule cannot predeclare). Emit kinds map to declared Bazel outputs:
  `verilog`, `lnast`, `graphviz`, `diagnostics`, `netlist-json`, … The tool
  writes *only* to these paths (plus `--workdir`); an unknown kind is a
  `usage` error.
- `--result-json PATH`. Write the structured result object (same schema as a
  JSONL line) to a declared file rather than stdout. Build actions read a
  declared output far more cleanly than captured stdout.
- `--workdir PATH`. All scratch — including the ephemeral lgdb — lives here,
  never the global cache. Bazel passes `$(RULEDIR)` or a sandbox `TMPDIR`.
  Each invocation is self-contained in its own workdir, so N actions run in
  parallel with zero contention and no lock.
- `--deterministic` (and honor `SOURCE_DATE_EPOCH`). Make output bytes a pure
  function of inputs:
  - `run_id` becomes a content hash of (tool version + input bytes + resolved
    config), not wall-clock + random.
  - No embedded timestamps in emitted Verilog/JSON; gate any "generated at"
    header behind the epoch.
  - Stable ordering everywhere (module emit order, JSON key order).
  This is the single most important departure from today's schema: the
  timestamp+random `run_id` must become *optional*, not mandatory.
- `--include-dir DIR` (repeatable, `-I`). Search path for Verilog `` `include ``
  and Pyrope `import`. See *Dependency files* below.
- `--depfile PATH`. Emit a Make-syntax dependency file. See *Dependency files*.
- `--hermetic` (implied by `--deterministic`). Resolve `import`/`include`
  *only* within `--files` + `--include-dir`; reaching a file outside the
  declared set is a `missing_file` error, not a silent filesystem read.

### Exit-code contract

For a build action, success is *only* the process exit code:

- `0` — `pass`.
- non-zero — `fail`; `error.class` in the result / `--result-json` disambiguates.

No partial success: `compile` exits 0 only if every constituent step is
`pass`. This is the same policy recipes use in the tag layer, restated here
because a build rule keys on nothing else.

### Clean stdout / no REPL chatter

In this mode the binary emits nothing on stdout except the selected protocol
(or nothing at all when `--result-json` is given). The legacy `lgshell` path
that prints `livehd cmd …`, `Welcome to livehd!`, echoes the input line, and
`command aborted…` must not run here. Diagnostics go to the `diagnostics`
emit, the `--result-json` error block, and/or stderr — never intermixed on
stdout.

### Bazel integration

Ship a thin Starlark ruleset in-repo (`//tools:livehd.bzl`) so users write:

```python
load("//tools:livehd.bzl", "livehd_verilog")

livehd_verilog(
    name = "foo_compiled",
    top  = "foo",
    srcs = ["foo.v", "bar.v"],
    out  = "foo.gen.v",
)
```

The macro sets `--deterministic`, `--workdir`, `--depfile`, and `--emit`
correctly so callers don't reconstruct the hermetic contract by hand. A
`--print-inputs` / `--dry-run` mode lets a Starlark aspect discover the
transitive input set when sources are not all listed up front.

## Dependency files (--depfile)

Pyrope (`import`) and Verilog (`` `include ``, package imports) pull in files
that are *not* named on the command line. `--files foo.prp` may transitively
read `bar.prp` and `baz.prp`. A build system must know those edges, for two
independent reasons:

1. **Incrementality.** If `foo.prp` imports `bar.prp`, editing `bar.prp` must
   rebuild the target — even though `bar.prp` never appeared on the command
   line. Without the edge, Make/Bazel rebuild only when `foo.prp` changes and
   silently serve stale output.
2. **Hermeticity (Bazel).** Bazel runs the action in a sandbox containing only
   declared inputs. If `bar.prp` is read but undeclared, the build either
   fails in the sandbox or "works" locally and breaks on a clean / remote
   build.

This is the same problem C compilers solve with `.d` files: `gcc -MMD -MF foo.d`
emits the set of headers `foo.c` actually `#include`d, and the build system
feeds that back in. LiveHD adopts the identical mechanism.

`--depfile PATH` writes a **Make-syntax** dependency file: the primary
`--emit` output(s) as target(s), and every file actually opened (top `--files`
plus everything transitively imported/included) as prerequisites:

```make
foo.gen.v: foo.v bar.v rtl/inc/defs.vh
```

Consumption:

- **Make**: `-include foo.d` — the standard auto-dependency pattern.
- **Ninja**: declare `depfile = foo.d` on the rule.
- **Bazel**: a custom rule consumes it for include-scanning / input
  validation, the same way `cc_*` actions consume compiler `.d` output.

Under `--deterministic`, the depfile content is itself stable: prerequisites
sorted, paths relative to `--workdir` / the exec root, no absolute or
timestamped entries.

Relationship to `--hermetic`: the depfile *reports* what was read; `--hermetic`
*enforces* that everything read was declared. Emit the depfile in every build;
turn on `--hermetic` under Bazel so an undeclared import fails loudly
(`missing_file`) instead of silently widening the input set.

## Tag workspace mode (1y-agent)

Everything below is the agent layer. It is sugar over the kernel above: a
`@tag` is a named, persistent bundle of (inputs, config, workdir, outputs),
and `livehd run <step> @tag` resolves that bundle and calls the same stateless
step engine, then records the result in the tag's run log. An agent can always
drop to the kernel for a quick one-shot.

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
livehd setup @tst1 --top foo --files foo.v bar.v
livehd setup @tst1 --set top=bar                    # modify existing
livehd setup @tst2 --top foo --files foo.v --input original=@tst1
livehd setup @tst1 --reuse                           # reuse if compatible
```

`setup` is create-or-update and must not destroy an existing tag by default.
Destructive recreation is explicit:

```bash
livehd tag reset @tst1 --yes
```

Agents should prefer non-destructive updates through `setup` or `config set`.

### run — execute a step or recipe

```bash
livehd run parse @tst1
livehd run cprop @tst1
livehd run cgen-verilog @tst1
livehd run check @tst1 --reference @tst2
livehd run recipe:roundtrip @tst1
livehd run recipe:roundtrip @tst1 --stop-after cprop
livehd run recipe:roundtrip @tst1 --dry-run
```

Step-specific options:

```bash
livehd run cprop @tst1 --set aggressive=true
livehd run cgen-verilog @tst1 --set odir=custom_out
```

`--dry-run` emits a machine-readable plan without changing the tag. The plan
includes resolved steps, input artifacts, output artifacts, config values,
cache/skip decisions, and the exact commands that would run.

### status — inspect a tag

```bash
livehd status @tst1
```

### config — inspect, edit, and validate tag config

```bash
livehd config show @tst1
livehd config set @tst1 top=bar
livehd config set @tst1 steps.cprop.aggressive=true
livehd config validate @tst1
livehd config schema
```

`setup --set key=value` is allowed as convenience sugar, but `config` is the
canonical agent API for config edits and validation.

### list — discover patterns or inspect tag-scoped design data

```bash
livehd list
livehd list steps
livehd list recipes
livehd list modules @tst1
livehd list hierarchy @tst1 --top foo
livehd list modules @tst1 --from original
```

### describe — get step or list-pattern details (JSON)

```bash
livehd describe parse
livehd describe cprop
livehd describe recipe:roundtrip
livehd describe modules
livehd describe hierarchy
```

### help — human-readable (calls describe internally)

```bash
livehd run parse --help
livehd --help
```

Machine-readable commands support `--format json|jsonl|text`. Agent defaults
are JSON for single-object commands (`list`, `describe`, `status`,
`config show`) and JSONL for streaming commands (`run`). `text` is for humans.

## Tag Directory Structure

A tag directory is shared between runner and livehd. Each tool prefixes
its own logs and results to avoid collisions. Runs are stored under unique
run directories so concurrent agents do not race on log names or JSONL
append order.

```
tst1/
  livehd.toml                      # livehd config — agent-editable
  runner.toml                      # runner config — agent-editable
  lgdb/                            # LGraph database (LiveHD internal state)
  out/                             # generated outputs (verilog, reports, etc.)
  lock                              # per-tag lock for mutating commands
  latest -> runs/20260409T100000Z_7f3a
  runs/
    20260409T100000Z_7f3a/
      livehd_results.jsonl          # livehd structured results for this run
      runner_results.jsonl          # runner structured results for this run
      logs/
        001_livehd_parse.log        # livehd step 1
        002_livehd_cprop.log        # livehd step 2
        001_runner_synth.log        # runner step 1
        003_livehd_cgen-verilog.log # livehd step 3
        002_runner_sim.log          # runner step 2
```

### Log file naming

Files under a run's `logs/` use the pattern `NNN_<tool>_<step>.log`:

- Counter is per-tool (livehd and runner each maintain their own sequence)
- `ls logs/ | sort` gives a useful view; tool prefix disambiguates origin
- Running the same step twice increments the counter: `002_livehd_cprop.log`,
  `004_livehd_cprop.log`

### Locking

Commands that mutate a tag (`setup`, `config set`, `tag reset`, `run`) take a
per-tag lock. Read-only commands (`status`, `list`, `describe`, `config show`,
`config validate`) do not require the mutation lock unless they need a
consistent snapshot while a run is active. Lock failures return a structured
`lock_timeout` error with the owning `run_id` when known.

## Config: livehd.toml

The tag config is TOML. It is created by `livehd setup` and can be edited
directly by agents or modified via `livehd config set @tag key=value`.

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
2. `livehd.toml` in the tag directory
3. Built-in defaults

### Setup snapshotting

`livehd setup` is a snapshot. Once a tag is created, changes to
project-level defaults do not affect it. The tag's `livehd.toml` is
self-contained.

## Output: livehd_results.jsonl

One JSON object per line, appended after each step in the current run
directory. JSONL format: append-only, crash-safe, agents read individual
lines.

### Result schema

Every result object includes a stable core envelope:

```json
{
  "schema_version": 1,
  "tool": "livehd",
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
  "log": "runs/20260409T100000Z_7f3a/logs/001_livehd_parse.log"
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
{"schema_version":1,"seq":1,"tool":"livehd","command":"run","step":"parse","status":"pass","tag":"@tst1","run_id":"20260409T100000Z_7f3a","invocation_id":"7f3a8e6c","exit_code":0,"started_at":"2026-04-09T10:00:00Z","ended_at":"2026-04-09T10:00:01Z","elapsed_ms":1200,"args":{"files":["foo.v"],"top":"foo"},"log":"runs/20260409T100000Z_7f3a/logs/001_livehd_parse.log","inputs":["foo.v"],"outputs":["lgdb"]}
```

### Failed step

```json
{"schema_version":1,"seq":3,"tool":"livehd","command":"run","step":"cgen-verilog","status":"fail","tag":"@tst1","run_id":"20260409T100000Z_7f3a","invocation_id":"7f3a8e6c","exit_code":1,"started_at":"2026-04-09T10:00:06Z","ended_at":"2026-04-09T10:00:06Z","elapsed_ms":300,"error":{"class":"internal","message":"unknown node type Flop_async","hint":"check the full log for the producer of this node","tail":["cgen_verilog.cpp:142: unknown node type Flop_async","  in module: foo"],"repro":"livehd run cgen-verilog @tst1"},"log":"runs/20260409T100000Z_7f3a/logs/003_livehd_cgen-verilog.log"}
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
{"schema_version":1,"seq":2,"tool":"livehd","command":"run","step":"parse","status":"fail","tag":"@tst1","run_id":"20260409T100000Z_7f3a","exit_code":1,"elapsed_ms":100,"error":{"class":"missing_file","message":"file not found: missing.v","hint":"check files list in livehd.toml","tail":["Error: cannot open missing.v: No such file or directory"],"repro":"livehd run parse @tst1"},"log":"runs/20260409T100000Z_7f3a/logs/002_livehd_parse.log"}
```

## CLI stdout

Stdout is reserved for the selected output protocol. No banners, progress
messages, raw tool logs, or human commentary are written to stdout in JSON or
JSONL mode. Raw output goes to log files; `--verbose` may mirror raw output to
stderr only.

`livehd run` prints the JSONL line(s) to stdout. For single steps, one line.
For recipes, one line per step as it completes (streaming):

```bash
$ livehd run recipe:roundtrip @tst1
{"schema_version":1,"seq":1,"tool":"livehd","step":"parse","status":"pass","elapsed_ms":1200,"log":"runs/20260409T100000Z_7f3a/logs/001_livehd_parse.log"}
{"schema_version":1,"seq":2,"tool":"livehd","step":"cprop","status":"pass","elapsed_ms":2100,"log":"runs/20260409T100000Z_7f3a/logs/002_livehd_cprop.log"}
{"schema_version":1,"seq":3,"tool":"livehd","step":"cgen-verilog","status":"fail","elapsed_ms":300,"error":{"class":"internal","message":"..."},"log":"runs/20260409T100000Z_7f3a/logs/003_livehd_cgen-verilog.log"}
```

`--format text` is the human-friendly mode and may print summaries, progress,
and abbreviated diagnostics to stdout.

## List Patterns

`livehd list` is the single entrypoint for enumeration. It can either:

- enumerate what patterns exist
- enumerate objects inside a specific tag

There is no separate `--list` flag. `list` is its own command.

This keeps the meaning clean:

- `list` = enumerate available patterns or matching objects
- `describe` = what does one step/recipe/list-pattern do?
- `status` = what is the high-level state of this tag?

Examples:

```bash
$ livehd list
{"schema_version":1,"patterns":[
  {"name":"steps","scope":"global"},
  {"name":"recipes","scope":"global"},
  {"name":"modules","scope":"tag"},
  {"name":"hierarchy","scope":"tag"},
  {"name":"stats","scope":"tag"},
  {"name":"history","scope":"tag"}
]}

$ livehd list steps
{"schema_version":1,"pattern":"steps","items":["parse","cprop","cgen-verilog","check","lnast-tolg","lnast-fromlg","graphviz"],"aliases":{"parse":"parse.verilog","cprop":"pass.cprop","cgen-verilog":"emit.verilog","check":"check.equiv"}}

$ livehd list recipes
{"schema_version":1,"pattern":"recipes","items":["roundtrip","compile"]}

$ livehd list modules @tst1
{"schema_version":1,"pattern":"modules","tag":"@tst1","items":["foo","alu","regfile"]}

$ livehd list hierarchy @tst1
{"schema_version":1,"pattern":"hierarchy","tag":"@tst1","top":"foo","tree":{"name":"foo","children":[{"name":"alu"},{"name":"regfile"}]}}

$ livehd list hierarchy @tst1 --top alu
{"schema_version":1,"pattern":"hierarchy","tag":"@tst1","top":"alu","tree":{"name":"alu","children":[]}}

$ livehd list modules @tst1 --from original
{"schema_version":1,"pattern":"modules","tag":"@tst1","source":"input:original","items":["foo","alu","regfile"]}

$ livehd list partitions @tst1
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
$ livehd list
{"schema_version":1,"patterns":[
  {"name":"steps","scope":"global"},
  {"name":"recipes","scope":"global"},
  {"name":"modules","scope":"tag"},
  {"name":"hierarchy","scope":"tag"},
  {"name":"stats","scope":"tag"},
  {"name":"history","scope":"tag"}
]}

$ livehd list steps
{"schema_version":1,"pattern":"steps","items":["parse","cprop","cgen-verilog","check","lnast-tolg","lnast-fromlg","graphviz"],"aliases":{"parse":"parse.verilog","cprop":"pass.cprop","cgen-verilog":"emit.verilog","check":"check.equiv"}}

$ livehd list recipes
{"schema_version":1,"pattern":"recipes","items":["roundtrip","compile"]}
```

### Step 2: describe

```bash
$ livehd describe parse
{"schema_version":1,"name":"parse","canonical":"parse.verilog","description":"Parse Verilog/SystemVerilog into LGraph via Yosys","args":{"required":[{"name":"files","type":"path[]","repeatable":true},{"name":"top","type":"string"}],"optional":[{"name":"path","type":"path","default":"lgdb"}]},"inputs":["verilog"],"outputs":["lgdb"],"cache":"writes-tag-state","examples":["livehd run parse @tst1"]}

$ livehd describe cprop
{"schema_version":1,"name":"cprop","canonical":"pass.cprop","description":"Constant propagation and optimization pass","args":{"required":[],"optional":[{"name":"aggressive","type":"bool","default":false},{"name":"max_iterations","type":"integer","default":10}]},"inputs":["lgdb"],"outputs":["lgdb"],"cache":"mutates-tag-state","examples":["livehd run cprop @tst1 --set aggressive=true"]}

$ livehd describe recipe:roundtrip
{"schema_version":1,"name":"recipe:roundtrip","steps":["parse","cprop","cgen-verilog","check"],"description":"Full compile + equivalence check","supports_dry_run":true}

$ livehd describe modules
{"schema_version":1,"name":"modules","description":"List module names available from a tag","scope":"tag","selectors":{"optional":[{"name":"from","type":"string"}]},"outputs":["items"]}

$ livehd describe hierarchy
{"schema_version":1,"name":"hierarchy","description":"Return module/instance hierarchy top at top or --top","scope":"tag","selectors":{"optional":[{"name":"top","type":"string"},{"name":"from","type":"string"},{"name":"format","type":"enum","values":["tree","flat"],"default":"tree"}]},"outputs":["tree"]}

$ livehd describe partitions
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
{"schema_version":1,"seq":4,"tool":"livehd","step":"sim","status":"pass","tier":"hot-debug","elapsed_ms":200,"at":"…"}
{"schema_version":1,"seq":5,"tool":"livehd","step":"sim","status":"pass","tier":"hot-approx","warning":"checkpoint stale; not for LEC","elapsed_ms":300,"at":"…"}
{"schema_version":1,"seq":6,"tool":"livehd","step":"sim","status":"pass","tier":"cold","reason":"state_shape_hash changed","elapsed_ms":4100,"at":"…"}
```

### --help (human rendering of describe)

```
$ livehd run cprop --help
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
livehd setup @orig --top foo --files foo.v
livehd run recipe:compile @orig

# Experiment with different optimization
livehd setup @opt_exp --top foo --files foo.v --input baseline=@orig
livehd run parse @opt_exp
livehd run cprop @opt_exp --set aggressive=true
livehd run cgen-verilog @opt_exp

# Compare outputs
livehd run check @opt_exp --reference @orig
```

## Relationship to lgshell / EPRP

The new `livehd` CLI is a structured argv-based frontend. It may call the
existing EPRP/pass machinery internally, but agents should not need to quote or
generate `|>` command strings. The legacy `lgshell` REPL remains useful for
interactive humans and debugging.

The stateless kernel also replaces the `printf 'inou.prp files:… |> pass.upass
…' | lgshell` pattern that test and `*_compile.sh` scripts use today (e.g.
`pass/prp_writer/tests/prp_writer_roundtrip_test.sh`,
`inou/yosys/tests/yosys_compile.sh`): those pipe a `|>` string into stdin and
grep stdout for success/error markers. A hermetic `livehd compile … --emit …
--result-json …` invocation gives those scripts (and any Bazel rule that
wraps them) declared outputs and a parseable result instead of grepping mixed
stdout.

Implementation rule: the agent-facing `livehd` binary must not print existing
REPL banners, prompts, command echoes, or raw pass output on stdout in JSON or
JSONL mode. Current EPRP labels can seed `describe`, but the metadata needs to
grow beyond `required/default/help` to include typed arguments and artifacts.

## Relationship to Runner

LiveHD's CLI follows the same tag-based model as `hagent/runner/`. The
key difference: runner orchestrates external tools (build systems, simulators),
while livehd wraps internal compiler passes. Both can share a tag directory.

| Concept             | runner                        | livehd                        |
|---------------------|-------------------------------|-------------------------------|
| Tag creation        | `runner setup @tag`           | `livehd setup @tag`           |
| Step execution      | `runner run <api> @tag`       | `livehd run <step> @tag`      |
| Config file         | `runner.toml`                 | `livehd.toml`                 |
| Results file        | `runner_results.jsonl`        | `livehd_results.jsonl`        |
| Log naming          | `NNN_runner_<step>.log`       | `NNN_livehd_<step>.log`       |
| Tag sigil           | `@tag`                        | `@tag`                        |
| Discovery           | `runner list` / `describe`    | `livehd list` / `describe`    |
| Tag introspection   | not defined yet               | `livehd list <pattern> @tag`  |
| Test orchestration  | `runner run @tag` (parallel)  | `livehd run recipe:X @tag`    |
