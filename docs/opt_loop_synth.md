# todo_synth — Pyrope synthesis QoR + runtime optimization loop

**Status:** plan, not started. Authored 2026-08-17 after a read-only audit of
`pass/color`, `pass/abc`, `pass/opentimer` and the `../lhdsuite` bench harness.
Every number quoted below was read out of the source or measured on this box;
none is a target.

**Deliverable of this document:** a repeatable loop, not a fixed feature list.
Sections 4–6 are one-time setup (a trustworthy measurement contract); sections
7–8 are the loop that runs indefinitely afterwards.

---

## 0. Objective

Iterate on lhd's **Pyrope** synthesis path

```
lhd compile <top>.prp  →  lhd pass color synth  →  lhd pass abc  →  netlist
```

with two goals, in this priority order:

1. **Quality: improve delay**, holding area. (R1)
2. **Runtime: reduce synthesis wall time**, cold or incremental — either is a
   win. (R6)

The Verilog input path is *not* a tuning target, but must keep working
(section 2, cadence row).

---

## 1. Rulings (2026-08-17 scoping session)

| # | ruling |
|---|---|
| **R1** | **Delay is the primary quality number; area is the guardrail.** A change that lengthens the critical path is rejected even if it saves area. A change that shortens the critical path is accepted unless area inflates past the epsilon in §4.4. |
| **R2** | In bounds: the **abc layer** (recipe, `{D}`/`{L}`, mapper read-back, `abc_arith`), the **color layer** (partitioning, region shape), and the **front-end** (`upass`/cprop rewrites that reach the synthesized graph). **Constraint: every option change must be a SHARED default that works for all designs.** Per-design tuning (the `region_opts` / `Region_opts` channel) is explicitly future work and must not be used to win a benchmark. |
| **R3** | **Two delay oracles.** `qor.json` `total.max_delay` (ABC's per-region mapped-delay estimate) is the cheap per-iteration signal. A change only **lands** after `lhd pass opentimer` on the mapped netlist confirms the whole-design register-to-register critical path did not regress. |
| **R4** | XiangShan enters `lhdsuite` as **five separate `CORES` entries** — `xs_rob`, `xs_alu`, `xs_div`, `xs_exu`, `xs_backend` — over the shared `xiangshan/Backend/` source tree, using a new **synth-only core shape** (no `sim`/`verif`/`tests` filegroups). |
| **R5** | **LEC is not a per-change gate** — too slow. Color partitioning is not expected to affect correctness. Run LEC **occasionally, on dino only**. The per-change correctness gate is: completes, no segfault, no diagnostic, QoR reported. |
| **R6** | Runtime levers owned by this plan: **cold cost of color+abc** and **incremental abc-cache hit rate**. Front-end compile time only when the fix is synthesis-related. **Parallelism / threading is out of scope.** |
| **R7** | The ABC delay constraint `{D}` is **auto-derived per design by one shared rule** — the rule ships, the number is computed. No per-design `-D` constant. |

---

## 2. Targets and cadence

All designs live in `../lhdsuite`. Pyrope sources only (R2/§0).

| target | top | source | cadence | role |
|---|---|---|---|---|
| `dino` | `PipelinedDualIssueCPU` | `dino/pyrope/` | every iteration | small, fast, **the only LEC target** (R5) |
| `minion` | (see `CORES`) | `minion/pyrope/` | every iteration | multi-slot / clock-gated / deep-hierarchy stress |
| `xs_rob` | `Rob` | `xiangshan/Backend/pyrope/` | every iteration | sequential-heavy **scalability** probe (slow by design — that is the point) |
| `xs_alu` | `Alu` | ″ | every iteration | small combinational datapath; the most **delay-sensitive** signal in the set |
| `xs_div` | `DivUnit` | ″ | every iteration | arithmetic — **but see the `div_blackbox` trap, §10.T1** |
| `xs_exu` | `ExuBlock` | ″ | periodic (see below) | large-block robustness |
| `xs_backend` | `Backend` | ″ | periodic | largest block; robustness |
| verilog path | — | `*/verilog/` | periodic | must not break; not tuned |

**Periodic** = before any land, and at minimum weekly. `xs_exu` /
`xs_backend` are excluded from the routine loop for wall-clock reasons only —
they are **never** allowed to degrade:

> **Hard constraints (§4.5), enforced as test failures:**
> no segfault / abort / OOM-kill, and **≤ 30 minutes** end-to-end synthesis on
> `xs_exu` and `xs_backend`.

---

## 3. Environment: sky130 resolution (must be machine-portable)

The loop may run on a different machine, so the tech library must be
**discovered, never assumed**. Today `bench/common.sh:382` reads a
pre-exported `HAGENT_TECH_DIR` and `README.md:39` tells the user to
`find ~/.ciel`. Replace that with `ciel`-driven resolution.

Measured on this box:

```bash
$ ciel ls --pdk-family sky130            # non-tty => JSON, hashes only, NO dates
["e3262351fb1f5a3cc262ced1c76ebe3f2a5218fb", "e8daeda73ca8f5814dbc0b11d1d05802251a3750"]

$ ciel ls --pdk-family sky130            # tty-rendered => dates + enabled marker
├── e8daeda73ca8f5814dbc0b11d1d05802251a3750 (2026.06.11) (enabled)
└── e3262351fb1f5a3cc262ced1c76ebe3f2a5218fb (2025.10.15)

$ ciel output --pdk-family sky130        # the ENABLED version hash
e8daeda73ca8f5814dbc0b11d1d05802251a3750
$ ciel path --pdk-family sky130 <hash>   # hash -> directory
/Users/renau/.ciel/ciel/sky130/versions/<hash>
```

**Resolution rule (M0.2):**

1. Enumerate installed versions with `ciel ls --pdk-family sky130`.
2. Pick the **latest**. The JSON form carries no dates, so take the newest
   `(YYYY.MM.DD)` from the rendered listing, and cross-check against
   `ciel output --pdk-family sky130`.
3. **If the newest-by-date and the enabled version disagree, hard-fail** with
   both hashes and let a human decide. Never silently pick one.
4. `HAGENT_TECH_DIR = $(ciel path --pdk-family sky130 <hash>)/sky130A/libs.ref/sky130_fd_sc_hd/lib`,
   verified to contain `sky130_fd_sc_hd__tt_025C_1v80.lib`.
5. **Never fall back to a pre-set `HAGENT_TECH_DIR`** — a stale env var is
   exactly the failure this replaces (see below).
6. Record the chosen **version hash in every result JSON** (§4.4).

> ⚠ **Finding, blocks the baseline.** On this box `HAGENT_TECH_DIR` is currently
> exported to `…/versions/e3262351…` — the **2025.10.15** PDK — while the latest
> and `ciel`-enabled version is `e8daeda7…` (**2026.06.11**). The rule above
> will change the library under the flow. **All pre-existing QoR numbers are
> therefore not comparable**, and M1 must re-take the baseline from scratch on
> the resolved PDK. This is also why the version hash must be stamped into
> every result.

---

## 4. M0 — the measurement harness (one-time; do this before touching lhd)

Nothing in §7 is meaningful until every number below is trustworthy and
comparable across machines.

### M0.1 — five XiangShan cores, synth-only shape

`bench/defs.bzl` `CORES` entries assume a whole core: `sim_tb`, `verif`,
`tests/`, `lec_trust`, `verilator_tb`. The XS blocks have none of that.

- Add a **synth-only core shape**: a `CORES` key (e.g. `synth_only: True`) that
  makes `core_benches()` emit *only* the `compile_*`, `synth`,
  `synth_incremental` scenarios and skip `sim_*`, `lec*`, `verify*`,
  `synth_lec_*`. Per §3 of `AGENTS.md` the generator must keep working from the
  `CORE_*` env contract — no module names in `bench/*.sh`.
- Five entries: `xs_rob` / `xs_alu` / `xs_div` / `xs_exu` / `xs_backend`, each
  `top` = the block name, all pointing at one `//xiangshan/Backend` package
  exposing `pyrope`, `pyrope_top`, `verilog`, `verilog_filelist`.
- Tag `xs_exu` and `xs_backend` so the routine loop can exclude them
  (`--test_tag_filters=-slow`) while `bazel test //...` still runs them.
- **Resolved (2026-08-17):** `xiangshan/Backend/pyrope/` holds 1088 `.prp`
  files and each block is a different `--top` over the same tree, but
  **`lhd compile <top>.prp` prunes to the top's cone** — a block target pays
  only for its own cone, not for all 1088 files. Consequences:
  - the five `xs_*` entries can share one `//xiangshan/Backend` package with no
    per-block source lists; compile cost scales with block size, which is the
    behaviour the cadence in §2 assumes (Rob/Alu/DivUnit cones are subsets of
    the ExuBlock/Backend cones);
  - front-end compile time is **not** on the critical path for the 30-minute
    big-block budget — that budget is an abc/color problem. §7 W3 stays
    bounded to graph-shape fixes and is not a compile-speed stream.

### M0.2 — `ciel` PDK resolution

Implement §3 in `bench/common.sh`, update `README.md:39` and the
`FAIL: HAGENT_TECH_DIR …` message. Keep the bazel `--test_env` passthrough for
CI, but resolve rather than require.

### M0.3 — the opentimer gate (R3)

`lhd pass opentimer --top <top> lg:net <lib> [file.sdc]` already exists
(`lhd/lhd_kernel_passes.cpp:717`), emits `<workdir>/timing.json`, and has a
`hier` mode that structurally flattens before timing.

- **M0.3a — audit before trusting.** On dino: does `pass.opentimer` read the
  sky130-mapped netlist end to end, does `hier=true` flatten it, and does its
  single-region number agree with ABC's `qor.json` `max_delay` for a
  one-region design? Record the agreement (or the systematic offset).
- **M0.3b — wire it into `bench/synth.sh`** as a new METRIC
  `<alg>_sta_delay`, reported next to `<alg>_max_delay`. It runs on the same
  netlist `synth.sh` already emits (`lg:net_pass2`), so it costs one extra
  pass, not another synthesis.
- **M0.3c** — decide the gate cadence: STA on the routine set every iteration
  if it is cheap, otherwise on the land gate only. Measure in M0.3a.

### M0.4 — the result ledger

One append-only file (`../lhdsuite/bench/ledger.jsonl` or equivalent), one row
per measured configuration, holding at minimum:

```
{ "date", "host", "lhd_git_sha", "lhdsuite_git_sha", "pdk_version",
  "target", "config_id",
  "compile_ms", "color_ms", "abc_ms",
  "regions", "gates", "area", "max_delay", "sta_delay",
  "cache_hits", "cache_misses", "cache_hit_ms", "cache_miss_ms",
  "div_blackbox" }
```

Rules: a row without `pdk_version` + `host` is unusable (§3). Rows from
different hosts are never compared directly — only *deltas within one host*
are. `bazel run //bench:show` should grow a mode that diffs two `config_id`s.

The ledger is shared with `todo_incr.md`'s incremental loop (that plan's I10),
so **every row carries a `flow` field** (`"synth"` here, `"incr"` there). The
two loops run on the same box, the same PDK and the same lhd sha, and they
interact in both directions: a W2 recipe change moves the abc cache hit rate,
and a cache-key change alters which netlist is being scored. One ledger keeps
that visible.

### M0.4b — `current_opt_loop_synth.html`, the standing scoreboard

A JSONL ledger answers "what happened in run 47"; it does not answer "are we
ahead of where we started". Every iteration must therefore also refresh a single
rendered summary — **baseline vs current, per bench** — so the state of the loop
is one file away at any moment. This mirrors `current_opt_loop_incr.html` (see
`todo_incr.md` H6b); keep the two pages structurally identical so one glance
reads either.

> ### ⚠ The page is a LIVE STATUS DISPLAY, not an end-of-run report
>
> **Write it as soon as each result exists, and keep rewriting it as more
> arrive** — the moment a single measurement lands, stamp it into the ledger and
> re-render. Not at the end of the target, not at the end of the iteration. A
> synthesis sweep runs for tens of minutes per target; the value of rendering it
> at all is watching it fill in, so a wrong number is caught while the run that
> produced it is still on screen, and a crash partway does not discard
> everything measured before it.
>
> Same requirement, same mechanism as the sibling loop: a measurement driver
> publishes each row through `bench/ledger.py add` + `render` as it is produced
> (`../lhdsuite/bench/matrix.sh` `publish()` is the reference implementation),
> and a render failure is reported and then ignored so it cannot take down the
> expensive half of the run.
>
> Two corollaries:
> - **A partial page is the normal state.** Every cell must render honestly when
>   its measurement has not happened yet — `-`, never a blank that reads as
>   zero, and never a derived number (a QoR delta, an `area` guardrail verdict)
>   computed from a missing input.
> - **Keep fixing the page as results expose it.** Real rows will keep finding
>   renderer defects — a verdict pointing the wrong way on a metric nobody
>   classified, a `div_blackbox` row averaged into a "no regression" claim. Fix
>   those in the same sitting; a scoreboard that is wrong once is not trusted
>   again.

- **Path:** `current_opt_loop_synth.html`, at the livehd repo root.
- **Derived, never authored.** The ledger is the single source of truth; this is
  a pure rendering, reproducible by re-running the renderer over
  `../lhdsuite/bench/ledger.jsonl` filtered to `flow="synth"`. **Generated, not
  committed** — it is per-host by construction, so `.gitignore` it. Nothing may
  live only in the HTML.
- **One host per page.** Several hosts in the ledger render as several sections,
  never a cross-host diff. The header carries `host`, `lhd_git_sha`,
  `lhdsuite_git_sha`, **`pdk_version`** (§3 — a page without it is unusable),
  the baseline `config_id` and date, and the current `config_id` and date.
- **One row per (target, metric)**, three cells — baseline / current / delta —
  and a verdict computed from the §M0.6 noise floor:

  | target | metric | baseline | current | Δ | verdict |
  |---|---|---|---|---|---|
  | xs_alu | `sta_delay` | 4.82 ns | 4.41 ns | −8.5% | ✅ better |
  | xs_alu | `area` | 1 240 µm² | 1 249 µm² | +0.7% | ➖ within guardrail |
  | minion | `max_delay` | 6.10 ns | 6.02 ns | −1.3% | ➖ within noise |
  | minion | `abc_ms` | 1 168 | 1 174 | +0.5% | ➖ within noise |

  (illustrative shape only — not measured, and not a target.)

- **The verdict column is the §4.4 gate, computed rather than eyeballed.** It
  must encode the rules that are easy to skip by hand:
  - `sta_delay` is the authority; `max_delay` is the cheap signal (R3);
  - **flag any row where `max_delay` and `sta_delay` move in opposite
    directions** — §4.4 rule 3 says stop and explain before landing, and a
    partitioning change (W1) is the most likely cause (§10 T2);
  - `area` regressing past **1%** is a guardrail breach, not a note;
  - a `div_blackbox=true` row renders its QoR cells as **invalid**, never as
    numbers (§10 T1) — do not let a partial score average into a
    "no regression" claim.
- **Also carry** the derived `{D}` in force (§6, so a QoR delta can be
  attributed to it), the §2 hard-constraint status for `xs_exu` / `xs_backend`
  (completed / time / no OOM), and the correctness gate (R5).
- **Refreshed continuously while measuring** (see the banner above), and again
  at step 5 of §8, on land *and* on revert, so a measured negative
  result is visible and the same idea is not retried silently.
- **The baseline column does not move** with each land: it stays the §M1
  baseline for this host, so the page answers "how far have we come since the
  loop started". Re-baseline only on a host, PDK (§3, T6) or toolchain change,
  and say so in the header when it happens.
- **Keep it plain.** Static table, no external assets, readable in a terminal
  browser.

### M0.5 — the robustness gate

A `xs_exu` / `xs_backend` target fails on: non-zero exit, signal death, ABC
memory-admission refusal, or exceeding the 30-minute budget. The budget is a
metric too, so the trend is visible long before it trips.

### M0.6 — noise floor

Reuse `best_run` from `bench/common.sh`. Establish, per target, the run-to-run
spread of `abc_ms` and `max_delay` on an unchanged tree; that spread defines
the epsilon in §4.4 (below), and any "win" inside it is not a win.

---

### §4.4 — the accept/reject rule (R1 + R3)

A change **lands** only if, on **every** routine target (§2):

1. `sta_delay` does not regress beyond the §M0.6 noise floor — **and** on at
   least one target it improves (or the change is runtime-only, §4.4b);
2. `area` does not regress by more than **1%** (guardrail);
3. `max_delay` and `sta_delay` move in the same direction — if they disagree,
   **stop and explain the disagreement before landing** (a divergence means one
   oracle is being gamed, most likely by re-partitioning; see §10.T2);
4. the correctness gate (R5) passes: completes, no segfault, no diagnostic,
   QoR reported;
5. the §2 hard constraints hold on `xs_exu` / `xs_backend`.

**4.4b — runtime-only changes** (no QoR intent) must show *zero* movement in
`sta_delay`/`area` outside the noise floor, plus a real time win. A runtime
change that also moves QoR is a QoR change and takes the full gate.

---

## 5. M1 — baseline

With M0 done, record a full ledger baseline for every target in §2, at the
**current shipped defaults**, on the **resolved** PDK. Read out of the source
today, for the record:

| knob | default | source |
|---|---|---|
| `pass.abc.flow` (comb) | `strash; &get -n; &fraig -x; &put; &get -n; &dch -f; &nf {D}; &put` | `abc_map.cpp:61` |
| `pass.abc.flow` (seq) | same string | `abc_map.cpp:75` |
| `pass.abc.delay` `{D}` | **empty — i.e. `&nf` runs UNCONSTRAINED** | `pass_abc.cpp:70` |
| `pass.abc.load` `{L}` | empty | `pass_abc.cpp:71` |
| `pass.abc.adder` | **`rca`** (ripple-carry) | `pass_abc.cpp:73` |
| `pass.abc.block_size` | `0` (auto) | `pass_abc.cpp:74` |
| `pass.abc.multiplier` | `array` (only kind implemented) | `abc_map.hpp:44` |
| `pass.abc.register` / `memory` | `true` / `false` | `pass_abc.cpp:59,63` |
| `pass.color.synth_alg` | `synth` | `pass_color.cpp:56` |
| `pass.color.min_ge` | `1000` GE | `pass_color.cpp:61` |
| `pass.color.max_ge` | `30_000_000` GE (~1 GB ABC peak @ ~30 B/GE) | `pass_color.cpp:70` |
| `pass.color.absorb` | `true` | `pass_color.cpp:74` |
| `pass.color.name_weight` | `4` | `pass_color.cpp:79` |

Known starting point (minion, 3-pass incremental, pre-PDK-change):

```
                   compile   color      abc  hits  miss  remapped
pass1 (cold)          86ms    31ms   1168ms     0     9      916ms
pass2 (comment-only)  83ms    31ms    250ms     9     0        0ms
pass3 (one-line edit) 82ms    31ms    501ms     8     1      251ms
```

Two facts to carry into §7: **abc is ~90% of cold synthesis** (color is 2.6%),
and minion resolves to only **9 regions** — so region *shape*, not color
runtime, is how the color layer moves the abc number.

---

## 6. M2 — the auto-derived delay target (R7)

`{D}` is empty today, so `&nf` maps with no delay constraint. Under R1 that is
the wrong default: nothing in the flow is being told what to optimize for.

**The shared rule (one rule, per-design number):**

1. **Probe**: map the design once with `{D}` unconstrained; take the resulting
   critical path (per §M0.3, preferring the STA number over the per-region max).
2. **Constrain**: re-map with `{D}` = probe × a **single shared factor** (the
   one tunable constant this rule ships with; a factor < 1 asks for tighter
   timing than the unconstrained map achieved, trading area for delay).
3. Cache the probe result in the workdir so incremental runs do not repay it.

**Design constraints this rule must respect:**

- ⚠ **`{D}` is inside the incremental cache key.** `Mapper::resolve_recipe()`
  (`abc_map.cpp:177`) puts the substituted flow string in the recipe verbatim,
  and `abc_incr.cpp:197` gates reuse on an exact recipe match. A derived `{D}`
  that jitters by a hair between runs invalidates **every region, every run** —
  it would destroy exactly the incremental hit rate §7 W4 is trying to raise.
  The derived value **must be quantized** (e.g. to a fixed number of significant
  digits / a library-time-unit grid) and **must be stable across an unrelated
  source edit**. Prove this with a `pass2 (comment-only)` run that still gets
  100% hits.
- The probe doubles cold abc time in the naive form. Options to evaluate:
  probe once and persist per design in the workdir; probe on a cheap
  (`&nf` only, no `&dch`) flow; or derive the target structurally without a
  probe map. Pick after measuring — the 30-minute `xs_backend` budget is the
  binding constraint here.
- The probe must be reported, not hidden: the ledger row carries the derived
  `{D}` so a QoR delta can be attributed to it.

M2 is sequenced before §7 W2 because every later delay experiment is measured
against whatever `{D}` policy is in force.

---

## 7. Work streams (the candidate lever pool)

These are hypotheses to draw from, ranked by expected value. The loop (§8)
takes one at a time; the list is expected to grow as the ledger teaches.

### W1 — color: region shape (delay + cold runtime)

Region boundaries cap what ABC can optimize across: a critical path crossing a
color boundary is optimized in two independent pieces, and the boundary itself
is a hard cut point. Minion at 9 regions is a coarse partition of a whole core.

- **W1.1 Size-window sweep.** `min_ge` / `max_ge` set the region size band.
  Bigger regions = more ABC context = potentially better delay, but superlinear
  ABC time and memory (`max_ge`'s ~30 B/GE calibration comes from a flat XSCore
  run that reached 221 GB). Sweep the band; find the shared setting that is
  Pareto-best across all targets. This is the single highest-leverage color
  knob and the most likely to *also* move the 30-min big-block budget.
- **W1.2 Timing-aware partitioning.** Today the window is size-driven only.
  A boundary placed on the critical path is a wasted cut. Feed the STA/QoR
  critical path (`crit_output` / `crit_src` are already recorded per region in
  `Region_qor`) back into the next partition so cuts prefer slack-rich nets.
  Larger change; hold until W1.1 has shown how much size alone buys.
- **W1.3 `absorb` / `name_weight` interaction.** `absorb=true` structurally
  inlines sub-`min_ge` defs; `name_weight=4` binds tighter across anonymous
  crossings specifically to keep boundaries on names the incremental cache can
  reuse, and is documented "QoR-neutral at the default". Verify that claim — it
  is a W4 (cache) knob sitting inside a W1 (QoR) mechanism.
- **W1.4 `synth_alg=pipe` vs `synth`.** Measure; it is a one-flag experiment.

### W2 — abc: recipe and arithmetic (delay)

- **W2.1 `adder` architecture — the strongest single hypothesis in this plan.**
  The default is **`rca`**, a ripple-carry adder: the *worst possible* delay
  structure, shipped as the default under a delay-primary objective. `cska` and
  `cla` are already implemented (`abc_arith.hpp`, unit-tested without an ABC
  dependency). Sweep `adder` × `block_size` across all targets and pick a
  shared default. Expect area cost — that is what the §4.4 guardrail is for.
- **W2.2 Flow string.** Both comb and seq resolve to the same
  `strash; &get -n; &fraig -x; &put; &get -n; &dch -f; &nf {D}; &put`. Explore
  the standard ABC alternatives already aliased in (`resyn2`, `compress2rs`,
  … installed at `abc_map.cpp` `kAbcAliases`), `&if` vs `&nf`, and repeating
  `&dch`. Each candidate is one shared string; measure delay, area **and**
  abc_ms — this stream trades directly against W-runtime.
- **W2.3 `{L}` load constraint.** Unset today. Cheap to sweep alongside W2.1.
- **W2.4 Multiplier architecture.** `array` is the only `Mult_kind`
  implemented, and the header names the enum as the extension point for
  Booth / Wallace-tree. A Wallace tree is a large delay win on any
  multiply-bound path. Implementation work, not a knob — schedule only if the
  ledger shows a multiplier on a critical path.
- **W2.5 Sequential flow.** `seq_flow` is currently a copy of `comb_flow`;
  ABC's sequential commands (retiming, `&dc2` on the seq network) are unused.
  Highest potential on `xs_rob` (sequential-heavy). Higher risk — retiming
  moves registers, which interacts with R5 (LEC only on dino) and with state
  correspondence; if pursued, it needs a real LEC.

### W3 — front-end: upass/cprop (bounded, R6)

In scope **only when the fix is synthesis-related** — i.e. it changes the graph
that reaches color+abc. Not a general compile-speed project — and per M0.1,
compile prunes to the top's cone, so front-end *time* is not what threatens the
30-minute big-block budget either. This stream is about graph **shape**.

- **W3.1** Strength-reduce the div/mod cones that ABC blackboxes (§10.T1).
  This is both a QoR fix and the thing that makes `xs_div` measurable at all.
- **W3.2** Structural cleanups that shrink the graph handed to abc — but note
  §10.T3: front-end changes have the widest blast radius in this plan (sim and
  LEC see them too), so each one needs the full test suite, not just `synth`.

### W4 — incremental: abc cache hit rate (R6)

This plan owns the **abc slice**; `todo_incr.md` keeps LEC / formal / sim.

- **W4.1 Content-stable region keys (todo_incr F8).** The cache key is the
  region **name** `<top>__c<N>`, and color ids are "a deterministic function of
  the id allocation order" (`color_common.hpp:69`). An edit that renumbers
  colors misses regions that did not change. Fixing this is the difference
  between "one edit → one miss" and "one edit → many misses". Directly
  measurable in the existing `synth_incremental` pass3 row.
- **W4.2 Auto-salt (todo_incr F5).** `"abc-incr-v6"` is bumped by hand
  (`abc_incr.cpp:517`) and `Incr_cache::make_salt` does not hash `pass/abc`
  sources at all — a mapper change silently reuses stale netlists. LEC already
  does this correctly via a genrule source hash (`lhd/BUILD:17`); copy that
  mechanism. **This is a soundness fix, and it protects this whole plan**: every
  W2 experiment changes `pass/abc`, and a stale cache would silently report the
  *old* recipe's QoR as the new one's.
  → **W4.2 should be done early, before the W2 sweeps.**
- **W4.3 Recipe-key stability** — the M2 `{D}` quantization requirement (§6).

---

## 8. The iteration protocol

Each iteration:

1. **Pick one hypothesis** from §7. One at a time — a two-knob change cannot be
   attributed, and the ledger is the product.
2. **Check the shared-option constraint (R2).** If the change only helps by
   being tuned per design, it is out of scope; record it in the "future work"
   list at the bottom of the ledger and pick another.
3. **Measure** on the routine set: `dino`, `minion`, `xs_rob`, `xs_alu`,
   `xs_div`. Report the full §M0.4 row for each.
4. **Gate** against §4.4. Run `xs_exu` / `xs_backend` and the periodic
   dino LEC before landing.
5. **Land or revert**, and append the row either way — a measured *negative*
   result is worth as much as a positive one and stops the same idea being
   retried. Then **regenerate `current_opt_loop_synth.html`** (§M0.4b), on land
   *and* on revert, so the scoreboard never lags the ledger.
6. Every land bumps the ledger's `config_id`. **The baseline column does not
   move** — it stays the §M1 baseline for this host, so the page always answers
   "how far have we come since the loop started". Re-baseline only on a host,
   PDK (§3) or toolchain change.

**Periodic (weekly, and before any land):** `xs_exu`, `xs_backend`, the Verilog
input path, and `dino` LEC (`synth_lec_flat` / `synth_lec_synth`).

---

## 9. Explicitly out of scope

- **Parallelism / threading** (concurrent abc regions, parallel compile). Not
  selected; it is a wall-clock win with no QoR content and would mask
  algorithmic regressions in the runtime numbers.
- **Per-design option tuning** (`region_opts` / `Region_opts`). Future work.
- **Verilog input-path optimization.** Kept working, never tuned.
- **General front-end compile-time work** unrelated to the synthesized graph.
- **The shared incremental substrate** (`todo_incr.md` M0–M8) beyond the abc
  slice named in W4.
- **Making LEC scale to XiangShan.** R5 rules LEC to dino only.

---

## 10. Known traps

- **T1 — `div_blackbox` makes `xs_div`'s QoR a lie.** `Region_qor` documents:
  blackboxed div/mod cones are **not mapped**, so "gates/area/delay
  under-report — the score is partial until the div is strength-reduced away".
  `DivUnit` is the one block whose entire content is division. **Decide in M1**:
  either W3.1 lands first and makes `xs_div` measurable, or `xs_div` is demoted
  to a *runtime-and-robustness-only* target with its QoR columns marked
  invalid in the ledger. Do not silently average a partial score into a "no
  regression" claim.
- **T2 — `max_delay` is a MAX over per-region ABC estimates.** A path crossing
  a color boundary is never measured end to end, and the number moves when
  region count changes for reasons that are not timing. This is precisely why
  R3 requires the STA gate, and why §4.4 rule 3 stops a land when the two
  oracles disagree. **W1 (partitioning) is the stream most able to game
  `max_delay`** — never land a W1 change on the qor number alone.
- **T3 — front-end changes are not synthesis-local.** A W3 rewrite is seen by
  `lhd sim` and the LEC/formal flows too. Full suite, not just `//bench:*_synth`.
- **T4 — stale `bazel-bin` symlink.** `./bazel-bin` flips to the last-built
  `-c` config; benchmark numbers from a debug `lhd` are meaningless. The bench
  defaults to `-c opt`; when driving `lhd` by hand, confirm which binary is
  being run before believing a measurement.
- **T5 — `--override_module=livehd=../livehd`** is how `lhdsuite` tests a local
  checkout. Without it the bench silently measures the pinned upstream livehd,
  i.e. reports "no change" for every experiment.
- **T6 — the PDK swap (§3).** The first M1 baseline will differ from every
  number recorded before this plan, for library reasons alone. Do not read that
  delta as a regression.

---

## Appendix — reference command shape

The three-pass incremental pattern this plan measures (minion shown; every
target follows the same shape via `bench/synth.sh`):

```bash
lhd compile src/pyrope/<Top>.prp --top <Top> --emit-dir lg:lg_p1 --workdir cw_lg_p1
lhd pass color synth --top <Top>.<Top> lg:lg_p1 --workdir W
lhd pass abc  --top <Top>.<Top> lg:lg_p1 --emit-dir lg:net_pass1 --workdir W --result-json r_pass1.json
# pass2 = comment-only edit  (expect 100% cache hits)
# pass3 = one-line edit      (expect exactly the touched region to miss)
```

plus, new in M0.3:

```bash
lhd pass opentimer --top <Top>.<Top> lg:net_pass2 "$HAGENT_TECH_DIR/sky130_fd_sc_hd__tt_025C_1v80.lib" --workdir W
# => W/timing.json  -> METRIC <alg>_sta_delay
```
