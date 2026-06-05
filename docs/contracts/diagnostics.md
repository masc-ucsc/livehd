# Diagnostics: unified compile error/warning surface

Status: design contract for **[[1z]]** (foundation) and **3f** (full-fidelity
finish). Companion to [`future_cli.md`](future_cli.md) (the JSONL transport and
step envelope), [`sourcemap.md`](sourcemap.md) (the source span this references),
and the **4h** compile-error test harness (golden-error files).

## 1. Goal

Today every pass reports problems by throwing a bare string and dying on the
first one:

- `Pass::error(msg)` → `throw Eprp::parser_error` (`pass/common/pass.hpp:39`);
  `Pass::warn(msg)` → `eprp.parser_warn`.
- `upass::error(fmt, …)` → `[[noreturn]]`, prints to stderr + throws
  `std::runtime_error` (`upass/core/upass_utils.hpp`); ~24 call sites.
- `inou/prp` uses `Pass::error` (~7 sites).

There is no severity beyond error/warn, no machine-parseable classification, no
source location, no way to collect more than one problem per run, and nothing an
agent can read structurally. This contract defines **one diagnostic record**,
**one process-global sink that serializes it as JSONL**, and **one renderer**
that turns the JSONL back into human text. Every pass — `inou/prp`, upass,
lgraph passes — emits through the same surface.

The machine stream is the source of truth; the human text is a *filter* over it.

### 1.1 The primary consumer is a coding agent

Diagnostics are designed to be *triaged by an agent*, not just read by a human.
That drives concrete choices, reflected throughout this doc:

- **Branch on identity, not prose.** A stable `code` (§3) lets the agent
  `switch` on the exact diagnostic without regexing `message`. Message wording
  can change freely; `code` cannot.
- **Triage fault at a glance.** `category` (§4) tells the agent *who is wrong*:
  fix the Pyrope source, pick a different construct (`unsupported`), or fix
  LiveHD itself (`internal`). The agent should never "fix the source" in
  response to an `internal` bug.
- **One run, all problems.** Collect-and-continue (§7) so the agent sees every
  error in a single compile, not one per edit→recompile loop.
- **Self-serve the fix.** `hint` is one actionable line; the optional `see`
  field points at the contract/spec section that defines the rule, so the agent
  can open the authority rather than guess.
- **Signal, not chatter.** Be sparing with `warning`; debug/`info` noise stays
  in logs, never in the diagnostic stream. Every emitted record should be
  something the agent can act on.

## 2. Two layers (do not conflate)

`future_cli.md` already defines `livehd_results.jsonl`: **one line per pipeline
step**, with a single summary `error` object on failure. That stays. This doc
adds a finer stream:

| Stream | Granularity | Owner | File |
|---|---|---|---|
| `livehd_results.jsonl` | one line per **step** | `future_cli.md` / **1y** | run dir |
| `diagnostics.jsonl` | one line per **diagnostic** | this doc / **1z** | `runs/<id>/logs/NNN_<step>_diagnostics.jsonl` |

The step result's `error` block becomes a **summary** of the step's diagnostics:
it carries the fatal diagnostic's `category`/`message`/`hint` (mapped to the
step-level `error.class`), plus a pointer and counts:

```json
"error":{"class":"syntax","message":"value -1 does not fit u8 [0,255]","hint":"…",
         "diagnostics":"logs/003_upass_diagnostics.jsonl",
         "diagnostics_count":{"error":1,"warning":2}}
```

A non-CLI run (unit test, legacy `lgshell` REPL one-liner) writes the same
`diagnostics.jsonl` records. Nothing about the record schema depends on the
CLI being present — under `lhd` the file path is the declared
`--emit diagnostics:PATH` slot (the kernel ignores the `LIVEHD_DIAG` env).

### 2.1 Output model (pre-CLI, env-controlled)

Until the CLI ([[1y]]) sets it explicitly, the sink reads one env var once,
`LIVEHD_DIAG`, which selects the channels:

| `LIVEHD_DIAG` | human (stderr) | JSON file | notes |
|---|---|---|---|
| *(unset)* / `both` | ✅ | `diag.jsonl` | **default — both channels on** |
| `jsonl` | — | `diag.jsonl` | machine-only (agents / CI) |
| `stderr` / `-` | ✅ | — | human-only |
| `off` / `none` | — | — | silent |
| `<path>` | ✅ | `<path>` | both, custom JSON file |

**Key behavior: by default a run prints to stderr AND writes `diag.jsonl`.** The
file is truncated once per process and flushed after every record, so a record
survives even when the process later aborts. The CLI replaces the cwd default
with the per-run path `runs/<id>/logs/NNN_<step>_diagnostics.jsonl`.

### 2.2 Human (stderr) format

The sink is the single human-output authority, in gcc/clang style — a
`<file>:<line>:<col>:` location segment after the `livehd:` program prefix:

```
livehd:<file>:<line>:<col>:error:<message>
livehd:<file>:<line>:<col>:help:<hint>
livehd:<file>:<line>:<col>:note:<secondary>
```

When the span is null the location segment is omitted (`livehd:error:<message>`,
like gcc's `prog: error:` for command-line errors). `error` = compilation cannot
proceed; `warning` = it proceeds but there is a likely style/correctness issue;
`note` = a secondary location; `help` = the hint line. The legacy `Elab_scanner::parser_error_int` / `parser_warn_int` no longer
print their own `file:line:col cat: msg` line — they route through
`sink().flush(...)`, so every thrown `Eprp::parser_error`/`parser_warn` produces
exactly one `livehd:` line (+ JSONL). Call sites with rich context
(`Prp2lnast::report_error`, `upass::error`, `eprp_var`) `stage()` a full record
(code/category/span) that the flush emits; generic throws get a generic record.
(In `-c dbg` an error still trips the `parser_error_int` `I(false)` developer
abort *after* reporting — that is a separate dev aid, not part of this surface.)

## 3. The diagnostic record (JSONL)

One JSON object per line, append-only, crash-safe (same discipline as
`future_cli.md §205`). Core envelope:

```json
{
  "schema_version": 1,
  "severity": "error",
  "code": "range-fit",
  "category": "type",
  "pass": "upass.attributes",
  "message": "value -1 does not fit unsigned type u8 [0,255]",
  "span": null,
  "hint": "use an explicit bit-select e#[0..=7] to force the bit pattern",
  "see": ["docs/contracts/typesystem_clean_plan.md#T4"],
  "notes": [],
  "seq": 7
}
```

| field | req | meaning |
|---|---|---|
| `schema_version` | ✓ | bumped only on incompatible change (mirrors `future_cli.md`) |
| `severity` | ✓ | `error` \| `warning` \| `note`. `note` is only valid inside `notes[]` |
| `code` | ✓ | **stable, greppable** diagnostic id (kebab-case, e.g. `range-fit`, `unknown-node`, `declare-twice`). Stable across message-wording changes — this is what **4h** golden files key on |
| `category` | ✓ | coarse bucket (§4); rolls up to the step-level `error.class` |
| `pass` | ✓ | originating stage, dotted (`inou.prp`, `upass.attributes`, `upass.bitwidth`, `pass.cgen_verilog`) |
| `message` | ✓ | one-line, human-readable, no trailing period, no location (location lives in `span`) |
| `span` | ✓ | source location object, or `null` when unknown (§5). **Always present as a key**, value may be `null` |
| `hint` | – | actionable suggestion (one line) |
| `see` | – | optional array of doc/spec pointers (e.g. `docs/contracts/typesystem_clean_plan.md#T4`) the agent can open to self-serve the fix |
| `notes` | – | array of secondary `{severity:"note", message, span}` (e.g. "declared here", "first written here") |
| `seq` | – | monotonic per run; CLI fills `run_id`/`invocation_id` when wrapping |

New optional fields may be added under the same `schema_version`; consumers
ignore unknown keys.

## 4. Category enum

Per-diagnostic `category` is a *compile-semantics* bucket (distinct from
`future_cli.md`'s step-level `error.class`, which mixes in process failures like
`timeout`/`signal`/`lock_timeout`):

| category | covers | agent action (who is wrong) |
|---|---|---|
| `syntax` | parse / grammar / malformed construct (tree-sitter, prp2lnast lowering) | fix the source |
| `name` | unresolved / duplicate / out-of-scope name, declare-once violations | fix the source |
| `type` | type mismatch, range-fit, sign, named-type semantics | fix the source |
| `bitwidth` | width contradiction, overflow without `wrap`/`sat` | fix the source (add `wrap`/`sat`/bit-select, or widen the type) |
| `unsupported` | known feature not yet implemented | **rewrite around it** — the construct is valid Pyrope but LiveHD can't lower it yet; do not "fix" the source to satisfy a missing feature |
| `internal` | LiveHD bug / unhandled case (should never be user-triggerable) | **fix LiveHD, not the source** — this is a compiler bug; reduce to a repro |

For *environmental* (non-compile) errors, `category` reuses `future_cli.md`'s
`error.class` vocabulary directly, so `category` is a superset of the compile
buckets above:

| category | covers | agent action |
|---|---|---|
| `missing_file` | input file / path not accessible | **fix the invocation** (`files:`/`path:` arg), not the source |
| `usage` | invalid CLI usage / unsupported argument | fix the command line |
| `config` | missing / invalid configuration | fix the config (`livehd.toml`) |

The compile buckets roll up to `error.class:"syntax"` for the step summary;
environmental categories already *are* `error.class` values and pass through.

Roll-up to the step-level `error.class` (`future_cli.md §257`): `syntax`→`syntax`,
`name`/`type`/`bitwidth`→`syntax` (input-is-wrong family), `unsupported`→
`unsupported`, `internal`→`internal`. Kept as a small table in one place.

## 5. Span — nullable until sourcemap ([[1f]])

`span` references `sourcemap.md`. It is the **only** field blocked on **1f**, so
it degrades gracefully and the whole surface ships before sourcemap:

- **No location available** (most upass / lgraph sites today): `span: null`.
- **Best-effort raw** (`inou/prp`, which still holds the tree-sitter `TSNode`):
  emit raw bytes without a sourcemap id —
  ```json
  "span":{"file":"foo.prp","start_byte":120,"end_byte":135}
  ```
- **Full fidelity** (after **1f**): the sourcemap id plus resolved line/col —
  ```json
  "span":{"source_id":201,"file_id":1,"file":"foo.prp",
          "start_byte":120,"end_byte":135,"start_line":12,"start_col":7,"end_line":12,"end_col":9}
  ```

The `Span` struct (§6) carries every field as optional; the renderer prints
whatever is present and omits the location block entirely when `span` is `null`.
**3f** is the entry that flips upass/lgraph sites from `null` to real ids once
**1f** threads `source_id`s through the IR.

## 6. In-code API

```cpp
namespace livehd::diag {

enum class Severity { error, warning, note };

struct Span {                       // all optional; default-constructed = unknown (null in JSONL)
  std::optional<uint32_t> source_id;   // sourcemap entry (post-1f)
  std::optional<uint32_t> file_id;
  std::string_view        file;        // pre-1f fallback
  std::optional<uint64_t> start_byte, end_byte;
  std::optional<uint32_t> start_line, start_col, end_line, end_col;  // resolved (post-1f)
  bool is_null() const;
};

struct Note { std::string message; Span span; };

struct Diagnostic {
  Severity         severity;
  std::string_view code;            // interned, stable
  std::string_view category;
  std::string_view pass;
  std::string      message;
  Span             span;
  std::string      hint;
  absl::InlinedVector<Note, 1> notes;
};

// Process-global, mirrors `Pass::eprp`. The CLI (1y = lhd) points it at the
// declared --emit diagnostics: path; tests point it at an in-memory buffer;
// the legacy lgshell REPL points it at stderr.
class Sink {
public:
  void   emit(Diagnostic&&);                  // serialize one JSONL line + accumulate
  bool   has_errors() const;
  size_t count(Severity) const;
  [[noreturn]] void fatal(Diagnostic&&);      // emit(error-severity) then throw to abort the step
};
extern Sink& sink();                          // the active sink

}  // namespace livehd::diag
```

**Migration keeps existing behavior.** `Pass::error` / `upass::error` become thin
wrappers that build a `Diagnostic{severity=error, category=internal, span={}}`
and call `sink().fatal(...)` — same throw-and-abort, but now the structured
record is captured. `Pass::warn` → `emit` with `severity=warning` (already
non-fatal). New code constructs a real `Diagnostic` with a proper `code`,
`category`, and `span`, and chooses `emit` (collect-and-continue) vs `fatal`.

## 7. Collect-and-continue vs fatal

Today the first error aborts. The sink supports both modes so a pass can report
*all* the problems in one run (far better for an agent loop):

- **Recoverable** diagnostics (most `type`/`range`/`name`/`bitwidth` checks where
  the pass can substitute a poison/unknown value and keep going) → `emit`. The
  step still fails at the end if `has_errors()`, but the agent sees every error
  at once.
- **Fatal** diagnostics (the pass genuinely cannot proceed — corrupt IR,
  `internal`) → `fatal` (emit + throw). Unchanged from today.

The step driver checks `has_errors()` after each pass and sets the step
`status:"fail"` + summary `error` block accordingly.

**Warning discipline.** A `warning` is something the agent should *act on* but
that does not block the build. If it is not actionable, it does not belong in
the stream — route it to the log. Debug/`info` output never enters
`diagnostics.jsonl`. This keeps the stream high-signal so an agent can treat
every record as a work item.

## 8. Human-readable renderer (the filter)

A standalone filter reads `diagnostics.jsonl` and prints clang/rust-style text.
It is decoupled — `livehd diag render < diagnostics.jsonl`, or the CLI's
`--format text` runs it inline. With a span:

```
error[range-fit]: value -1 does not fit unsigned type u8 [0,255]
  --> foo.prp:12:7
   |
12 |   x:u8 = -1
   |          ^^
   = hint: use an explicit bit-select e#[0..=7] to force the bit pattern
```

Without a span (pre-1f, or a `null`-span site) it degrades to one line and never
fabricates a location:

```
error[range-fit]: value -1 does not fit unsigned type u8 [0,255]
  = hint: use an explicit bit-select e#[0..=7] to force the bit pattern
```

The renderer needs the source file only to print the carat line; it reads the
file via `span.file` (pre-1f) or resolves `source_id` through the sourcemap
(post-1f). No file → no carat, message still renders.

## 9. Test hook (4h)

**4h** pins expected diagnostics to a test. Because `code` and `category` are
stable (and `span`, once **1f** lands, is deterministic), a golden-error file is
a projection of `diagnostics.jsonl` keyed on `(code, category, span)` —
**message text is not pinned** (it can be reworded without breaking goldens).
Until **1f**, goldens pin `(code, category)` and `span` is allowed to be `null`.
The in-memory sink mode (§2) is what the harness attaches to.

## 10. What lands in 1z vs 3f

**1z (now, no sourcemap dependency):**
1. `livehd::diag` — `Severity` / `Span` / `Diagnostic` / `Sink`, the JSONL
   serializer, the active-sink plumbing (mirroring `Pass::eprp`).
2. Route `Pass::error` / `Pass::warn` / `upass::error` through the sink (preserve
   throw-on-fatal). All ~30 sites keep working; spans are `null`.
3. `inou/prp` best-effort raw `TSNode` byte spans (§5).
4. The renderer/filter (§8) + `--format text` wiring stub.
5. Assign real `code`/`category` to the highest-value existing diagnostics first
   (range-fit, declare-once, unknown-node, unsupported-feature); the long tail
   can default to `category:internal` and migrate incrementally.

**3f (after 1f):** flip upass/lgraph spans from `null` to real `source_id`s;
full lgraph-pass coverage; line/col resolution in the renderer; complete CLI
roll-up (`diagnostics_count`, step-summary mapping).

## 11. Locked decisions (1z)

Resolved 2026-05-30, optimizing for the agent as primary consumer:

- **`code` registry — free-form, harvest later.** `code` is a free-form
  kebab-case `string_view` at the emit site (low friction to add a diagnostic).
  When **4h** needs a catalog, harvest the live codes into a generated table +
  uniqueness check; we do not pay the central-enum cost up front.
- **`unsupported` is an `error`.** Severity `error`, `category:unsupported`. An
  agent must not silently proceed past a feature LiveHD can't lower; making it
  fatal-but-distinctly-categorized is exactly the signal that says "rewrite
  around this, don't fix the source" (§4 triage).
- **One diagnostics file per step.** `runs/<id>/logs/NNN_<step>_diagnostics.jsonl`,
  matching `future_cli.md`'s log-per-step layout. No run-wide stream; the
  per-step `error` summary already cross-references the file. Revisit only if
  cross-step dedup becomes a real need.
- **Sink dedups within a step.** The sink suppresses a record whose
  `(code, span, message)` it has already emitted in the current step, so a
  fixpoint pass that re-derives the same problem each iteration reports it once.
  The agent sees a clean unique work list, not N copies.
