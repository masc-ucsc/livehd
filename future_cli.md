# LiveHD Future CLI Design

## Goals

Design a CLI for LiveHD that is optimized for LLM coding agents as primary
users. Humans remain supported but are not the design priority.

Principles:

- One command = one step (no implicit multi-step pipelines)
- Structured JSON output on stdout (agents parse it directly)
- TOML config per tag (agents read and edit it)
- Explicit state via `@tag` references (no in-memory-only state)
- Self-documenting API (`list`, `describe`) so agents discover capabilities
- Raw logs separate from structured results

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
livehd setup @tst1 --force                           # destroy and recreate
livehd setup @tst1 --reuse                           # reuse if compatible
```

### run — execute a step or recipe

```bash
livehd run parse @tst1
livehd run cprop @tst1
livehd run cgen-verilog @tst1
livehd run check @tst1 --reference @tst2
livehd run recipe:roundtrip @tst1
livehd run recipe:roundtrip @tst1 --stop-after cprop
```

Step-specific options:

```bash
livehd run cprop @tst1 --set aggressive=true
livehd run cgen-verilog @tst1 --set odir=custom_out
```

### status — inspect a tag

```bash
livehd status @tst1
```

### list — discover available commands

```bash
livehd list steps
livehd list recipes
```

### describe — get step details (JSON)

```bash
livehd describe parse
livehd describe cprop
livehd describe recipe:roundtrip
```

### help — human-readable (calls describe internally)

```bash
livehd run parse --help
livehd --help
```

## Tag Directory Structure

A tag directory is shared between runner and livehd. Each tool prefixes
its own logs and results to avoid collisions.

```
tst1/
  livehd.toml                      # livehd config — agent-editable
  runner.toml                      # runner config — agent-editable
  lgdb/                            # LGraph database (LiveHD internal state)
  out/                             # generated outputs (verilog, reports, etc.)
  logs/
    001_livehd_parse.log           # livehd step 1
    002_livehd_cprop.log           # livehd step 2
    001_runner_synth.log           # runner step 1
    003_livehd_cgen-verilog.log    # livehd step 3
    002_runner_sim.log             # runner step 2
  livehd_results.jsonl             # livehd structured results
  runner_results.jsonl             # runner structured results
```

### Log file naming

Files under `logs/` use the pattern `NNN_<tool>_<step>.log`:

- Counter is per-tool (livehd and runner each maintain their own sequence)
- `ls logs/ | sort` gives a useful view; tool prefix disambiguates origin
- Running the same step twice increments the counter: `002_livehd_cprop.log`,
  `004_livehd_cprop.log`

## Config: livehd.toml

The tag config is TOML. It is created by `livehd setup` and can be edited
directly by agents or modified via `livehd setup @tag --set key=value`.

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

One JSON object per line, appended after each step. JSONL format:
append-only, crash-safe, agents read individual lines.

### Successful step

```json
{"seq":1,"tool":"livehd","step":"parse","status":"pass","elapsed":1.2,"args":{"files":["foo.v"],"top":"foo"},"log":"logs/001_livehd_parse.log","outputs":["lgdb"],"at":"2026-04-09T10:00:00Z"}
```

### Failed step

```json
{"seq":3,"tool":"livehd","step":"cgen-verilog","status":"fail","elapsed":0.3,"error":"unknown node type Flop_async","error_class":"internal","tail":["cgen_verilog.cpp:142: unknown node type Flop_async","  in module: foo"],"log":"logs/003_livehd_cgen-verilog.log","at":"2026-04-09T10:00:06Z"}
```

### Error classification

Every failure includes `error_class`:

| error_class   | Meaning                                       |
|---------------|-----------------------------------------------|
| `syntax`      | Input file has syntax errors                   |
| `internal`    | LiveHD bug or unhandled case                   |
| `equiv_fail`  | Equivalence check failed                       |
| `signal`      | Process killed by signal (segfault, abort)     |
| `timeout`     | Step exceeded time limit                       |
| `missing_file`| Required file or path does not exist           |
| `config`      | Missing or invalid configuration               |

### Failure diagnostic block

On failure, the JSONL entry includes:

- `error` — one-line summary
- `error_class` — machine-parseable classification
- `tail` — last 10 lines of stderr (immediate context)
- `hint` — actionable suggestion
- `log` — path to full raw log
- `repro` — exact command to reproduce

```json
{"seq":2,"tool":"livehd","step":"parse","status":"fail","elapsed":0.1,"error":"file not found: missing.v","error_class":"missing_file","hint":"check files list in livehd.toml","tail":["Error: cannot open missing.v: No such file or directory"],"log":"logs/002_livehd_parse.log","repro":"livehd run parse @tst1","at":"2026-04-09T10:00:01Z"}
```

## CLI stdout

`livehd run` prints the JSONL line(s) to stdout. For single steps, one
line. For recipes, one line per step as it completes (streaming):

```bash
$ livehd run recipe:roundtrip @tst1
{"seq":1,"tool":"livehd","step":"parse","status":"pass","elapsed":1.2,"log":"logs/001_livehd_parse.log"}
{"seq":2,"tool":"livehd","step":"cprop","status":"pass","elapsed":2.1,"log":"logs/002_livehd_cprop.log"}
{"seq":3,"tool":"livehd","step":"cgen-verilog","status":"fail","elapsed":0.3,"error":"...","log":"logs/003_livehd_cgen-verilog.log"}
```

`--verbose` interleaves raw output for human debugging but still emits
JSONL lines.

## Discovery Protocol

Agents use two commands to discover capabilities:

### Step 1: list

```bash
$ livehd list steps
{"steps":["parse","cprop","cgen-verilog","check","lnast-tolg","lnast-fromlg","graphviz"]}

$ livehd list recipes
{"recipes":["roundtrip","compile"]}
```

### Step 2: describe

```bash
$ livehd describe parse
{"name":"parse","description":"Parse Verilog/SystemVerilog into LGraph via Yosys","args":{"required":["files","top"],"optional":["path"]},"outputs":["lgdb"]}

$ livehd describe cprop
{"name":"cprop","description":"Constant propagation and optimization pass","args":{"required":[],"optional":["aggressive","max_iterations"]},"inputs":["lgdb"],"outputs":["lgdb"]}

$ livehd describe recipe:roundtrip
{"name":"recipe:roundtrip","steps":["parse","cprop","cgen-verilog","check"],"description":"Full compile + equivalence check"}
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
| Test orchestration  | `runner run @tag` (parallel)  | `livehd run recipe:X @tag`    |
