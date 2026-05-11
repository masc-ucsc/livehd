# LiveHD: An Agent-Native Substrate for Hardware Design, Debug, and Exploration

## Context

LiveHD is being reshaped around a single observation: modern hardware design
increasingly involves AI coding agents, but today's EDA flows were built for
human GUI workflows. The result is that even capable agents struggle with
hardware tasks — long compile–edit–simulate iterations, hours-long
regressions, opaque waveform formats, GUI-bound debugging, and tooling that
emits human-readable logs rather than machine-readable state.

The direction documented here repositions LiveHD as the substrate that makes
hardware design, debug, and exploration loops *agent-native by design*. The
focus areas are **simulation**, **what-if exploration**, and **tests** —
built on a unified checkpoint model and a small, deliberate set of Pyrope
grammar extensions.

This is a design document, not a feature list. It captures the shape of the
changes; concrete tasks live in issues and roadmap pages.

## Why agent-native, not agent-on-top

Layering coding agents on top of existing EDA tools amplifies their
weaknesses: each agent action triggers a full rebuild, signals carry
optimization-mangled names, logs are scraped for state, and waveforms must be
viewed in a GUI. Agents end up making slow, error-prone progress on tasks
engineers do in minutes.

Rebuilding the substrate inverts this. If the compiler preserves source
anchors, checkpoints let us replay from any point, probes can be injected
without rebuilding, and the verification language already describes the
design, then the same artifacts engineers produce are directly consumable by
agents — and vice versa. The two converge on a single workflow rather than
fighting through a translation layer.

## Design targets

Grouped by the capability they unlock.

### Cross-cutting

- **Source-preserving IR end-to-end.** LNAST and LGraph carry
  `file:line:column`, module, and pipeline-stage anchors through every
  transformation. Waveforms, tool errors, and localization scores all map
  back to agent-editable Pyrope source without best-effort heuristics.

### Iteration-length reduction (make each cycle of the loop cheap)

- **Unified checkpoint substrate.** A single API over Dromajo (architectural)
  and LiveSim (micro-architectural). Deterministic tail-from-checkpoint
  replay is the default debug mode, not a special case.
- **Hot probe / coverpoint injection at replay.** Read-only monitors and
  value extractors added to a running checkpoint **without recompilation**.
  This is the linchpin that makes interactive exploration economically
  viable.
- **What-if signal override engine.** First-class `poke(signal, value)` over
  checkpoints, with explicit safety semantics — combinational vs. sequential,
  fanout warnings, auto-escalation to full rebuild when overrides can't be
  honored.
- **Incremental everything.** Sub-second incremental elaborate, synth, and
  LEC on small edits. Full rebuilds become the exception.
- **Queryable trace store.** Indexed waveforms with signal slice, transition
  pattern, and value-distribution queries instead of `grep` on multi-GB
  dumps.

### Iteration-number reduction (need fewer cycles per fix)

- **Invariant detection as a code generator.** Differential pass/fail
  analysis emits *Pyrope source files* the agent reads, edits, and runs —
  not opaque suspect records over RPC. Agents are uniquely good at file
  editing; the workflow plays to that.
- **Agent-legible style.** Globally unique signal names, explicit widths
  and types, no implicit wires, deterministic naming through optimizations.
  Style is correctness infrastructure when the consumer is an LLM.
- **HLS-style typesystem with safe QoR knobs.** Pass-through annotations
  (latency, retiming, pipelining) that agents can tune freely, with LEC
  guaranteeing the functional contract.

### Verification gate (cross-cutting principle)

- **LEC-as-a-gate, always on.** Fast incremental logical equivalence
  checking between pre-edit and post-edit netlists, applied to every edit
  before it is accepted. Verification is not optional in any loop; LiveHD
  should make it cheap enough that nothing bypasses it.

## The Pyrope debug language extension

The verification surface in Pyrope (`test`, `assert`, `cover`, `waitfor`,
`step`, `peek`, `poke`, `past`, `eventually`, `rose`, `implies`,
`when`/`unless`) already expresses most of what an agent-driven debug
session needs. Two narrow grammar additions complete the picture.

### Grammar diff

```js
// test_statement gains an optional attribute list
test_statement: $ => seq(
  'test'
  , field('attributes', optseq('::', $.attribute_list))   // NEW
  , field('args', $.expression_list)
  , field('code', $.scope_statement)
)

// assert_statement gains an optional attribute list
assert_statement: $ => seq(
  optional('always')
  , choice('assert', 'cassert')
  , field('attributes', optseq('::', $.attribute_list))   // NEW
  , field('condition', $._expression)
  , field('msg', optional(seq(',', $._string_literal)))
  , $._semicolon
)
```

That is the entire syntactic delta. Both additions follow Pyrope's existing
`for::[…]` / `while::[…]` / `loop::[…]` modifier pattern.

Everything else parses today:

- `waitfor`, `peek`, `poke`, `step`, `sigref` are paren-free function-call
  statements under the current grammar.
- `past`, `eventually`, `rose`, `next`, `implies` are existing temporal
  constructs.
- `when` / `unless` gating already exists for assertions.

### What the attributes mean

The attribute list is recognized at runtime, not by the parser. New
attribute keys can be introduced without touching the grammar. The initial
vocabulary:

| attribute      | applies to        | meaning                                                          |
|----------------|-------------------|------------------------------------------------------------------|
| `ckpt="..."`   | `test`            | replay from this checkpoint instead of live elaboration          |
| `bench="..."`  | `test`            | bench identifier (testbench / stimulus context)                  |
| `suspect=N`    | `test`            | links the test to a ranked suspect; used by trajectories         |
| `score=...`    | `test` / `assert` | confidence score from the detector                               |
| `unproven`     | `assert`          | candidate invariant — reports evidence, does not abort           |
| `from="..."`   | `assert`          | source anchor (`file:line`) for the predicate                    |

### The find/probe pattern, in existing constructs

Two recurring concepts emerge in any debug session:

1. **Locator** — find a point in time matching a condition.
2. **Probe** — evaluate state at that point.

Both already exist in Pyrope:

- **Locator** = `waitfor(cond, max=N)` — search forward in the trace until
  `cond` holds or the bound is hit. `max=` makes search bounded and timeouts
  a first-class outcome.
- **Probe** = the statements after `waitfor` inside a `test` block —
  `peek`, `cover`, `assert`, `poke`, `step`.

Parallel sweep across runs is the existing `for bench in (…) { test … }`
pattern; Pyrope already specifies that tests run in parallel.

Locator composition (find a milestone, then search relative to it) is
sequential `waitfor`s in the same test.

## Directory layout

Debug artifacts live entirely outside the source tree. The original RTL in
`src/` is read-only to the debug loop; only the final committed fix touches
it.

```
project/
├── src/                          # original Pyrope — read-only to debug loop
│   └── alu.prp
└── runs/
    └── fail_142/                 # one directory per failing test
        ├── bench.info            # testbench id, seed, regressed commit
        ├── ckpt/
        │   ├── c0800.lsim
        │   ├── c1200.lsim
        │   └── c1238.lsim        # last checkpoint before failure
        ├── traces/
        │   ├── pass_t05.fst
        │   └── fail_t142.fst
        ├── v0/                   # detector's first emission
        │   ├── debug.prp
        │   ├── evidence.jsonl
        │   └── stdout.log
        ├── v1/                   # agent's first edit + run
        │   └── ...
        └── v2/ ...
```

Versioning is directory-based. After many iterations, `ls runs/fail_142` is
a complete record of the debug session — readable by humans and agents
alike.

## End-to-end example

A failing test `t142` on a small ALU. Differential analysis across 47
passing runs and the failing run produces ranked suspects.

### `runs/fail_142/v0/debug.prp` — detector output

This file is **fully valid Pyrope** and runs without any agent edit.

```pyrope
// runs/fail_142/v0/debug.prp -- AUTO-GENERATED
// detector=invdetect-0.3.1  gen=2026-05-10
// pass=[t01..t47]  fail=[t142]

const all_runs = ('t142', 't01', 't05', 't12', 't18', 't23', 't31', 't47')

// === auto: regenerable ===

// ─── suspect 1 / score 0.94 ─────────────────────────────────────
// flag_z==0 when state==EXEC held in 47/47 passing; broke at c1240 in t142
// anchor src/alu.prp:42:7
for bench in all_runs {
  test::[ckpt="../ckpt/{bench}.lsim", suspect=1, score=0.94]
    "s1 / {bench}" {

    const flag_z = sigref("alu.flag_z")
    const state  = sigref("state")

    waitfor(flag_z == 1 and state == EXEC, max=2000)

    cover peek("alu.opcode")
    cover peek("alu.rs1")
    cover peek("alu.rs2")
    cover past(flag_z)
    cover peek("alu.valid")

    assert::[unproven, from="src/alu.prp:42:7"] peek("alu.valid")
  }
}

// ─── suspect 2 / score 0.81 (related: s1) ───────────────────────
// alu.req ⇒ eventually[..3] alu.valid held in 47/47 passing
// anchor src/alu.prp:51:3
for bench in all_runs {
  test::[ckpt="../ckpt/{bench}.lsim", suspect=2, score=0.81]
    "s2 / {bench}" {
    waitfor(peek("alu.req"), max=2000)
    assert::[unproven] eventually[..3] peek("alu.valid")
    cover peek("alu.opcode")
  }
}

// ─── suspect 3 / score 0.62 (possible upstream of s1) ───────────
// counter <= 4 held in 47/47 passing; reached 5 at c1238 in t142
// anchor src/alu.prp:78:2
for bench in all_runs {
  test::[ckpt="../ckpt/{bench}.lsim", suspect=3, score=0.62]
    "s3 / {bench}" {
    const counter = sigref("counter")
    waitfor(counter > 4, max=2000)
    cover past(counter)
    cover peek("counter_inc")
    cover peek("counter_dec")
    cover peek("state")
    assert::[unproven] counter <= 4
  }
}

// === agent: preserved ===
```

### `evidence.jsonl` — populated after run

```jsonl
{"test":"s1 / t142","ckpt":"../ckpt/t142.lsim","waitfor":"found@1240",
 "covers":{"alu.opcode":"SUB","alu.rs1":"0x80","alu.rs2":"0x01"},
 "asserts":[{"unproven":true,"status":"FAIL"}]}
{"test":"s1 / t01","ckpt":"../ckpt/t01.lsim","waitfor":"timeout@2000"}
{"test":"s3 / t142","ckpt":"../ckpt/t142.lsim","waitfor":"found@1238",
 "covers":{"counter_inc":1,"counter_dec":0,"state":"EXEC"}}
```

### Agent's v1 — what-if exploration

The agent reads the evidence: s1 fired only in t142; s3 fired only in t142,
two cycles before s1. Hypothesis: s3 is upstream of s1. Test it:

```pyrope
// === agent: preserved ===

// hypothesis: clamping counter at 4 prevents the flag_z failure
test::[ckpt="../ckpt/c1200.lsim", bench="t142"]
  "s3 what-if clamp" {

  const counter = sigref("counter")
  const flag_z  = sigref("alu.flag_z")
  const state   = sigref("state")

  waitfor(counter == 4, max=2000)
  poke "counter", 4
  step 20

  assert::[unproven] (flag_z == 0) when (state == EXEC)
}
```

After running v1, the unproven assertion passes — clamping `counter`
eliminates the `flag_z` failure. The agent commits a fix to
`src/alu.prp:78` (the counter logic), and LEC validates it against the
pre-edit netlist before the change is accepted.

## What this workflow enables

Every step is a file operation — `view`, `str_replace`, `bash` — which is
exactly the surface coding agents are pretrained on. The same workflow is
used by humans: read `vN/debug.prp`, edit it, run it, look at evidence. No
GUI dependency, no specialized debug client.

- **Auto-generated artifacts are real, runnable Pyrope.** A reviewer or a
  new contributor can read any `debug.prp` and understand the hypotheses
  under investigation.
- **Trajectories are diffs.** Inspecting how a session progressed reduces
  to `git diff v0/debug.prp v3/debug.prp`. This is the data shape on which
  LLMs are pretrained.
- **What-if testing is one `poke` line.** No separate tool, no recompile,
  no waveform editor.
- **Cross-run differential analysis is one `for bench`.** Pyrope's native
  parallelism handles fan-out; results land in `evidence.jsonl` as a
  table.
- **The detector is a code generator, not a service.** Its output is text
  the agent (and the human) can edit.

## Hot-reload tiers

A Pyrope edit produces one of three reload outcomes. The CLI reports
the tier in JSONL so the agent knows whether the post-edit run is
verification-grade or exploration-only.

| Tier | Soundness | When it applies | State |
|---|---|---|---|
| `hot-debug` | Sound | Edit is debug-taint-only (added `puts`, `cassert`, `cover`, toggled `debug` attr) — proven by sticky-attr propagation (`attributes_spec.md §Phase 1`) | Checkpoint preserved; sim continues without re-init |
| `hot-approx` | **Unsound** — opt-in, loudly labelled | Edit changes comb logic; state shape unchanged. Loaded checkpoint was produced by *old* logic; new logic would not have produced it. | Checkpoint values reused; for "what does it look like now" exploration. Not for LEC. |
| `cold` | Sound | Anything that changes state shape, ports, partition kind, or `interface_hash` | Full restart from a known-good checkpoint or seed; upass cache keeps it fast |

Debug-tainted code paths execute during simulation runs (so
observability code emits values), are excluded from Verilog egress,
and are gated by an LGraph anti-pollution check that no
debug-tainted value flows into a non-debug node. The check is the
static guarantee that `hot-debug` is sound.

`hot-approx` exists because the next-most-common edit after "add a
print" is "tweak comb logic to see what happens" — useful for human
and agent exploration, but the result is *not* what a real run from
a fresh seed would produce. CLI must label every such run.

## Partition-keyed checkpoints

Checkpoints are partition-keyed via the `Partition::state_shape_hash`
(`architecture.md §3, §9`). The on-disk map is
`(partition_path, state_decl_name) → value`, agnostic of which
backend produced it. A checkpoint written by a `slop` fast-sim run
reloads into an `LGraph`-Verilog large-sim run and vice versa —
this is the "switch between simulation modes" property the substrate
exists to provide.

State falls into three classes:

- `Flop` / `Latch`: keyed by `(partition_path, reg_name)`.
- `Memory`: keyed by `(partition_path, mem_name)` with a `(addr, value)` map.
- `Fflop` valid + data: keyed pair.

Compatibility is checked at reload by comparing `state_shape_hash` per
partition. Mismatches refuse the reload with a clear error rather
than silently mis-mapping state.

## Source preservation — what the substrate guarantees

The "source-preserving IR end-to-end" claim resolves to two tiers
(see `architecture.md §6` and `TODO.md` "Source location (LOC)
propagation strategy"):

- **Mandatory LOC** on partitions, ports, regs, and memories. These
  survive every pass; CI asserts presence.
- **Source-map indirection** for internal wires, SSA temps, and
  synthesised cells. One canonical map built from Pyrope; internal
  names point at one or more source locations. Synthesised cells
  with no surface name fall back to the enclosing partition's anchor.

`sigref("alu.flag_z")` resolves through the partition tree: `alu` is a
partition path, `flag_z` is a port or named declaration. Hot-probe
injection at replay depends on this resolution being stable across
optimisation — which it is, because the names are declared at the
partition boundary.

## Open design questions

These remain open and will be settled as implementation proceeds:

1. **Auto-section regeneration semantics.** Sectioned file with
   `// === auto: regenerable ===` and `// === agent: preserved ===` markers
   is the leading option. Alternative is a `.gen.prp` baseline that agents
   copy from. Sectioned is friendlier; needs precise rules for handling
   agent edits *inside* the auto section.
2. **Inline evidence vs. sidecar only.** Current plan is both — inline
   comments for the agent's next `view`, JSONL for machine consumption. A
   single source of truth has to be picked for consistency.
3. **`waitfor` timeout policy.** Default `max=` and whether timeout aborts
   the body or skips remaining statements. Leaning toward skip +
   record-timeout, so probes don't blow up on `not_found`.
4. **`poke` re-replay semantics.** Forward-from-current-cycle (default)
   vs. re-replay-with-override-from-checkpoint (explicit form). Two modes
   are useful; the distinction needs explicit syntax.
5. **Bench registry.** Raw paths to `.lsim` files are fragile. A registry
   that resolves `bench="t05"` through a manifest is the preferred
   direction; format and lookup rules to be determined.
6. **Cross-checkpoint locator scope.** When a `waitfor` searches across
   multiple benches in a `for` loop, do the `peek` calls in the body need
   any qualification? Current answer: no — each iteration of the `for` is
   a separate `test` with its own implicit context. To be confirmed in
   practice.
7. **GC for `runs/` directories.** Versions accumulate. Policy for
   archiving or deleting old `vN/` directories — automatic, manual, or
   driven by attributes on the test — is undecided.

## Summary

LiveHD's direction can be stated in one sentence: **make the substrate
agent-legible, and the workflows that benefit humans and agents converge.**
Source-preserving IR, unified checkpoints, hot probe injection, what-if
overrides, incremental LEC, and a two-line Pyrope grammar extension
together turn hardware debug from a GUI-bound, human-only activity into a
substrate that coding agents and engineers use through the same interface.

The minimal Pyrope grammar additions (`test::[…]` and `assert::[…]`)
preserve the language's existing aesthetic — the same `::[attrs]` modifier
pattern as `for`, `while`, and `loop`. No new keywords, no new
sub-language, no separate debug DSL. The verification surface engineers
already write is the same surface agents read and edit.
