# pass/satopt — bounded, proof-backed logic simplification

`pass/satopt` is one engine with independently selectable stages
(todo/livehd/2s-satopt). It simplifies an LGraph with rewrites that are each
proven, commits them to the graph, and reports what each stage cost and did.
It runs in exactly two places, both with the shared profile:

| Caller | Stages |
|---|---|
| the compile graph pipeline (after cprop/bitwidth, before pass.formal) | `pass.satopt.stages` (default: every stage) |
| `lhd pass satopt lg:DIR --emit-dir lg:OUT` | same |

`pass.satopt` is the single public enable switch for the compile step. It is
off by default, and on by default only when `lhd synth` or `lhd lec` compiles a
Pyrope/Verilog SOURCE: `lhd synth foo.prp` is `lhd compile --set
pass.satopt=true foo.prp` followed by mapping. An lg:/ln: input (`lhd synth
lg:...`, an lg: LEC side, `lhd pass abc|usyn`) is taken as compiled: no satopt
unless `--set pass.satopt=true`. An explicit `--set pass.satopt=true|false` or
configuration setting always wins. The mappers never run satopt themselves;
mapper-local `pass.abc.satopt`/`pass.usyn.satopt` options are retired.

It calls no ABC. `pass/abc` registers its `&fraig` mux-fact prover
(`abc_satopt.cpp`) at static initialization; an ABC-free build skips mux-arm
bit facts but still runs Hotmux exclusivity/collapse. Every other proof uses
cvc5 through `pass/formal`'s `Prover`.

## Stages

Stages always run in this order; the set only selects which run
(`--set pass.satopt.stages=none|default|all|a,b,...`, one spelling for every
caller; an unknown or repeated name is a usage error). Every stage runs by
default; an explicit `pass.satopt.stages` list replaces that.

| Stage | What it proves and rewrites | Default |
|---|---|---|
| `constants` | a Mux select, Hotmux control or one-bit Flop enable always 0/1 (tied off, dead arms zeroed); an operation output that is one constant (replaced); an unsigned output whose top bits are always 0 (its consumers read the narrower slice); any output whose low k bits are one constant C (its consumers read `Or(And(t, -2^k), C)`, the low-lane form cprop narrows every reading operation through, pass/cprop/cprop_lowlane.cpp) | on |
| `equiv` | an operation output always equal to a value earlier in topological order, same width and sign (consumers rewired) | on |
| `complement` | an output always the complement of an earlier value, when its private cone is larger than the `Not`/mask `Xor` that replaces it | on |
| `odc` | an output none of whose fanout-window exits (one or two levels, at most 16 cells) observes it: a constant, or only its low bits; proven over the window and applied one at a time | on |
| `hotmux` | per-bit mux-arm facts (an arm bit is 0/1/equal/complement to another arm's bit whenever selected; ABC `&fraig`), then Hotmux collapse: `unique if` controls proven exclusive globally are stamped proven and cprop's mux sharing absorbs them | on |
| `memory` | memory ports: synthesis merges/removes/narrows; shared only ties a write enable proven never active | on |
| `resub` | experimental: a one-bit output whose private cone has 2+ cells re-expressed as And/Or/Xor/And-not of two nearby one-bit values | on |
| `simp_ctrl` | derive and prove a Boolean function over up to three existing one-bit signals for mux selects and Flop/memory enables; commit only when it removes more cells than it adds | on |

Targets of the value stages are operation cells only (Sum, Mult, Div, And, Or,
Xor, Not, Ror, Rxor, Popcount, EQ, LT, GT, SHL, SRA, two-arm Mux, Hotmux);
wiring, constants, inputs and state or instance outputs may be the value a
target is replaced with, never a target. Other constant-bit runs are left
alone on purpose: rebuilding a value from lanes hid an affine shift amount
from the word-select ware and grew a 4:1 word select 4x.

`simp_ctrl` runs after `resub` and is independent of it. For example:

```
lhd compile design.v --set pass.satopt=true --set pass.satopt.stages=simp_ctrl
```

An explicit stage list replaces the default list, e.g.
`pass.satopt.stages=constants,hotmux,memory` for the audited core only.
The new stage only rewrites unsigned one-bit controls whose readers are all
Mux/Hotmux select pins, Flop enables, or memory port/bulk-update enables.
It excludes data readers, wide selectors and multi-bit write masks. Comparisons
and reductions can supply one-bit inputs; their datapaths are not resynthesized.

Simulation proposes an arbitrary truth table (not a fixed gate template).
Conflicting output samples for the same candidate inputs reject that support.
Missing rows propose zero, then a SAT counterexample fills them or disproves
that support. A proven table becomes a small shared And/Or/Xor/Mux expression.
Every rewrite is unconditionally equivalent, including Hotmux controls; the
stage neither assumes nor discharges unique-if obligations and needs no ODC.

The search is bounded to 12 candidate signals, four upstream Boolean levels,
up to three replacement inputs, 320 support attempts, 16 proof queries per
control and eight refinements per support. Candidate neighbors must depend only
on already collected upstream values. The removable-cone estimate stops at 48
Boolean cells; proofs additionally cap the cone at 512 pins. The shared work,
query and solver-resource budgets still apply. Solver state is allocated only
after simulation and gain filters accept a candidate. This stage currently
reruns its bounded search rather than persisting proof-cache rows.

## Profiles

`Profile::shared` keeps every observable check: every graph output, state
update, memory behavior, opaque interface, `unique if` exclusivity obligation
(a Hotmux that loses its reader is kept, its controls are never changed or read
in a replacement, and a rewrite that would leave every arm identical needs the
controls proven exclusive first), property markers (Sub nodes), and the latch
contract (a latch Q stays a direct arm of its hold mux). `Profile::synthesis`
may ignore obligations and refine memory don't-cares.

## Simulation and proofs

`Word_sim` (`satopt_sim.hpp`) evaluates LGraph operations at word level over
columns of patterns: seven corners (0, all ones, 0101…, 1010…, signed min,
signed max, 1), then seeded random patterns (`pass.satopt.samples`, 64 by
default), then counterexamples. A leaf's pattern depends only on its instance
path, node, port and column. It only nominates: every rewrite is proven by
cvc5 (`formal::Prover`: `is_true/is_false`, `masked_const`, `masked_relation`,
`unchanged_under`, `are_exclusive`) or, for mux facts, by the registered bit
prover. A refuted query's model (`Prove_options::produce_model`) becomes a new
column, so later candidates it refutes need no query. Unknown proves nothing.

## Budget, cache, report

One deterministic budget bounds a run (`--set pass.satopt.<knob>`, also read by
synthesis): `work` (simulation values, walks, solver cone pins; default 2e8),
`queries` (solver calls; 2e5), `budget_k`/`cone_max` (per query), `samples`,
and `time_ms` (a wall-clock backstop, 0 = off, not deterministic). A stage may
spend an equal share of what is left among itself and the applicable stages
still waiting (what it leaves unspent flows to them), so a long stage list
cannot starve the late stages. Out of budget
a stage keeps what it proved and reports `exhausted`.

Other stages cache proofs under `<workdir>/satopt_cache` when `lhd.incremental` is on:
per definition, keyed by an exact source descriptor (nodes, edges, widths,
constants, Sub callees, IO declarations), the code salt (this package, the
synthesis salt, the cvc5 encoder, MODULE.bazel), the proof-relevant options
and every descended definition. A row whose search was cut short is never
reused as complete, and a reused row charges what its search cost, so cold and
warm runs leave the same budget to later work.

Each run reports per stage `state` (disabled / inapplicable / exhausted /
completed), `ms`, `work`, `candidates`, `sim_rejects`, `queries`, `proven`,
`refuted`, `unknown`, `reused`, `applied`, `bits`, `nodes_removed` and
`budget_skips`: the result JSON's `satopt` member for compile (and so for a
`lhd synth`/`lhd lec` from source) and `lhd pass satopt` (`--stats` prints one
line).

## Files

| File | Role |
|---|---|
| `satopt_stages.{hpp,cpp}` | stages, profiles, `Budget`/`Meter`, `Report`, the coordinator `run()` |
| `satopt_ctrl.{hpp,cpp}` | small-support, exact control resynthesis (`simp_ctrl`) |
| `satopt_sim.{hpp,cpp}` | `Word_sim`: patterns, models, signatures, window re-simulation |
| `satopt.{hpp,cpp}` | selector proofs, `Satopt_seeds`, the source key, synthesis dead-logic drop |
| `satopt_sweep.{hpp,cpp}` | value stages: constants/narrowing, equiv, complement, odc, resub |
| `satopt_mux.{hpp,cpp}` | mux-arm facts (bit prover callback) and Hotmux collapse |
| `satopt_memory.{hpp,cpp}` | memory-port proofs |
| `satopt_detail.hpp` | shared internals: proof cut, arms, `Arm_builder`, `Select_rewrite`, prover options |
| `pass_satopt.cpp` | the `pass.satopt` plugin (labels, cleanup, copy-out with callees, report file) |

## Tests

Unit: `satopt_stages_test`, `satopt_sim_test`, `satopt_test`, `satopt_sweep_test`,
`satopt_memory_test`, `satopt_ctrl_test`, and `//pass/abc:abc_satopt_test` (ABC mux facts). End to
end: `//lhd/tests:lhd_satopt_test` (compile opt-in and standalone LEC,
idempotence, stages, budgets, cache across edits, reports, synthesis from a
source vs an lg: input),
`lhd_formal_fail_test` (a failing `unique if` still fails after satopt), and
`prp-sim-satopt_committed`.
