# todo_incr — the incremental compile loop (LEC, formal, synthesis, sim)

**Status:** loop, not started. The audit behind it was taken 2026-08-14 (read-only,
over the four flows plus the shared `--workdir` plumbing); findings below were
checked against the code and, where marked *reproduced*, against
`bazel-bin/lhd/lhd`. **Converted from a milestone plan into a repeatable loop on
2026-08-17**, with three XiangShan blocks added as targets.

> ### ⚠ Every number in this document is a PROXY, not a baseline
>
> All wall-clock figures quoted below were measured on **one machine** —
> `mascm1`, Apple M5 Max, 18 cores, 64 GB, arm64 Darwin 25.5.0, 2026-08-17 —
> against one lhd build and one lhdsuite checkout. They are here for
> **orientation and ratio** only: to say which phase dominates, roughly how long
> a target takes, and whether a future run is in the right order of magnitude.
>
> **They are not targets, not gates, and not a baseline.** The loop may run on a
> different machine, and it very likely will. Its **first action after the
> harness is standing up is to take its own baseline** (§9) on its own host and
> write it to the ledger with `host`, `lhd_git_sha`, `lhdsuite_git_sha` and
> `pdk_version` stamped (H6). Every §8 comparison is then a **delta within that
> one host's ledger**, never against a figure printed here.
>
> Two figures deserve extra suspicion even as proxies: the `pass.formal` win in
> F10 predates the PDK resolution change (T6) and came from a different box, and
> the minion three-pass table in §9 is explicitly pre-PDK-change. Treat both as
> "this phase is worth attacking", not "this is what you should get".
>
> **Paths:** every path here is relative to the **livehd repo root**, and every
> command is written to run from there; the bench suite is the sibling checkout
> `../lhdsuite`. No absolute paths appear in this plan, so nothing is tied to one
> user's home directory or one machine's layout.

**Harness status (measured 2026-08-17, after pulling `../lhdsuite` to
`68afbda`):** the synthesis side of the XS work has already landed there — five
`xs_*` cores with a `synth_only` core shape, `slow` tags and a
`CORE_SYNTH_BUDGET_S=1800` end-to-end budget, plus an `apply_synth_only_variant`
helper that synthesizes the `comment1`/`bug1` edits with no checked-in variant
files. What this plan adds on top is measured, not assumed, in §6.

**Deliverable of this document:** a repeatable loop, not a fixed feature list.
§6 is one-time setup (a trustworthy measurement contract); §7–§9 are the loop
that runs indefinitely afterwards. §3–§5 are the standing evidence and the shape
any storage lever must land in — they are inputs to the loop, not a schedule.

Its sibling is `docs/opt_loop_synth.md`, the **synthesis QoR** loop. Same shape,
same box, same ledger, different objective: that one optimizes the netlist, this
one optimizes the *rebuild*. Ownership is settled in **I10**.

---

## 0. Objective

Iterate on the cost of **recompiling a design after a small source edit**, across
every phase lhd runs:

```
lhd compile  →  pass.formal  →  pass color / pass abc  →  lhd sim  →  lhd lec
```

with two goals, in this priority order:

1. **Latency: reduce WARM wall time** — what an edit→result cycle costs when a
   `--workdir` is already populated. (Q1)
2. **Do not pay for it** in cold wall time, in **simulated cycles/s**, in cache
   size, or in soundness. (Q2)

The second half of Q2 is the constraint that shaped this revision: **making `lhd
sim` compile faster must not make the simulation itself slower.** A design is
compiled once and simulated for millions of cycles; trading throughput for build
time is a net loss disguised as a win. That is ruling **I3**.

---

## 1. Rulings (2026-08-17 scoping session)

| # | ruling |
|---|---|
| **I1** | **Warm wall time is the primary number; cold wall time is the guardrail.** A lever that halves the warm rebuild and adds 5% to the cold run is a win. One that halves the warm rebuild by doubling the cold run is not — measure both, always. |
| **I2** | **Soundness is not tradeable.** A wrong reuse is a miscompile, which is strictly worse than a slow build. Every warm result must be byte-identical (or provably equal) to the cold result on the same input, and a fingerprint collision must be harmless *by construction* — the fingerprint proposes, exact validation decides, over preserved inputs. |
| **I3** | **Simulated throughput is a hard guardrail.** `sim_exec_ms` / `sim_cycles_per_s` must not regress beyond the noise floor on any target. The cheap ways to cut `sim_cc_ms` — splitting translation units, dropping the optimization level, coarsening inlining across defs, per-def compilation — all cost cycles/s. A change that buys compile time with simulated throughput is **rejected even if net wall time improves**. The number this loop is allowed to move is `sim_cycles_per_s_with_cc`; the number it must hold is `sim_cycles_per_s`. |
| **I4** | **Hit COUNT is never the gate; TIME re-done is.** minion once hit 199 of 264 abc regions for a 1.0× speedup, because everything expensive was in the 65 that missed. Every incremental gate is expressed in milliseconds re-done, not in hits. |
| **I5** | **A `store-failed` is a hard fail; a principled refusal is reported, not hidden.** `reuse_eligible=false`, `canonical_digest.valid=false` and unsupported loop shapes are design decisions and must be counted separately from "the cache tried to snapshot this and could not", which is a livehd bug that recomputes forever. |
| **I6** | **The loop starts now.** Every milestone — including the shared substrate — is a **lever** drawn one at a time and measured, not a prerequisite phase. Ordering constraints live on the individual levers (§7), not on the calendar. The one exception is the harness (§6): nothing below is meaningful until the numbers are trustworthy. |
| **I7** | **One lever per iteration; the ledger is the product.** A two-change iteration cannot be attributed. A measured negative result is recorded and is worth as much as a positive one. |
| **I11** | **Numbers are per-host and per-baseline; nothing in this document is a target.** Every figure printed here is a proxy from one machine (see the banner above). The loop takes its own baseline on its own host before gating anything, stamps `host` / `lhd_git_sha` / `lhdsuite_git_sha` / `pdk_version` on every ledger row, and compares **only deltas within one host**. A cross-host or cross-baseline comparison is not evidence and must never appear in a land/revert decision. Re-baseline after any PDK change (T6), toolchain change, or move to a different box. |
| **I8** | **No per-design tuning.** Every lever must be a shared default that works on all targets. The only user action a warm build may require is naming a `--workdir`; per-design cache knobs are out of scope, exactly as per-design synthesis options are in the sibling loop. |
| **I9** | **XiangShan enters as three cores** — `xs_alu`, `xs_rob` (routine) and `xs_backend` (periodic) — over the shared `xiangshan/Backend/` tree. `xs_alu` and `xs_rob` exercise **compile, synth and sim**; `xs_backend` is **compile+synth until L11 lands**, because `lhd sim` cannot lower it at all today — a single residual fine-color cycle disables coarsening (F22). That is a lever to fix, not a permanent scope reduction. Each has a trivial in-house sim driver (§6 H2) so the I3 guardrail is measurable on the two that run. **LEC and formal incremental stay on dino/minion** — `opt_loop_synth` R5 already rules LEC to dino for scale reasons, and the XS blocks carry no verify sidecar. |
| **I10** | **One ledger, shared with `opt_loop_synth`; rows carry a `flow` field.** Both loops run on the same box, PDK and lhd sha, and they interact in both directions (an abc recipe change moves cache hit rate; a cache-key change changes which QoR number is being reported). Ownership split: the abc **key and salt soundness fixes** (F5/F8, i.e. that plan's W4.1/W4.2) are implemented **here** and consumed there; the abc **recipe/arithmetic/partition knobs** are theirs and never touched here. |

---

## 2. Targets and cadence

All designs live in `../lhdsuite`.

| target | top | source | phases exercised | cadence | role |
|---|---|---|---|---|---|
| `dino` | `PipelinedDualIssueCPU` | `dino/pyrope/` | compile, synth, sim, **lec**, **formal** | every iteration | small and fast; the full five-phase matrix |
| `minion` | `minion_top` | `minion/pyrope/` | compile, synth, sim, **lec**, **formal** | every iteration | deep hierarchy, clock gating; carries the **largest measured win** (F10) |
| `xs_alu` | `Alu` | `xiangshan/Backend/pyrope/` | compile, synth, sim | every iteration | small XS block; the cheap XS signal |
| `xs_rob` | `Rob` | ″ | compile, synth, sim | every iteration | **one 44.6 MB / 405k-line source unit** — the decisive datapoint for the L8 grain question |
| `xs_backend` | `Backend` | ″ | compile, synth (sim blocked on F22/L11) | periodic | largest block; scale and robustness |
| verilog path | — | `*/verilog/` | compile, sim | periodic | must keep working; not tuned |

**Periodic** = before any land, and at minimum weekly.

> **Hard constraints (§8 rule 6), enforced as test failures:**
> no segfault / abort / OOM-kill on any target, and **≤ 30 minutes** end-to-end
> for any single `xs_backend` incremental scenario (three passes).

`xs_div` and `xs_exu` from `opt_loop_synth` §2 are deliberately **not** here.
`xs_div` is a QoR probe whose value depends on the `div_blackbox` fix and adds
nothing to a rebuild-latency loop; `xs_exu` is redundant with `xs_backend` for
scale. Both remain available if the ledger later asks for them.

---

## 2b. Start here — the first four work items, in order

Read §1 (rulings) and §8 (the gate) first; they are short and everything else
assumes them. Then do these, in this order, because each unblocks the next.

**Step 1 — wire the generated DPI stubs into the build (F21).** The generator
already exists and is verified; what is missing is making the bench use it:

```bash
# from the livehd repo root — derives all 20 sink models from the wrappers
python3 ../lhdsuite/bench/gen_stubs.py \
        ../lhdsuite/xiangshan/Backend/pyrope \
        ../lhdsuite/xiangshan/Backend/synth_stubs
```

Then either commit that output once, or (preferred, keeps it generated rather
than checked in) turn it into a genrule feeding `xiangshan/Backend/BUILD`'s
`pyrope_stubs` / `pyrope_stub_top` filegroups. Done when
`bazel build //bench:xs_alu_synth` analyzes and

```bash
lhd compile ../lhdsuite/xiangshan/Backend/pyrope/Rob.prp --top Rob \
    ../lhdsuite/xiangshan/Backend/synth_stubs/*.prp \
    --emit-dir lg:scratch_lg --workdir scratch_wd
```

exits 0 — measured `pass — 0 errors, 14 warnings`. `xs_alu` already works
without stubs.

**Step 2 — make the XS cores emit the sim scenarios (H1).** Wire
`//xiangshan/Backend:sim` into `_lhd_bench`'s `data`, add the `sim_*` keys to the
three `CORES` entries, and switch the sim scenarios from the `synth_only`
allowlist to `needs_cfg="sim_tb"`. Done when `bazel test //bench:xs_alu_sim_pyrope`
passes with the checksum in H2.

**Step 3 — add the two missing scenarios (H4) and the ledger (H6).** Without
`sim_incremental` there is no I3 measurement, and without the ledger there is no
way to compare two iterations. Do not skip the noise floor: a guardrail with an
unmeasured epsilon cannot reject anything.

**Step 4 — take the baseline (§9), then draw L1.** L1 (`pass.formal` obligation
caching) is first because F10 measured the largest single win in the audit. L2
(auto-salt) must land before any lever that edits `pass/abc` or `inou/cgen`,
including the sibling plan's recipe sweeps.

Everything after that is §9's protocol: one lever, measure the routine set, gate
on §8, append the ledger row, land or revert.

---

## 3. What exists today

| component | store | key | compare | salt / version | enabled by |
|---|---|---|---|---|---|
| compile / elaborate | `<lg-out>` or `<wd>/lgdb`, mutable working/output library (**not a cache**) | def name | none | none | path persistence, no cache contract |
| `pass.formal` (in the O1 recipe) | **none** | — | — | — | never |
| LEC / `formal verify` | `<wd>/formal_cache.json` | 128-bit 2-lane Merkle canonical digest **pair** + options | digest only | **auto** `kFormalSrcSalt` (`lhd/BUILD:17` genrule over `pass/lec` + `pass/semdiff` + the cvc5 pin) | user `--workdir` **and** `lec.cache` |
| synthesis `pass.abc` | `<wd>/abc_cache/` + `abc_cache_pre/` (two GraphLibraries + json) | region **module name** `<top>__c<N>` + verbatim recipe | **exact structural compare vs the stored old graph**, traversal-bijection fallback | **manual** `"abc-incr-v6"` + liberty content + modes (`pass/abc/abc_incr.cpp:528`) | user `--workdir` **and** `pass.abc.cache` |
| sim `inou.cgen.sim` | `<odir>/gen_digests.json` | module name | **64-bit single-lane FNV**, traversal-order and name sensitive, **not hierarchical** (`inou/cgen/cgen_sim.cpp:1994`) | **manual** `kSimGenVersion = "simgen-62"` (`cgen_sim.cpp:2110`) | any `--emit-dir sim:` odir; `--workdir` only indirectly |
| sim host build | `<wd>/sim/build.ninja` | mtime + depfile | — | — | ninja on PATH |
| color kernel reuse (intra-run) | memory | 128-bit class hash | occurrence verify | — | always |
| `color_reduce` (intra-run) | memory | private 2-lane digest | exact walk | — | `pass.color reduce` |

Three of the four target rules already exist somewhere. No two components
implement them the same way, and `compile` implements none of them.

---

## 4. Findings

### Soundness

- **F2 — five independent digest implementations, three quality tiers.**
  `semdiff::canonical_digest` (`pass/semdiff/semdiff.hpp:287`: 128-bit, two
  lanes, canonical, Merkle) → `color_reduce` and the color-plan kernel hash
  (128-bit + exact verify) → `Cgen_sim::sim_graph_digest` (64-bit, ONE lane,
  order-dependent via the traversal-index `seq[]`, name-sensitive). A collision
  in the last one is a stale generated `.cpp`, i.e. a miscompile. Multiple
  flow-specific identities are legitimate; the defect is letting a weak
  fingerprint decide reuse without exact validation.

- **F3 — only abc keeps the old graph.** "Traverse old vs new to guarantee the
  same structure" is implemented once, in `pass/abc/abc_incr.cpp`
  (`structural_identical`, falling back to `structural_equivalent_traversal`).
  LEC and sim are digest-only; there is nothing to traverse.

- **F4 — the sim digest is not hierarchical.** A `Sub` folds only
  `cg->get_name()`. But the emitted PARENT code depends on child *subtree*
  properties: simgen-8 `__tick` clock-guard forwarding at call sites, simgen-7
  Moore/state-only classification walking through a Memory, simgen-6 state-only
  prebind. Some of those surface as parent graph IO and do invalidate — a
  stateful→stateless leaf edit moves the parent digest via the clock port
  (verified) — but that is incidental, not a guarantee.
  **FIXED 2026-08-17 (I-1):** `Cgen_sim::hier_graph_digest` folds each
  instantiated child's digest, memoized per `Gid`. This was not optional — it is
  the precondition for F11, since the color root's generated code is derived
  from an occurrence plan spanning the whole cone while its own body hash never
  moves for a leaf edit.

- **F5 — salt discipline splits.** LEC auto-invalidates from a source content
  hash. abc and sim need a human to bump `"abc-incr-v6"` / `"simgen-62"`.
  `Incr_cache::make_salt` does not hash `pass/abc` sources at all, so a mapper
  read-back change silently reuses stale netlists until someone remembers.

- **F8 — the abc cache key is not content-stable.** Boundary PORT names were
  deliberately made content-stable, but the key is the region name
  `<top>__c<N>`, and color ids are "a deterministic function of the id
  allocation order" (`pass/color/color_common.hpp:69`). An edit that renumbers
  colors misses every region. This is why the lhdsuite gate had to become "time
  re-mapped", not hit count.

### Staleness / hygiene

- **F6 — no workdir ownership scopes or GC. *Reproduced*:** a def
  renamed between two runs into the same `lg:` dir survives as a ghost and is
  emitted downstream.

  ```
  run1: alpha, topg   → GL/  (2 graphs)
  run2: beta,  topg   → GL/  (3 graphs)     # alpha no longer exists in the source
  lhd compile lg:GL --emit verilog:out.v → module alpha, module beta, module topg
  ```

  `compute_run_id()` (`lhd/lhd_result.cpp:400`) already computes a deterministic
  content hash over tool version + command + resolved options + input contents,
  but that makes it an invocation/result identity: it changes across edits,
  tops and flows and therefore cannot identify a reusable workdir namespace.

- **F7 — the sim emit dir is never swept. *Reproduced*:** renaming `leaf`→`leaf2`
  leaves `h3.leaf.cpp/.hpp/.iface.json` on disk and a permanent `"h3.leaf"` row
  in `gen_digests.json`. `BUILD`/`manifest.json` are rebuilt from `var.graphs`
  and stay clean — **except** `color_aux_sources`, which is a
  `directory_iterator` scan for `.color-kernel-*.cpp`
  (`lhd/lhd_kernel_common.cpp:1570`), so stale kernel files from a previous run
  DO get swept into the generated BUILD.

  Both defects also **corrupt this loop's own measurements**: a ghost def is
  extra work in every downstream number, so it inflates cold and warm alike.

### Coverage / cost

- **F1 — the `--workdir` rule is not uniform.** abc and lec require a
  *user-named* `--workdir` (a scratch dir would start cold every run). Sim keys
  off the emit dir instead, so `lhd compile --emit-dir sim:DIR` is incremental
  with no `--workdir` at all. Compile reopens its mutable working/output
  GraphLibrary when that path persists, but exposes no incremental cache
  contract or ownership boundary for it.

- **F9 — no intra-run reuse in abc.** `lookup_compare` is keyed by module name
  only, so two structurally identical regions each pay ABC in the same run. And
  `flatten` (default `auto`, on whenever the coloring is `flat`) inlines the
  hierarchy, destroying cross-instance sharing entirely. Sim is already correct
  here: one `.cpp` per def, plus occurrence kernel reuse in the color plan.

- **F10 — nothing caches the front end.** LEC re-elaborates BOTH sides from
  source on every run (visible in the emitted recipe); sim and synth recompile
  the design every run. And `pass.formal mode:fast` re-proves every graph in the
  library on every compile — lhdsuite measured minion `intpipe_csr_file`
  32.9s → 0.67s with it off, whole design 43.4s → 5.1s (**proxy, older box,
  pre-PDK-change — a ~50× and ~8× RATIO, not a number to reproduce**, I11). The
  verdict cache exists and `pass/formal/pass_formal.cpp` never consults it.

  **Re-measured 2026-08-17 on `mascm1` and it does NOT generalize (§9b I-0):**
  on `xs_rob`, `lhd compile` costs 86,293 ms with `pass.formal mode:fast` and
  86,846 ms with `mode:none` — the same number. So F10 is a minion-shaped
  finding, not a general one: minion's `intpipe_csr_file` carries obligations
  that are expensive to discharge, and XiangShan's do not. L1 keeps its place in
  the pool but no longer leads it; confirm the ratio on minion, on the loop's
  own host, before drawing it. (For the record, `pass.formal` is not in the
  `lhd sim` recipe at all — it is a `lhd compile` cost only.)

- **F11 — the color root is excluded from sim reuse** (`!color_root &&` in the
  gate, `inou/cgen/cgen_sim.cpp:~2590`), yet its digest is still stored and
  never read. **FIXED 2026-08-17 — see the iteration log, I-1.** It was the
  single largest sim-side cost: the root owns the evaluator and state-commit
  shards, every canonical kernel TU and, under the LLVM backend, one LLVM
  codegen per color.

- **F12 — docs disagree with code.** `pass.abc.cache`'s help
  (`pass/abc/pass_abc.cpp:90`) says "content-addressed by a canonical region
  digest"; `abc_incr.hpp` says the opposite ("keyed by module name … instead of
  a 128-bit digest"). There is no single description of what `--workdir` means;
  each command's help describes it differently.

- **F13 — test coverage.** Unit level is decent: `lhd/tests/lhd_abc_incr_test.sh`,
  `lhd/tests/lhd_sim_incremental_test.sh`, `pass/lec/tests/lec_cache_test.sh`,
  `lhd/tests/lhd_formal_cache_split_test.sh`, `pass/abc/abc_incr_test.cpp`.
  Design level in `../lhdsuite` has `lec_incremental`, `synth_incremental`,
  `verify_incremental` on the three-pass cold/comment/edit pattern — but **no
  `sim_incremental`, no `compile_incremental`**, nothing exercising one shared
  `--workdir` across targets, and nothing measuring replica / sub-loop reuse.

- **F14 — persistence is not a transaction.** An atomic rename of one JSON index
  does not atomically publish its GraphLibraries and generated files. A crash can
  leave a record that names a missing or half-written artifact. Concurrent
  processes can also overwrite one another's read-modify-write index. Every
  corrupt/incomplete record must fail cold, but preventing the inconsistent
  publication is the better contract.

- **F15 — one structural identity is not enough.** LEC's canonical digest is
  intentionally based on IO names, state identity and proof semantics. abc
  compares a PRE-map graph but restores a distinct MAPPED graph. Sim output also
  depends on internal names, generator options, hierarchy-derived clock/reset
  roles and the color plan. Sharing the store is correct; hard-wiring every
  caller to `semdiff::Canonical_digest` plus one traversal is not.

### New — measured while scoping the XiangShan targets (2026-08-17)

- **F16 — `Rob.prp` is ONE `pub mod` in 405,279 lines / 44.6 MB.** Measured on
  lhdsuite `68afbda`: `grep -c '^pub mod' xiangshan/Backend/pyrope/Rob.prp` = 1.
  Three consequences, all load-bearing:
  1. It **settles half of open question 1** (L8's grain): a *per-source-unit*
     frontend cache buys **nothing** on Rob, because Rob is one unit. Only a
     per-def grain — or an intra-file incremental parse — helps there.
  2. Checking in `tests/comment1/Rob.prp` and `tests/bug1/Rob.prp` would add
     **~90 MB to git**. This is why lhdsuite's `apply_synth_only_variant`
     synthesizes the edit instead of overlaying files (§6 H3) — the right call,
     independently arrived at.
  3. It is the right stress case for L7's open question 2 (validation-snapshot
     representation): whatever a compile-scope snapshot costs, Rob pays it worst.

- **F17 — what a testbench can actually drive is limited by DEPTH, not only by
  width.** Two separate limits, both measured:
  1. **`lhd sim` refuses to WRITE a hierarchical path more than one level into
     a DUT port:**

     ```
     error[unsupported]: test 'alu.free_run': cannot write hierarchical path
     'acc.io_in_bits.ctrl.toRobValid' (read-only; use `sigref`)
     ```

     — and string `sigref` is on the not-implemented list. **READS at that
     depth work** (`acc.io_out_bits_res.data` is fine), so the limit is
     asymmetric. It binds hard here: `Alu`'s entire datapath (`ctrl.fuOpType`,
     `data.src`, `data.imm`) sits at depth 2 and cannot be stimulated at all.
  2. A testbench scalar still truncates past 64 bits, as lhdsuite's `cva6`
     entry documents. `Rob.io_enq_req` is 4080 bits and `Alu.io_in_bits.dataPipe`
     is 242.

  Note the tree moved *toward* drivability in the `68afbda` regeneration:
  `Alu.io_in_bits` used to be a flat `u543` and is now a nested tuple mirroring
  the Verilog `struct packed`. That makes the two language sides agree on port
  shape — which is what makes one driver serve both — but limit (1) means the
  newly-exposed leaves still cannot be poked.

  So the H2 drivers are valid **codegen-time benchmarks, host-compile-time
  benchmarks, throughput benchmarks and cross-language oracles — and NOT
  functional coverage.** Never report them as design verification.

- **F18 — `Alu` declares no `clock`/`reset` ports; `Rob` and `Backend` do.**
  So the drivers need two shapes: the `tick N clocks=(clock=1)` form dino uses
  for Rob/Backend, and a clock-port-free form for Alu (which holds no registers
  at all — each `step` is one combinational evaluation).

- **F19 — the sim split this loop needs already exists.** `bench/sim.sh` already
  reports `sim_setup_ms` (cgen), `sim_cc_ms` (host C++ compile+link, derived as
  `sim_run_ms - sim_exec_ms`), `sim_exec_ms` (**best of 3**, via `best_run`),
  `sim_cycles_per_s` and `sim_cycles_per_s_with_cc`. I3's guardrail is
  measurable today; what is missing is a *scenario* that runs it twice over one
  workdir (§6 H4).

- **F20 — `apply_synth_only_variant bug1` could never compile on `xs_rob`.
  *Reproduced and fixed.*** The shipped helper (`bench/common.sh:607`) rewrote
  the first inner ` & 1` of a matching line to ` ^ 1`. On `Rob` the first match
  is `io_perf_17_value_REG = A & (B >> 0x8f) & 1 & 1`, which becomes
  `A & (B >> 0x8f) ^ 1 & 1` — and lhd rejects that outright:

  ```
  Rob.prp:234818:81: error[syntax]: operators at the same precedence cannot be
  mixed without parentheses — the result depends on evaluation order
  ```

  So `xs_rob`'s pass-3 (`bug1`) leg would have failed on a syntax error rather
  than measuring anything. Fixed in the lhdsuite working tree by anchoring the
  match at end-of-line (so the inverted value is provably one bit and the
  assignment cannot narrow) and replacing the whole RHS with `(RHS) ^ 1` (so the
  rewrite is fully parenthesized and cannot create a precedence mix). Verified:
  `Alu` and `DivUnit` compile after mutation; `Rob`'s mutated line is now
  `io_perf_17_value_REG = (A & (B >> 0x8f) & 1 & 1) ^ 1`, which parses.

  Two caveats worth carrying: the helper's comment claims it edits a "public
  output", but its regex matches any line starting `  io_`, and on `Rob` it
  picks an internal `io_perf_17_value_REG` net. That is still expected to be
  observable, but it is not what the comment promises. And a `bug1` that changes
  nothing observable would silently produce a clean cache hit and prove nothing
  — so pass 3's ">=1 miss" assertion is load-bearing, not decorative.

- **F21 — `xiangshan/Backend/BUILD` references a `synth_stubs/` directory that
  does not exist in the repo.** The pulled BUILD declares
  `filegroup(name = "pyrope_stubs", srcs = glob(["synth_stubs/*.prp"]))` and
  `pyrope_stub_top = ["synth_stubs/DiffExtInstrCommit.prp"]`, and `defs.bzl`
  puts both in every `synth_only` target's `data`. The directory is absent, so
  `bazel test //bench:xs_*` cannot even analyze. It also blocks the three large
  tops from compiling standalone:

  ```
  error: unit `DummyDPICWrapper_InstrCommit` is blocked on unresolved
         import(s): "DiffExtInstrCommit.DiffExtInstrCommit"
  ```

  Measured: `Alu` and `DivUnit` compile from Pyrope; `Rob`, `ExuBlock` and
  `Backend` all fail this way.

  **Root cause — a translation artifact, not an incremental concern.** The SV
  wrapper instantiates its DPI cell only under `` `ifdef DUMMY ``:

  ```systemverilog
  module DummyDPICWrapper_InstrCommit(input clock, reset, input struct … io);
  `ifdef DUMMY
    DiffExtInstrCommit dpic (.clock(clock), .enable(io.valid & ~reset), .io(io.bits));
  `endif
  endmodule
  ```

  Under `-DSYNTHESIS` that body is **empty**, which is why the Verilog side never
  needs the cell. The Pyrope snapshot has no preprocessor, kept the
  instantiation unconditionally, and so references ~20 modules that do not exist
  in the tree. `Rob.prp` imports two of them on lines 2–3 and instantiates them
  11 times, so this is not a pruning failure — `lhd compile` is correctly
  pulling in the top's real cone.

  **Resolved, without committing anything.** `bench/gen_stubs.py` derives one
  sink model per cell from the `DummyDPICWrapper_*.prp` sources — same port
  names, no outputs, empty body, which is exactly what the wrapper compiles to
  in Verilog:

  ```pyrope
  pub mod DiffExtInstrCommit::[timecheck=false](clock:u1, enable:u1, io:u322) -> () {
  }
  ```

  The cell name and the `io` width are both read out of the wrapper, so a
  regenerated XiangShan snapshot picks up new cells and changed widths with no
  edit. 20 stubs generated; `Rob` then compiles: `pass — 0 errors, 14 warnings`
  in 2m21s (proxy, I11). The stubs are supplied as **extra positional sources**
  (`bench/compile.sh:34` already does this via `CORE_P_STUB_DIR`), so they never
  have to live beside the design.

  Two follow-ups this leaves open:
  1. `bench/gen_stubs.py` must run before the bench stages runfiles, and
     `xiangshan/Backend/BUILD`'s `pyrope_stubs`/`pyrope_stub_top` filegroups
     currently expect **checked-in** files under `synth_stubs/`. Either commit
     the generated output once, or make the generator a genrule feeding those
     filegroups. The second keeps the "generated, not committed" property.
  2. The wrapper reads the instance value (`mut dpic = DiffExt…(…)`) while the
     stub has no outputs, so each site warns `unresolved ref '%dpic_70' —
     wiring nil (0sb?)`. Harmless (the value is dead, matching the Verilog), but
     it is 20 warnings of noise that a stricter gate would trip over.

- **F22 — `lhd sim` cannot lower `Backend` at all, from either language side.**
  *Reproduced* on a clean Verilog-built library (so this is not an F21 or a
  driver problem):

  ```
  lhd compile verilog --top Backend …        2m35s, 0 errors      (proxy, mascm1)
  lhd sim lg:vbe …/xs_backend_tb.prp         6m18s, then:
  error[unsupported]: module `Backend` cannot be lowered by the
                      occurrence-wide color scheduler
  ```

  The times are proxies (I11); the **failure** is not — it is deterministic and
  reproduces regardless of host speed.

  The gate is `!color_runtime_root` in `inou/cgen/cgen_sim.cpp:4690`. **The
  emitted `Backend.color-plan.txt` gives the whole story, and it is NOT a scale
  wall** — compare it against `Rob`, which plans cleanly at only 4.6× fewer
  sites:

  | | `Rob` (plans fine) | `Backend` (fails) |
  |---|---|---|
  | `version-sites` / `fine-colors` | 638,869 | 2,932,654 |
  | **`colors` after coarsening** | **5,855** | **2,932,654** |
  | **`color-merges`** | **633,014 (99.1%)** | **0** |
  | `versioning-complete` | true | **false** |
  | `version-dag-acyclic` | true | **false** |
  | `boundary-dominance` | true | **false** |
  | errors | 0 | 1,086,778 |

  **One root cause, everything else downstream.** Of those 1,086,778 errors,
  1,086,776 are the same `direct-ABI slot … does not dominate consumer` line and
  one is `coarsened color condensation graph is cyclic`. The first domino is the
  remaining one:

  ```
  error "fine-color dependency cycle remains after state and compact-loop
         carry cuts; blocked-count=542032 witnesses=…"
  ```

  The chain is mechanical: a residual fine-color cycle ⇒ `version_dag_acyclic =
  false` ⇒ `coarsen_supported` is *literally* that flag
  (`inou/cgen/sim_color_plan.cpp:2863`) ⇒ **coarsening is skipped entirely**, so
  2.93 M sites each stay their own color ⇒ the condensation graph over them is
  cyclic ⇒ dominance fails at a million boundary slots ⇒ `!color_runtime_root`.
  So the 0-merge coarsening is a *victim*, not the cause, and the lever is the
  cycle. See **L11**.

  **Consequence for §2 in the meantime:** `xs_backend` is a compile+synth target
  until L11 lands; `xs_alu` and `xs_rob` carry the sim phase, which is enough for
  I3 — the guardrail needs a design whose sim runs, not the largest one. Keep
  `xs_backend_tb.prp` in the tree as L11's regression test; do not tag it
  `fixme` and do not leave it in the sim matrix as a known-red target.

---

## 5. The shape a storage lever must land in

This is not a milestone; it is the contract L4/L6/L7 must satisfy when they
touch persistence. Nothing here is a schedule.

### 5.1 One substrate, flow-specific adapters

The shared layer owns storage mechanics, not flow semantics. Its location must
not force a dependency on `//pass/semdiff`; choose `//core/incr` (or another
neutral package after checking the Bazel dependency graph). A graph adapter may
depend on semdiff and be reused by abc/LEC where its identity is appropriate.

Conceptual API contract:

```cpp
namespace livehd::incr {

struct Namespace {
  std::string repository;  // advisory provenance, not a content key
  std::string tag;         // "abc" | "sim" | "lec" | "formal" | "compile"
  std::string scope;       // top + side/output role; the ownership/lock unit
};

struct Key {
  Digest code_salt;  // implementation/schema identity
  Digest context;    // resolved result-affecting options and external inputs
  Digest input;      // cheap content fingerprint: proposes a candidate only
};

struct Candidate {
  Record_id id;
  Key       key;
  // Immutable references to validation snapshots, result artifacts and typed
  // metadata. A missing object makes this a MISS, never a partial hit.
};

class Run {
public:
  Run(std::string_view workdir, Namespace ns);  // acquires the scope lock

  std::vector<Candidate> propose(std::string_view alias, const Key&);

  // The adapter materializes the preserved input(s) and decides exact reuse.
  // The substrate records the verdict but does not define graph equivalence.
  bool validate(const Candidate&, Validator&);

  // Restore a previously computed result only after validate() returned true.
  // abc restores a mapped graph, sim retains generated files, formal replays
  // typed outcomes; these are not necessarily the validation snapshots.
  Result restore(const Candidate&, Restorer&);

  void accept(std::string_view alias, const Key&, Snapshot_bundle,
              Artifact_bundle, Typed_metadata);

  // Atomically replaces THIS scope's inventory after the complete flow passed.
  // Unreferenced immutable objects become GC candidates. abort() publishes none
  // of the run's new records or ownership changes.
  void commit(Inventory);
  void abort();
};

}  // namespace livehd::incr
```

The exact public C++ types can change, but the following distinctions are API
requirements:

- **Code salt** identifies the implementation/schema that interprets a record.
- **Context fingerprint** includes result-affecting resolved options and external
  content. Alternating two recipes/options must retain two valid records rather
  than invalidating the whole tag.
- **Input fingerprint** is only a lookup accelerator. Equality never directly
  authorizes reuse. Lookup may return multiple candidates; an invalid/colliding
  first candidate must not hide a later candidate that validates exactly.
- **Validation snapshots** are the old inputs checked against fresh inputs.
- **Result artifacts** are what a hit restores or retains. They may be different
  graphs/files from the validation snapshots.
- **Alias** is human provenance and per-scope inventory, not a soundness key.
- **Typed metadata** has an adapter-owned schema. Do not reduce formal policy or
  ABC QoR/port information to an unstructured payload string.

**Non-negotiables carried over from the existing implementations:**

- The cache is a SPEEDUP, never an oracle of record. A refusal, missing object,
  corrupt record or validator failure costs recomputation and nothing else.
- Exact adapter validation is mandatory before restoring a result. A fingerprint
  collision must be harmless.
- abc keeps `reuse_eligible` (`pass/partition/pass_partition.hpp:40`) as a
  load-bearing caller veto; no generic traversal overrides it.
- An adapter that cannot construct a stable fingerprint or exact validator must
  skip reuse. `canonical_digest.valid=false` remains one such refusal for the
  semdiff-based graph adapter.
- A completed record is immutable. Publication is one atomic manifest-pointer
  change after all snapshots/artifacts exist; an interrupted run leaves the
  previous committed generation usable.

### 5.2 Repository namespace, scopes and immutable objects

One workdir may serve multiple tops and multiple flows. It is a repository-level
namespace with per-scope ownership; it is not the current state of one design.

Conceptual layout (exact escaping/generation names are an implementation detail):

```
<workdir>/
  manifest.json                    # schema + optional repository provenance
  locks/<tag>/<scope>.lock         # one active owner of a flow/top/side scope
  locks/output/<destination>.lock  # short merge/publish lock for shared outputs
  runs/<invocation>/logs/          # collision-free per-invocation logs
  incr/
    scopes/<tag>/<scope>/
      current                      # atomic pointer to a committed generation
      generations/<generation>/inventory.json
    objects/
      graph/<content-id>/          # immutable GraphLibrary/skeleton objects
      blob/<content-id>/           # generated files and typed metadata
  state/compile/<scope>/lgdb/      # current working graph, not a cache object
```

An external `--emit-dir lg:` or `--emit-dir sim:` gets its own small ownership
manifest in that output directory. It lists only files/defs owned by that
producer scope, so cleanup never removes contributions made by another compile.

Migration is adapter-specific, not wholly mechanical:

| today | migration requirement |
|---|---|
| `<wd>/abc_cache/` + `<wd>/abc_cache_pre/` | preserve distinct PRE validation and MAPPED result namespaces |
| `<wd>/formal_cache.json` | split record families by typed retention policy; do not flatten hints/verdicts/cones into one record kind |
| `<odir>/gen_digests.json` | output manifest remains beside generated files; validated cache records live under the workdir |
| `<wd>/lec_{impl,ref}_lgdb` | two-sided validation snapshot bundle under the LEC scope |
| `<wd>/lgdb` | move to a compile scope; one global mutable library cannot support concurrent different tops |

Repository provenance is best effort:

- If the workdir and invocation both have a known Git remote and the normalized
  URLs differ, stop with a compile error advising a different workdir or
  `lhd workdir clean`. Normalize equivalent SSH/HTTPS spellings and never record
  URL credentials or userinfo in the manifest/diagnostic.
- The same remote, Git worktrees, copies, or missing/unknown Git metadata are
  accepted. Absolute checkout paths are never identity.
- Zero matching records is legal. Treat it as a cold replacement of that scope,
  then retire the old scope inventory only after the new run commits. Do not
  wipe other tops/flows or shared objects still referenced by them.

### 5.3 Enablement, context and code-salt discipline

- A user-named `--workdir` enables persistent validated reuse for every ported
  component. No user workdir means a scratch run and no persistent snapshots.
  Per-component `--set <ns>.cache=false` opts out.
- **Sim without `--workdir` still gets filesystem-level incrementality:**
  regenerate deterministic sources and replace each output only when its bytes
  changed. Bazel/ninja then retain compilation results. Do not offer a second
  emit-dir-backed graph-cache mode such as `sim.cache=true` without a workdir.
- Every record has a **context fingerprint**. Result-affecting options/content
  live here, not in a cache-wide salt. In particular: abc recipe + liberty
  content + mapping modes; LEC proof-relevant options; sim top/VCD/debug/codegen
  options + color-plan identity; compile frontend/uPass/import inputs.
- Every adapter has a build-time **code salt**, extending the
  `//lhd:formal_salt` source-content pattern to abc, sim and compile. Include the
  complete declared source/dependency closure and relevant external pins. Keep a
  schema version too: the source hash invalidates code changes, while the schema
  version explains on-disk compatibility.
- `"abc-incr-v6"` and `kSimGenVersion` cease being the sole soundness gate.
  `kSimGenVersion` may remain as a human-facing generated-C++ stamp.

### 5.4 Concurrency and failure contract

- The lock unit is `(repository namespace, tag, scope)`, where scope includes
  top and any side/output role needed to isolate ownership.
- Different tops/targets/flow tags may run concurrently and share immutable
  content objects.
- Two simultaneous runs of the same tag/scope are unsupported: the second exits
  cleanly with a `workdir-busy` diagnostic naming the owner PID, start time and
  command. Use an OS advisory file lock so process death releases ownership;
  the adjacent owner metadata is diagnostic only and cannot create a permanent
  stale lock.
- All new objects are written and closed before commit. Only the scope's atomic
  `current` pointer changes visibility. Failure/interrupt calls `abort()` (or is
  equivalent to it after process death), leaving the prior committed inventory.
- Content-object publication is idempotent create-if-absent. A short
  workdir-metadata lock serializes only provenance/global-reference updates; it
  must not serialize the compilation or validation work of different scopes.
- Different scopes that publish into the SAME mutable external `lg:`/`sim:`
  destination do their expensive work concurrently but serialize the final
  staged merge/manifest commit under a destination lock. This is required for a
  shared GraphLibrary; two processes must never call in-place `save()` on it.
- A referenced object that is missing, corrupt, has the wrong salt/context, or
  fails exact validation is a cold miss. It must never abort the compiler or
  authorize partial restoration.

---

## 6. H — the measurement harness (one-time; the only prerequisite)

Per I6 this is the sole thing that precedes the loop. Nothing in §7 is
meaningful until every number below is trustworthy and comparable.

### H1 — let the shipped `synth_only` cores also run the sim scenarios

**Mostly landed already.** lhdsuite `68afbda` added `//xiangshan/Backend`
(`pyrope`, `pyrope_top`, `verilog`, `verilog_filelist`, `pyrope_stubs`,
`pyrope_stub_top`) and five `CORES` entries — `xs_rob`, `xs_alu`, `xs_div`,
`xs_exu`, `xs_backend` — each `synth_only: True`, `color_algs: ["synth"]`,
`v_flags: "--single-unit"`, with `xs_exu`/`xs_backend` tagged `slow` and given
`CORE_SYNTH_BUDGET_S=1800` (a `timeout --foreground` re-exec of the whole
scenario in `bench/common.sh`). One package serves every block because
`lhd compile <top>.prp` prunes to the top's cone.

Two changes remain:

- **`synth_only` is one boolean where this plan needs two capabilities.**
  `core_benches()` (`bench/defs.bzl:463`) gates it with a hard-coded allowlist —
  `compile_verilog`, `compile_pyrope`, `compile_pyrope_parallel`, `synth`,
  `synth_incremental` — and drops everything else, sim included. The three
  targets in §2 must additionally emit `sim_pyrope`, `sim_verilog`,
  `sim_incremental` and `compile_incremental`, while still emitting **no**
  `lec*`, `verify*`, `synth_lec_*` or `sim_verilator` target. The smallest
  correct change is to stop deciding sim from `synth_only` and let the existing
  `needs_cfg` mechanism do it: mark the sim scenarios `needs_cfg="sim_tb"`, so a
  core with a driver gets them and one without does not. `defs.bzl` already
  reads `cfg.get("sim_tb", "")`, and `needs_cfg` already exists for exactly this
  ("no `sim_verilator` on a core with no `verilator_tb`"). Per `AGENTS.md` §3
  the generator keeps working from the `CORE_*` env contract; no module names in
  `bench/*.sh`.
- Add a `sim` filegroup to `//xiangshan/Backend` (the drivers now live in
  `xiangshan/Backend/sim/`) and the `sim_tb`, `sim_cycles`, `sim_tb_unit`,
  `sim_marker`, `sim_expect` keys to the three entries, pointing at H2.
- `_lhd_bench` puts `pkg + ":sim"` in `data` only for non-`synth_only` cores;
  that needs the same treatment, or the drivers will not be staged into
  runfiles.

**Blocked by F21** — `synth_stubs/` is referenced by the BUILD but absent, so
no `xs_*` target can be analyzed and `Rob`/`ExuBlock`/`Backend` cannot compile
from Pyrope at all. Resolve that first; nothing else in §6 can be measured on
those three until it is.

### H2 — a trivial sim driver per XS block — **written, and verified on `Alu`**

The XS blocks ship no testbench, so I3's guardrail would be unmeasurable there.
Three free-running drivers now exist:

```
xiangshan/Backend/sim/xs_alu_tb.prp        # verified, both language sides
xiangshan/Backend/sim/xs_rob_tb.prp        # verified from Verilog; Pyrope side needs F21
xiangshan/Backend/sim/xs_backend_tb.prp    # written, but blocked by F22 — keep it,
                                           # it is the regression test for that gap
```

Contract for each, following `dino/sim/dino_tb.prp`:

- Name the DUT as `import("lg:<Block>")` — a module compiled in the same
  `lhd sim` invocation — **not** a source path. That is what lets one driver
  serve both the Pyrope tree (`MODE=pyrope`) and the `lg:` library built from
  the Verilog tree (`MODE=verilog`).
- Take a `cycles` test parameter (`bench/sim.sh` makes a driver without one a
  hard failure, so the count cannot silently drift).
- Drive a source-seeded xorshift64 into the reachable inputs, `step`, and fold
  the observable outputs into an order-sensitive checksum masked to 63 bits (an
  unmasked u64 prints as a negative decimal, which is deterministic but a poor
  `sim_expect` string).
- Print `xs_<block>: cycles=N sum=…` — `sim_marker` is `xs_<block>:` and
  `sim_expect` is the checksum.
- Two shapes, per **F18**: Rob/Backend use dino's
  `tick cycles clocks=(clock=1)` and hold `reset` for two cycles; Alu declares
  no clock or reset port and needs the clock-port-free form.
- Only ports that are **≤64 bits, at most one level deep, and identically named
  on both language sides** are touched (F17). Everything else is left at its
  default and the driver says so in a comment.

**The checksum is cross-validated, not hand-written — and this is measured, not
asserted.** `bench/sim.sh` runs the same driver in `MODE=pyrope` and
`MODE=verilog`, and XS ships both trees. On `Alu`, both sides print the same
value:

```
lhd sim xiangshan/Backend/pyrope/Alu.prp  …/xs_alu_tb.prp --arg cycles=1000
  -> xs_alu: cycles=1000 sum=888709067567740450
lhd compile verilog --top Alu --emit-dir lg:V -- -F …/filelist.f -DSYNTHESIS --single-unit
lhd sim lg:V …/xs_alu_tb.prp --arg cycles=1000
  -> xs_alu: cycles=1000 sum=888709067567740450
```

So `xs_alu`'s `sim_expect` is `sum=888709067567740450`. `xs_rob` runs and
passes from the **Verilog** side (`xs_rob: cycles=1000 sum=4607111980261556205`)
but cannot be cross-checked against Pyrope until **F21** is resolved. Freeze the
other two `sim_expect` values only once both sides agree; leave them `""`
(marker gate only) until then rather than blessing one side's output.

**First cost datapoint, and the reason this loop exists** — *proxy on `mascm1`,
not a baseline (I11)*. One `lhd sim` of `Rob` at 1000 cycles, from the
Verilog-built library:

```
lhd compile verilog --top Rob …            1m08s wall
lhd sim lg:vrob …/xs_rob_tb.prp            3m24s wall / 29m24s user
```

The absolute seconds will differ on another machine; **the ratio is the durable
part.** user ≫ wall says that time is host C++ compile running wide, not
simulation — `Rob` at 1000 cycles simulates in milliseconds. That is exactly the
`sim_cc_ms` term L3 and L6 target, and exactly why I3 forbids buying it back out
of `sim_exec_ms`. Confirm the ratio still holds on the loop's own host during
the §9 baseline before assuming the same lever ordering.

Two things these drivers are **not**:

- **Not functional coverage (F17).** Depth and width limits mean `Alu`'s
  arithmetic core is not stimulated at all. Report them as codegen /
  host-compile / throughput benchmarks and as a cross-language oracle. Never as
  design verification.
- **Not a throughput count at 1000 cycles.** 1000 cycles is the right
  *smoke/marker* count and the right `sim_cycles` for the correctness gate, but
  it will measure process startup, not the simulator — dino's benchmark went
  from 2k → 20k → 4M cycles for exactly this reason, and 20k collapsed to 52 ms
  (mostly startup) after one cgen speedup. So: **`sim_cycles` = 1000 for the
  gate; the I3 timed leg uses a per-block `sim_perf_cycles` tuned in the H6
  baseline so `sim_exec_ms` clears the `best_run` noise floor by a comfortable
  margin.** Record both counts in the ledger row; re-check them after any large
  sim speedup.

### H3 — `comment1` / `bug1` for the XS blocks — **landed, with one bug fixed**

Every `*_incremental` scenario calls `apply_variant`, which overlays whole files
from `<core>/tests/<variant>/`. For XS that is untenable: **F16** measured
`Rob.prp` at 44.6 MB, so two whole-file variants would add ~90 MB to git.

lhdsuite `68afbda` solves this the right way with
`apply_synth_only_variant NAME DIR` (`bench/common.sh:596`), which **synthesizes
the edit in place** on the writable copy `copy_core_pyrope` already makes —
nothing is committed:

- `comment1` appends one comment line to `<Top>.prp`. Nothing really changed;
  every cache must hit.
- `bug1` inverts the first public-output-looking assignment of the form
  `  io_… = … & 1`, which cprop cannot erase.

**Its `bug1` was broken on `xs_rob` and is fixed in the working tree — see F20.**
The rewrite now anchors at end-of-line and fully parenthesizes
(`= (RHS) ^ 1`), which is both width-safe and precedence-safe. Verified by
compiling the mutated top: `Alu` and `DivUnit` pass; `Rob`'s mutated line now
parses (its remaining failure is F21, not the mutation).

Because the edit is synthesized rather than committed, **the log line is the
only record of what was injected** — keep `apply_synth_only_variant` reporting
the file and line it changed, and treat a silent no-op as a hard failure.

### H4 — the two missing scenarios

- **`sim_incremental`** (`bench/sim.sh`, `MODE=incr`) — the three-pass pattern
  over one shared `--workdir`, reporting `sim_setup_ms`, `sim_cc_ms`,
  `sim_exec_ms` and `sim_cycles_per_s` on **every** pass:
  - pass 1 cold;
  - pass 2 after `comment1` — **gate: zero generated files rewritten**, and
    `sim_cc_ms` far below pass 1;
  - pass 3 after `bug1` — **gate: exactly the edited dependency subtree's TUs
    recompiled**, untouched TUs still cached;
  - plus a no-workdir leg asserting byte-identical regeneration (mtimes
    preserved), which is what lets ninja/Bazel keep object files.
  - **I3 gate on every pass:** `sim_exec_ms` within the noise floor of pass 1's.
    Note this scenario must NOT pin `sim.ninja=false` the way the existing
    `sim_pyrope` benchmark does — the incremental host build is precisely what
    is under test here. Record which build path ran.
- **`compile_incremental`** (`bench/compile.sh`, `MODE=incr`) — the same three
  passes over one `--workdir`. Meaningful only once L1/L8 exist; land the
  scenario early anyway so its cold numbers are in the ledger from day one, and
  gate on **wall time**, never on hit count (I4).

### H5 — the "warm equals cold" checker

I2 needs a mechanical check, not a promise. Add one shared helper used by every
incremental scenario:

- **sim:** the generated `.cpp` / `.hpp` / `.iface.json` / color kernels from a
  warm pass must be **byte-identical** to the cold pass's for the same input.
- **synth:** the pass-2 netlist must LEC-equal the design; `synth_lec_synth`
  already does this on dino/minion, and for XS (no LEC) the cheaper substitute
  is a byte-compare of the emitted `lg:net_*` against a from-cold rerun.
- **formal/LEC:** the warm verdict set must equal the cold verdict set, name for
  name — not merely "also PROVEN".
- Any `store-failed` line anywhere is a hard failure (I5); principled refusals
  are counted into their own ledger column.

### H6 — ledger, noise floor and PDK

- **One ledger**, shared with `opt_loop_synth` (I10):
  `../lhdsuite/bench/ledger.jsonl`, one row per measured configuration. Rows
  carry the shared identity block plus this loop's columns:

  ```
  { "date", "host", "lhd_git_sha", "lhdsuite_git_sha", "pdk_version",
    "flow": "incr", "target", "phase",          // compile|synth|sim|lec|formal
    "config_id",
    "cold_ms", "warm_ms", "edit_ms",            // pass 1 / pass 2 / pass 3
    "warm_speedup", "edit_speedup",
    "hits", "misses", "redone_ms",              // redone_ms is the I4 number
    "sim_setup_ms", "sim_cc_ms", "sim_exec_ms",
    "sim_cycles", "sim_perf_cycles", "sim_cycles_per_s",
    "sim_cycles_per_s_with_cc",
    "store_failed", "refused",
    "workdir_bytes" }
  ```

  `workdir_bytes` is not decoration: cache size is a real cost and feeds open
  question 3 (retention budget). A row without `pdk_version` + `host` is
  unusable; rows from different hosts are never compared directly, only deltas
  within one host.
- **Noise floor.** Reuse `best_run` from `bench/common.sh`. Establish, per
  target and per column, the run-to-run spread on an unchanged tree. That spread
  is the epsilon in §8. Any "win" inside it is not a win. `sim_exec_ms` needs
  this most — it is the I3 guardrail, and a guardrail with an unmeasured noise
  floor cannot reject anything.
- **PDK resolution** comes from `opt_loop_synth` §3 (`ciel`-driven, never a
  pre-exported `HAGENT_TECH_DIR`). Synthesis rows here are affected by it, so
  stamp `pdk_version` in every row; adopt that resolution rather than
  reimplementing it.

### H6b — `current_opt_loop_incr.html`, the standing scoreboard

A JSONL ledger answers "what happened in run 47"; it does not answer "are we
ahead of where we started". Every iteration must therefore also refresh a single
rendered summary — **baseline vs current, per bench** — so the state of the loop
is one file away at any moment.

> ### ⚠ The page is a LIVE STATUS DISPLAY, not an end-of-run report
>
> **Write it as soon as each result exists, and keep rewriting it as more
> arrive.** Not at the end of the target, not at the end of the iteration — the
> moment a single (target, phase, mode) measurement lands, stamp it into the
> ledger and re-render. A matrix run takes tens of minutes; the whole value of
> having it rendered is watching it fill in, so a wrong number is caught while
> the run that produced it is still on screen and a crash in the fourth phase
> does not throw away the first three.
>
> This is a hard requirement on any measurement driver, not a nicety.
> `../lhdsuite/bench/matrix.sh` implements it: its `group_end` appends the row
> and immediately calls `ledger.py add` + `ledger.py render` (`publish()`), with
> the render's failures reported and then ignored — a broken renderer must never
> take down the expensive half of the run.
>
> Two corollaries:
> - **A partial page is the normal state**, so every cell must render honestly
>   when its measurement has not happened yet: `-`, never a blank that reads as
>   zero and never a derived number computed from a missing input.
> - **Keep fixing the page as results expose it.** The renderer is judged
>   against real rows, and real rows will keep finding defects in it — a
>   verdict pointing the wrong way, a division by a mode that did not run. Fix
>   those as they surface, in the same sitting; a scoreboard that is wrong once
>   is not trusted again.

- **Path:** `current_opt_loop_incr.html`, at the livehd repo root beside this
  plan. Its sibling loop writes `current_opt_loop_synth.html` in the same place.
- **Derived, never authored.** The ledger is the single source of truth; the
  HTML is a pure rendering and must be reproducible by re-running the renderer
  over `../lhdsuite/bench/ledger.jsonl`. **Generated, not committed** — it is
  per-host by construction (I11), so add it to `.gitignore`. Nothing may be
  recorded only in the HTML.
- **One host per page.** If the ledger holds rows from several hosts, render one
  section per host and never diff across them (I11). The header carries `host`,
  `lhd_git_sha`, `lhdsuite_git_sha`, `pdk_version`, the baseline `config_id` and
  date, and the current `config_id` and date.
- **One row per (target, phase)**, and for each tracked metric three cells —
  **baseline / current / delta** — plus a verdict from the §8 epsilon:

  | target | phase | metric | baseline | current | Δ | verdict |
  |---|---|---|---|---|---|---|
  | minion | compile | `warm_ms` | 4 210 | 980 | −76.7% | ✅ better |
  | minion | sim | `sim_cc_ms` | 18 400 | 9 100 | −50.5% | ✅ better |
  | minion | sim | `sim_exec_ms` | 2 190 | 2 205 | +0.7% | ➖ within noise |
  | xs_rob | sim | `sim_exec_ms` | 1 980 | 2 240 | +13.1% | ❌ **I3 violation** |

  (illustrative shape only — not measured, and not a target.)

- **The verdict column is the §8 gate, computed, not eyeballed.** Three states:
  better-than-noise, within-noise, worse-than-noise. An I3 guardrail column
  (`sim_exec_ms`, `sim_cycles_per_s`) that lands in "worse" renders as a hard
  failure regardless of how good the compile-side columns look — that is the
  whole point of the page. Show the epsilon actually used, so a "within noise"
  claim is auditable.
- **Also carry the non-time gates**, because they are what a scoreboard tempts
  you to forget: `store_failed` (must be 0), `refused` (must be attributed),
  the H5 warm-equals-cold result, and `workdir_bytes`.
- **Refreshed continuously while measuring** (see the banner above), and again
  at step 5 of §9 on land *and* on revert. A reverted lever still updates the
  page's "last attempted" line, so the same idea is not retried silently.
- **Keep it plain.** A static table, no external assets, readable in a terminal
  browser. Its job is to be glanceable and trustworthy, not pretty.

### H7 — the robustness gate

An `xs_backend` target fails on: non-zero exit, signal death, ABC
memory-admission refusal, or exceeding the 30-minute budget for the whole
three-pass scenario. The budget is itself a metric, so the trend is visible long
before it trips.

---

## 7. The lever pool

Hypotheses to draw from, ranked by expected value. The loop (§9) takes **one at
a time**; the list is expected to grow as the ledger teaches. Each lever names
its evidence, the columns it should move, and its dependencies.

**The ranking is derived from proxy measurements (I11).** It is a starting
order, not a fixed schedule: once the loop has its own baseline, re-rank by
where that host's ledger actually shows the time going. If the baseline
contradicts the order below, follow the baseline.

> **It did, on 2026-08-17.** The §9b I-0 baseline measured `pass.formal` at
> **86.3 s vs 86.8 s** on `xs_rob` — pure noise, not the ~8× F10 reports from
> minion — while the front end took 68% and color-plan discovery 23% of a warm
> `lhd sim`. Current order on this host: **L8 (front end, ~75 s of a ~91 s warm
> `xs_rob` rebuild)** ≫ L11 (it blocks `xs_backend` sim outright *and* was the
> reason `Rob`'s root looked uncacheable) > L5/L1, with L1 re-scoped to "verify
> on minion first". L2, L3, F4, F11 and most of L6 landed together as I-1.

### L1 — cache `pass.formal` obligations *(was M6)*

**Evidence: F10, the largest measured effect in the audit** — minion
`intpipe_csr_file` 32.9s → 0.67s and whole design 43.4s → 5.1s simply by turning
`pass.formal mode:fast` off (proxy, another box; treat as a ~50×/~8× ratio). The
verdict cache exists; `pass/formal/pass_formal.cpp` never consults it.

Cache individual **obligations**, not merely a whole-def verdict. A hit must
replay the same graph effects as a cold proof: proven/runtime-check attributes,
safe assertion deletion, assumption retention/removal, hierarchy-preflight
occurrence accounting and diagnostics. Key every outcome by its obligation cone,
hypothesis set, mode/budget semantics, top/occurrence context and code salt.

- **Moves:** `compile` `warm_ms`/`edit_ms` on dino and minion.
- **Depends on:** nothing. It is the first lever.
- **Note:** the 43.4s → 5.1s pass-*off* figure is an upper bound from a
  different box — not a promised warm number and not a target (I11). Re-measure
  on minion, on the loop's own host, after this lands and before drawing L8.

### L2 — auto-salt for abc, sim and compile *(was F5 / §5.3)*

**Soundness, and it protects every other measurement in this document.**
`Incr_cache::make_salt` does not hash `pass/abc` sources at all, and
`kSimGenVersion` is bumped by hand — so a change to the mapper read-back or to
the sim codegen silently reuses stale artifacts, and the ledger reports the
*old* implementation's numbers as the new one's. LEC already does this correctly
via a genrule source hash (`lhd/BUILD:17`); copy that mechanism.

- **Moves:** nothing, by design. It makes every other row believable.
- **Depends on:** nothing. **Every lever that edits `pass/abc` or
  `inou/cgen` depends on L2** — including `opt_loop_synth`'s entire W2 stream
  (that plan's W4.2 is this lever; see I10). Draw it early.

### L3 — deterministic sim regeneration, replace only on byte change *(was M0)*

**Already shipped for the C++, and now for the LLVM objects too.**
`File_output::same_on_disk` (`core/file_output.cpp:117`) has always byte-compared
before writing, and `lhd/tests/lhd_sim_incremental_test.sh` pins it. The one hole
was `*.llvm.o`, written straight through `llvm::raw_fd_ostream` — so the LLVM
backend stamped a fresh mtime on every object every run and forced a relink even
when nothing had changed. Closed 2026-08-17 (I-1). What remains of this lever is
the built-in (non-ninja) host build, which still recompiles every TU
unconditionally (`lhd/lhd_kernel_sim.cpp:2138`) because it has no depfiles.

For sim without a user workdir: regenerate the sources deterministically and
rewrite each output file only when its bytes actually changed, preserving
mtimes. ninja/Bazel then retain every object file whose input did not move.

**This is the purest I3-compliant lever in the pool:** the generated C++ is
byte-identical, so the compiled binary is byte-identical, so `sim_exec_ms`
cannot move at all — the guardrail is satisfied by construction. It buys
`sim_cc_ms`, which is the dominant term in an edit→simulate cycle.

- **Moves:** `sim_cc_ms` and `sim_cycles_per_s_with_cc` down; `sim_exec_ms`
  provably flat.
- **Depends on:** H4's `sim_incremental` scenario existing to prove it.

### L4 — ownership manifests, scoped working state and GC *(was M2)*

Fixes the two **reproduced** staleness defects, F6 and F7 — which are also a
measurement-integrity fix, since a ghost def is extra work in every number.

- Give each external `lg:`/`sim:` output an owner-scope inventory. Only remove a
  stale def/file previously owned by the same scope. This fixes F6/F7 without
  breaking `compile_pyrope_parallel` accumulation.
- Stage shared-output changes per run, then merge/publish them under a short
  destination lock; never mutate/save one GraphLibrary concurrently.
- Move the implicit mutable `<wd>/lgdb` into compile-scoped state so different
  tops do not share one current library.
- Drive sim `color_aux_sources` from the committed emission inventory, never a
  `directory_iterator` scan of arbitrary old files.
- Add `lhd workdir status|clean`: provenance, active locks, scopes, generations,
  live/orphan object sizes and the exact effect of clean. `status` is also where
  `workdir_bytes` comes from.
- GC only objects unreachable from every committed scope generation.

- **Moves:** correctness of every column; `workdir_bytes` down.
- **Tests:** `lhd/tests/lhd_workdir_gc_test.sh` (F6 rename — `alpha` must not
  reappear); `lhd/tests/lhd_sim_stale_artifact_test.sh` (F7 rename); two
  producers sharing an output cannot sweep one another's defs/files.

### L5 — content-stable abc region keys and intra-run abc reuse *(F8 + F9)*

The abc key is the region *name* `<top>__c<N>` and color ids are a function of
allocation order, so an edit that renumbers colors misses every region — the
difference between "one edit → one miss" and "one edit → many misses". And
`lookup_compare` is keyed by module name only, so two structurally identical
regions each pay ABC twice in the same run.

- **Moves:** `synth` `edit_ms` and `redone_ms` (directly visible in the existing
  `synth_incremental` pass-3 row), and `cold_ms` via intra-run sharing.
- **Depends on:** L2 (otherwise the measurement is not believable).
- **Shared with `opt_loop_synth` W4.1** — implemented here, consumed there (I10).
- Colour-renumber and replica reuse are *expected opportunities, not assumed
  facts*: canonical graph identity includes IO/state/Sub names. Require the
  tests to demonstrate which shapes actually share before promising a hit rate.

### L6 — validated sim reuse *(was M4; fixes F2/F4/F11)*

Today a **64-bit single-lane, traversal-order-dependent** FNV decides whether to
reuse a generated `.cpp`. A collision is a miscompile (I2).

- Define a sim-specific input fingerprint over every codegen-relevant graph
  feature, internal/exposed name, resolved option, hierarchy dependency and
  color-plan identity.
- Define an **exact validation projection** for those inputs. A plain semdiff
  structure check is insufficient: a name/attribute change can move the
  generated C++ or `.iface.json` without changing LEC semantics.
- A child edit invalidates precisely the ancestors whose generated parent code
  depends on the child's interface/subtree classification (F4).
- Make the color root eligible only when the complete plan/observation input is
  represented and exactly validated; remove the stored-but-unread digest (F11).
- Restore/retain a complete artifact set (`.hpp`, `.cpp`, `.iface.json`, color
  kernels/evaluators); one missing artifact is a miss.

- **Moves:** `sim_setup_ms` and `sim_cc_ms` down on the edit pass.
- **I3 watch:** this is the lever most able to violate I3 by accident. A
  finer reuse grain tempts a finer TU split. Hold `sim_exec_ms` flat or do not
  land it.
- **Depends on:** L2, L3, L4.

### L7 — the transactional substrate and its adapters *(was M1 + M3 + M5)*

The §5 substrate, plus the abc and LEC/`formal verify` adapters. It is a
refactor, not a hypothesis — so under I6 it is drawn when two or more adapters
are demonstrably paying for the lack of shared transactional semantics (F14),
not on a schedule.

- **abc adapter** is the reference port: it already implements exact validation.
  Input fingerprint proposes a PRE-map candidate; context covers verbatim
  recipe, Liberty content, mapping modes, DFF policy and code salt. Validate the
  fresh PRE body with `structural_identical` / cyclic-traversal fallback and
  `matching_names=true`. Preserve `reuse_eligible`, port-existence checks and
  the two-namespace rule (PRE validation bodies and MAPPED result bodies cannot
  share one GraphLibrary). Restore the mapped body, leaf-cell declarations and
  typed QoR metadata only after validation succeeds.
- **LEC / `formal verify` adapter** is a typed policy migration, not a
  mechanical JSON move. A definitive def-pair record has two input fingerprints,
  proof-relevant context and preserved REF/IMPL snapshots; validate each fresh
  side against its corresponding old side before transferring PROVEN. Keep
  PROVEN-only verdict caching; never cache REFUTED without witness
  revalidation; preserve budget-aware Unknown attempts. Strategy hints remain
  heuristic and survive code-salt/design changes. Pair hints remain
  entity-keyed, re-enter the obligation key and are revalidated at injection.
  Cone proofs keep their self-contained digest policy. Port record families
  incrementally if necessary.

- **Unit tests:** candidate/validate/accept/restore; new alias finding an
  existing content candidate; same alias with moved input; alternating contexts
  coexist; missing/corrupt snapshot and result artifact miss cold; injected
  fingerprint collision rejected by the validator; crash before commit preserves
  the old generation; killing the lock owner releases the scope;
  duplicate-scope contention fails cleanly; different scopes commit concurrently.
- **Depends on:** L4 (ownership) for the external-output half.

### L8 — the frontend / elaboration tier *(was M7)*

The remaining half of F10, deliberately measurement-gated. First select and name
the cache boundary:

1. parsed/source-normalized LNAST,
2. post-uPass LNAST,
3. post-tolg/pre-recipe LGraph, or
4. post-recipe graph.

The key must include source contents, resolved frontend/uPass options and the
import closure. The result must be a self-contained typed artifact for the
selected boundary, not an ambiguous "lowered graph".

**F16 already settles half of the grain question**: `Rob.prp` is ONE `pub mod`
in 405,279 lines, so a *per-source-unit* grain buys nothing on `xs_rob` — the
whole block is one unit. Per-source-unit still matches `lhd scan` and
`compile_pyrope_parallel`; per-lowered-def matches downstream reuse and is the
only grain with anything to offer Rob. Decide with the post-L1 profile, and use
`xs_rob` as the deciding target rather than dino.

- **Moves:** `compile` `warm_ms`/`edit_ms` on every target; LEC cold time (it
  re-elaborates BOTH sides from source on every run).
- **Depends on:** L1 (re-measure after it; L1 may absorb most of the compile
  cost on minion).

### L9 — replica and compact-loop reuse *(was M8)*

Explicit rather than assumed-automatic:

- A `Subnode_loop` body is one virtual/native work item across occurrences;
  never physically unroll an LGraph for reuse.
- A block instantiated N times should map once when exact boundary identity
  allows it, emit one def-level `.cpp`, and prove once per identical obligation.
- Hier LEC's lifted-loop `bbin:` cut points need a structural alias contract so
  a harmless source variable rename does not force UNKNOWN.
- Report principled non-reuse (`reuse_eligible=false`, undigestable state,
  unsupported loop shape) separately from implementation `store-failed` errors
  (I5).

- **Depends on:** L5, L6, L7.

### L11 — break the residual fine-color cycle so large designs plan at all *(F22)*

**Not a scoping workaround — a real optimization target**, and the only lever
here that unlocks a whole target/phase rather than shaving a percentage.

`Backend` cannot be simulated at all today, and the `.color-plan.txt` evidence in
F22 says why: **one** residual cycle disables coarsening outright
(`coarsen_supported = plan.summary_.version_dag_acyclic`,
`sim_color_plan.cpp:2863`), which leaves 2.93 M colors where `Rob` gets 5,855,
and everything after that collapses. Fix the cycle and the rest of the pipeline
is already known to work at 638 k sites.

The work, in order:

1. **Decide whether the cycle is REAL.** The report names it precisely —
   `fine-color dependency cycle remains after state and compact-loop carry cuts;
   blocked-count=542032 witnesses=…` — and prints witness site ids. Resolve a
   few witnesses back to nodes/nets and ask whether they form a genuine
   combinational loop or an artifact of dependency granularity.
2. **Suspect word-level tracking over packed structs first.** Compiling `Rob`
   from Pyrope emits, from a different subsystem:

   ```
   warning: 914 on-cycle bit-field read(s) could not be dissolved
            (315791 rewired over 16 pass(es)); a word-level combinational
            cycle may remain
   help:    node-creation budget exhausted on a read -- raise the split budget
            if this is not a real loop
   ```

   That is the same failure *shape*: independent bit ranges of one word tracked
   as a single dependency node manufacture a cycle that does not exist in the
   hardware. XiangShan is wall-to-wall packed structs, so it is the natural
   suspect. **Check the split budget first** — the help text says so, and a
   budget exhaustion is a bailout, not a property of the design.

   > **DONE, 2026-08-17, and the help text was WRONG. Do not repeat this.**
   >
   > `split_packed_selfref_wires` set ONE `cap_hit` flag for five different
   > refusals — per-reader budget, global budget, recursion depth, invalid pin,
   > degenerate slice — and the hint hard-coded the budget explanation for all
   > of them. Three measurements, on `Rob`:
   >
   > | change | result |
   > |---|---|
   > | node budget x8 (3 extra fixpoint rounds) | **0** extra reads dissolved; same 315,791 rewired; +5.5% wall, 10.4 GB peak RSS |
   > | recursion guard 64 → 256 | **0** change: same 914, same 315,791 |
   > | recursion guard 64 → 1024 | **stack overflow** (bus error) partway through the compile |
   >
   > **The limiter is the recursion depth, and it cannot be raised.** Landed
   > instead: the refusal reasons are now separated (`Stop_reason` bits) and the
   > warning names the one that actually fired — `stopped by recursion-depth`,
   > with a hint that says more budget will not help. The two round-count bugs
   > next to it are fixed too (it printed the `max_rounds` constant rather than
   > the rounds actually run, so "stuck after 2 rounds" and "cut off at the cap"
   > were indistinguishable; `Rob` in fact stops after **2**, not 16).
   >
   > **So the real work is to make `resolve` ITERATIVE** — an explicit work
   > stack instead of `self(self, …)` — which removes the constant rather than
   > tuning it. That is a rewrite of a subtle pass and wants its own iteration.
3. **If the cycle is false, cut it** the way the planner already cuts state and
   compact-loop carries — the machinery exists, it just does not reach this
   class. If it is real, the design needs a genuine cut point and the diagnostic
   should name a source-level net rather than a hash.
4. **Do not "fix" this by raising a threshold.** There is no size threshold in
   play: coarsening is gated on acyclicity alone. A change that merely lets a
   cyclic plan through would produce a wrong schedule, which is an I2 violation.

- **Moves:** unblocks `xs_backend` for the sim phase entirely; expected to help
  every large design's `sim_setup_ms`, since 2.93 M colors is also a codegen
  cost. Watch `kernel-reuses` (2,773,962 on Backend) — good reuse today is
  partly an artifact of the degenerate coloring.
- **Verification:** `xs_backend_tb.prp` runs and its checksum matches the
  Verilog side, `Backend.color-plan.txt` reports all-true with `color-merges`
  in `Rob`'s proportion, and `Rob`'s own plan is unchanged (no regression).
- **Depends on:** nothing. It is independent of the substrate work and can be
  drawn early — and per the sibling plan's interest in `pass color`, coordinate
  before touching shared partitioning code.
- **Note:** this is a *correctness/capability* lever, so §8's rule 1 (must
  improve a warm number) does not apply. It lands on rules 3–6 plus its own
  verification above.

### L10 — documentation truth *(was part of M0; F12)*

`pass.abc.cache`'s help says "content-addressed by a canonical region digest";
`abc_incr.hpp` says the opposite. Fix the help to describe the implementation
that actually ships, and add one `--workdir` contract section to
`lhd/README.md`. Cheap, and it stops the next audit rediscovering F12.

---

## 8. The accept/reject rule

A change **lands** only if, on **every** routine target (§2):

1. the warm number it targets (`warm_ms`, `edit_ms`, `redone_ms`, `sim_cc_ms`,
   `sim_setup_ms`) improves beyond the §H6 noise floor on at least one target;
2. **no** warm or cold number regresses beyond the noise floor on any target
   (I1) — in particular a lever must not pay for warm speed with `cold_ms`;
3. **I3 holds: `sim_exec_ms` and `sim_cycles_per_s` do not regress beyond the
   noise floor on any target.** If `sim_cc_ms` falls while `sim_exec_ms` rises,
   the change is **rejected regardless of net wall time**. If the two move
   together in an unexpected direction, stop and explain the coupling before
   landing — an unexplained coupling usually means the TU split or optimization
   level changed silently;
4. **warm equals cold (I2):** the §H5 checker passes on every scenario, and
   `store_failed` is 0 (I5). `refused` may be non-zero but must be attributed;
5. the correctness gate passes — dino/minion: LEC, formal and asserted sim all
   green; XS: completes, no diagnostic, sim marker present and the Pyrope and
   Verilog checksums agree;
6. the §2 hard constraints hold on `xs_backend`;
7. `workdir_bytes` is reported. A lever that more than doubles cache size needs
   an explicit note and feeds open question 3.

**8b — pure-soundness levers** (L2, L4, L10) are expected to show *no* movement
outside the noise floor. They land on rules 3–6 alone. A soundness lever that
happens to move a time number is a performance change and takes the full gate.

---

## 9. The iteration protocol

Each iteration:

1. **Pick one lever** from §7 (I7). Respect its stated dependencies.
2. **Check the shared-default constraint (I8).** If it only helps with
   per-design tuning, it is out of scope; record it under "future work" at the
   bottom of the ledger and pick another.
3. **Measure** on the routine set: `dino`, `minion`, `xs_alu`, `xs_rob` — the
   full §H6 row for each, all three passes.
4. **Gate** against §8. Run `xs_backend` and the periodic dino/minion LEC before
   landing.
5. **Land or revert**, and append the row either way — a measured negative
   result is worth as much as a positive one and stops the idea being retried.
   Then **regenerate `current_opt_loop_incr.html`** (H6b), on land *and* on
   revert, so the scoreboard never lags the ledger.
6. Every land bumps `config_id`. **The baseline column does NOT move** — it stays
   the §9 baseline for this host, so the page always answers "how far have we
   come since the loop started", not merely "since last week". `config_id`
   tracks the current column; re-baseline only on a host, PDK or toolchain
   change (I11), and say so in the page header when it happens.

**Periodic (weekly, and before any land):** `xs_backend`, the Verilog input
path, and the full dino/minion LEC + formal matrix.

**Baseline — do this once, on the loop's OWN machine, right after §6, before
drawing any lever (I11).** Record a full ledger row for every target in §2 at
current shipped defaults, on the resolved PDK, with `host` / `lhd_git_sha` /
`lhdsuite_git_sha` / `pdk_version` stamped, plus the per-column noise floor from
H6. That set — not this document — is what §8 compares against for the rest of
the loop's life. Re-take it after a PDK change, a toolchain change, or a move to
a different box.

The proxy below is the pre-PDK-change minion shape on a different box. It is
here to show what a row looks like and which phase dominates, **not** as a
number to hit:

```
                   compile   color      abc  hits  miss  remapped
pass1 (cold)          86ms    31ms   1168ms     0     9      916ms
pass2 (comment-only)  83ms    31ms    250ms     9     0        0ms
pass3 (one-line edit) 82ms    31ms    501ms     8     1      251ms
```

Two *shape* facts to carry in, which are the durable part: abc is ~90% of cold
synthesis, and minion resolves to only 9 regions — so region shape, not color
runtime, is how the color layer moves the abc number. Every absolute figure
above is superseded by the H6 baseline on the resolved PDK; do not compare
across the library change (T6) or across hosts (I11).

---

## 9b. Iteration log

One entry per drawn lever, land or revert (§9 step 5). Every figure is a delta
**within one host's ledger** (I11); the host block is stated once per entry and
never compared against another entry's.

### I-0 — baseline, 2026-08-17, `mascm1`

Apple M5 Max, 18 cores, 64 GB, arm64 Darwin 25.5.0; `bazel-bin/lhd/lhd` from
`-c opt`; lhdsuite `165850a` + working tree. Measured on `xs_rob` (`Rob`, one
`pub mod` in 405,279 lines / 44.6 MB — F16), which is the target that makes the
phase split legible because every phase is seconds rather than milliseconds.

| measurement | ms |
|---|---|
| `lhd compile Rob.prp --top Rob` (no emit, `pass.formal mode:fast`) | 86,293 |
| the same with `--set pass.formal.mode=none` | 86,846 |
| `lhd sim <source> --setup-only`, pass 1 cold | 109,305 |
| … pass 2, nothing changed | 109,938 |
| … pass 3, comment-only edit | 110,475 |
| `lhd sim lg:<prebuilt> --setup-only`, cold | 35,577 |
| … warm | 35,429 |
| `lhd compile lg:<prebuilt> --top Rob` (library load + recipe, no sim) | 6,388 |

**Three findings, all of which re-rank §7.**

1. **Nothing was incremental at all.** Three passes over one workdir, one of
   them a no-op, cost the same to the second. The per-module digest that does
   exist saved nothing measurable, because it excluded the one module that
   costs anything (F11).

2. **`pass.formal` is a no-op on this target: 86.3 s vs 86.8 s, i.e. noise.**
   F10's ~8×/~50× came from minion, on another box, and does not generalize —
   exactly the failure mode I11 exists to catch. **L1 is therefore NOT the lead
   lever**; re-confirm it on minion before drawing it, and expect it to be a
   minion-shaped win rather than a general one.

3. **The phase split, which is the durable part:** front end ≈ 74.5 s (68%),
   color-plan discovery ≈ 25 s (23%), C++ emission ≈ 4.5 s, library load +
   recipe ≈ 6.4 s. The plan's attention on codegen and on the host C++ compile
   was aimed at the smaller half.

### I-1 — sim generation reuse for the color root — **LANDED**

Levers L6 (in part), L3's remaining hole, F4, F11 and half of L2. One
iteration rather than four because the pieces are not separable: the root cannot
be reused without a hierarchical key, and a hierarchical key is worthless
without a complete artifact manifest.

- **`Cgen_sim::hier_graph_digest`** folds each instantiated child's digest,
  memoized per `Gid` (F4). Required for soundness: the root's code is derived
  from an occurrence plan spanning the whole cone, while its own body hash never
  moves for a leaf edit.
- **`gen_digests.json` records the complete artifact set per module**
  (`{"d":…,"f":[…]}`, schema `simgen-63`), so a hit restores everything the
  emission produced — the color runtime/kernel headers, one TU per canonical
  kernel, the evaluator and state-commit shards, and every `*.llvm.o`. One
  missing file is a cold miss, never a partial restore (§5.1).
- **The color root is now eligible** (F11). `color_plan_->report()` is out of the
  key: the plan is *derived* from the same cone and options, and past 100k
  version sites the report drops its per-site detail and degenerates into
  summary counts — redundant and too weak to key on.
- **Because the key no longer needs the plan, the caller can ask before building
  one.** `Cgen_sim::generation_current()` is answered in `inou_cgen.cpp` ahead of
  `Color_plan::discover`, which is what turns a 23% saving into the whole
  emitter phase.
- **`*.llvm.o` goes through `File_output`** (L3's one hole). It was written with
  a raw `raw_fd_ostream`, so the LLVM backend stamped a fresh mtime on every
  object every run and forced a relink even when the bytes were identical.
- **A record is written only when the run reported no error.** The old guard
  used the emitter's internal `cycle_unresolved_` flag, which fires on ordinary
  register reads at the color root — a flop's value is bound by the color
  runtime, not by `pin2var` (measured: dino reports its own program counter) —
  so it excluded precisely the module this reuse exists for, on every design
  with a register.
- **Auto-salt (L2)**, so none of the above depends on a human remembering to
  bump a version string: `//inou/cgen:cgen_salt` and `//pass/abc:abc_salt` hash
  their own sources (the latter including `pass/partition`, which decides the
  region boundaries the abc key is made of) into constants folded into both
  caches. Trap **T4** is retired.

**Result.** Every pair below is **cold and warm measured back to back in one
run** — the only comparison this box supports, see trap **T12**. "Warm" is the
pass after a comment-only source edit; the `lg:` rows feed a prebuilt library so
the front end is out of the picture.

| target / backend | input | cold | warm | speedup |
|---|---|---|---|---|
| `minion_top`, **slop** | source | 12,244 ms | **3,019 ms** | **4.1×** |
| `xs_rob`, **slop** | `lg:` | 37,149 ms | **6,487 ms** | **5.7×** |
| `xs_rob`, **slop** | source | 182,476 ms | **145,742 ms** | 1.25× |
| `dino`, **slop** | source | 408 ms | **143 ms** | 2.9× |
| `dino`, **llvm** | source | 586 ms | **168 ms** | 3.5× |

The "before" side needs no separate build: **before this change the warm pass
equalled the cold pass** — measured directly at I-0 on `xs_rob`
(109.3 / 109.9 / 110.5 s over three passes), and structural for every design,
since the root was excluded from reuse by construction.

The spread across targets is the interesting part and it is not noise: the two
big designs are opposites. **minion** is front-end-cheap and codegen-expensive,
so the lever takes most of its rebuild. **`xs_rob`** is one 44.6 MB source unit,
so the front end alone is ~40% of its cold time and caps the visible win at
1.25× even though the emitter phase itself fell 5.7× to the library-load floor
measured in I-0. Read the `lg:` row as the lever's true effect and the
from-source row as what a user sees until L8 lands.

The `xs_rob` absolute figures here are ~1.7× I-0's for the same work; that is
**T12** (the box throttled over the session), confirmed with a control run of an
untouched command, not a regression. The only work this lever adds to a cold run
is one hierarchical-digest walk per graph, shared through a single memo.

**Gate (§8):** warm numbers improve, cold does not regress beyond spread,
`sim_exec_ms` is unmovable *by construction* — the generated tree is
byte-identical, verified mechanically over all 589 files on `xs_rob` and across
a comment-only source edit, so the compiled binary is the same binary (I3 holds
with no measurement needed). `bazel test //...` 1997/1997 pass.

### I-2 — L11 probe: the residual fine-color cycle — **NEGATIVE, recorded**

Drawn because L11 blocks `xs_backend`'s sim entirely *and* was the reason
`Rob`'s root looked uncacheable. The plan said to check the node budget first,
because the shipped hint says so. The hint is wrong; three measurements are in
L11 above. Summary: the limiter is the **recursion-depth guard**, raising the
budget 8× dissolves **zero** reads (at +5.5% wall and 10.4 GB RSS), raising the
guard to 256 changes nothing and to 1024 **crashes**.

**Landed from it** (diagnostics only, no behaviour change): the five distinct
refusals are separated into `Stop_reason` bits and the warning names the one
that fired; the round count printed is the one actually run, not the constant.
**Reverted:** the adaptive budget growth — a measured negative result.

**Next step is a rewrite, not a knob:** make the slice resolver iterative.

### Still open after I-1/I-2

**The front end.** It is ~75 s of `xs_rob`'s ~91 s warm rebuild, and nothing
caches it. See L8 — and note that
`Rob.prp` being one 44.6 MB unit does **not** make a per-source-unit grain
useless here after all: `lhd sim <design> <tb>` compiles both in one invocation,
so a per-unit cache would let the design unit hit while only the edited
testbench re-parses, which is the shape of the actual edit→simulate loop. A
comment-only edit to the design still needs a post-parse key to hit.

---

## 10. Tests — `../lhdsuite/incr/`

The existing three-pass pattern (cold / `tests/comment1` comment-only touch /
`tests/bug1` real edit, all sharing one `--workdir`) is the right shape. Add a
fast tier plus ownership, context and concurrency coverage.

- **New `../lhdsuite/incr/` micro-cores.** dino-sized, sub-minute, so the
  incremental matrix runs on every change instead of at `eternal` timeout.
- **`sim_incremental`** and **`compile_incremental`** — §H4.
- **`workdir_shared`** — one `--workdir` driven through compile → synth → lec →
  sim in sequence, then all four again. Asserts no cross-tag collision, no stale
  artifact, and that each tag's second pass hits. Repeat with two different tops
  and confirm that both inventories survive while content objects are shared.
- **`workdir_concurrent`** — run different flow/top scopes at the same time and
  require both to commit. Start a duplicate flow/top scope and require a clean
  `workdir-busy` error; kill the owner and verify automatic lock release. Also
  publish two scopes to one shared `lg:` destination: compilation may overlap,
  but the staged merge/commit must serialize and retain both.
- **`workdir_replacement`** — zero overlap under same/unknown provenance is a
  cold successful replacement of only that scope. Two known different Git
  remotes produce the cleanup/new-workdir diagnostic.
- **`context_split`** — alternate two ABC recipes, LEC option sets and sim
  configurations. Returning to the first context must hit its retained record
  rather than finding a cache-wide salt eviction.
- **`replica_reuse`** — a design instantiating one block N times. Asserts ABC
  maps it once for shapes whose exact boundary validator admits sharing, sim
  emits one def-level `.cpp`, LEC proves one identical obligation, and
  principled refusals are reported rather than hidden.
- **`cache_damage`** — remove/corrupt a validation snapshot and a result
  artifact, and interrupt a writer before commit. Every case must recover as a
  cold run with the prior committed generation still readable.

Gates follow `synth_incremental`'s precedent: **time re-done, not hit count**
(I4), and a hard fail on any `store-failed` (I5).

---

## 11. Known traps

- **T1 — 1000 cycles measures process startup, not the simulator.** The XS
  drivers' `sim_cycles=1000` is a smoke/marker count. The I3 guardrail needs
  `sim_perf_cycles`, tuned per block against the noise floor. dino's history is
  the warning: 20k cycles was ~2.2s until one cgen fix took dino from ~9k to
  ~383k cycles/s, after which the same 20k measured 52 ms of mostly startup.
  **Re-check every cycle count after any large sim speedup** — a benchmark that
  has outrun its own count reports startup, not the simulator, and would let an
  I3 violation through.
- **T2 — a testbench can only poke one level deep, and only 64 bits wide
  (F17).** `lhd sim` rejects a deeper write outright — "cannot write
  hierarchical path … (read-only; use sigref)", and string `sigref` is
  unimplemented — while allowing the corresponding READ. `Alu`'s whole datapath
  sits one level too deep, so the XS drivers are compile/throughput benchmarks
  and a cross-language oracle — never functional coverage, and never quoted as
  such in a report.
- **T3 — hit count is not time (I4).** 199/264 regions hitting produced a 1.0×
  speedup. Any gate written in hits will pass while the loop achieves nothing.
- **T4 — a stale salt reports the old implementation's numbers as the new
  one's. RETIRED 2026-08-17 (I-1).** `//inou/cgen:cgen_salt` and
  `//pass/abc:abc_salt` hash their own sources into the two cache keys, so an
  emitter or mapper change auto-invalidates. `"abc-incr-v6"` and
  `kSimGenVersion` remain as human-readable schema tags, not as the soundness
  gate. Nothing here needs a human to remember a bump any more — including
  `opt_loop_synth`'s W2 sweeps, which this trap used to corrupt silently.
- **T5 — sim is incremental without a `--workdir` (F1).** It keys off the emit
  dir, so a run intended as "cold" can be accidentally warm. Every cold sim
  measurement must start from a fresh emit dir, not merely a fresh workdir.
- **T6 — the PDK swap.** `opt_loop_synth` §3 changes the resolved sky130 version
  (`e3262351…` 2025.10.15 → `e8daeda7…` 2026.06.11). Every synthesis number
  taken before that resolution is incomparable, for library reasons alone. Do
  not read the delta as a regression.
- **T7 — stale `bazel-bin` symlink.** `./bazel-bin` flips to the last-built
  `-c` config; numbers from a debug `lhd` are meaningless. The bench defaults to
  `-c opt`; confirm which binary ran before believing a measurement.
- **T8 — `--override_module=livehd=../livehd`** is how `lhdsuite` tests a local
  checkout. Without it the bench silently measures the pinned upstream livehd —
  i.e. reports "no change" for every lever.
- **T9 — `Rob.prp` is 44.6 MB in one `pub mod` (F16).** Do not check in
  whole-file variants for it (H3 synthesizes the edit instead), and expect it to
  dominate any per-file snapshot cost L7 introduces. It is also slow enough to
  distort a casual measurement — tens of seconds just to compile from Verilog,
  before any sim codegen (~68 s proxy on `mascm1`; re-measure per I11).
- **T11 — a synthesized `bug1` leaves no diff in git.** `apply_synth_only_variant`
  edits a scratch copy, so the only record of what pass 3 injected is the line
  the helper prints. A regression that changes WHICH site it picks would silently
  change what the scenario measures. Keep the printed site in the bench log, and
  treat "no site found" as a hard failure rather than a skipped edit (F20).
- **T12 — this box drifts by 1.7× over a session; only SAME-RUN ratios are
  evidence.** *Measured 2026-08-17.* One identical, untouched command
  (`lhd compile Rob.prp --set pass.formal.mode=none`) took **86,846 ms** in the
  morning and **144,411 ms** after several hours of heavy work — thermal
  throttling on an M5 Max. Two consequences, both of which nearly produced a
  false conclusion during I-1:
  1. **Never compare a number to one taken earlier in the session.** A "cold vs
     warm" pair is only evidence if the two were measured back to back. Rob's
     cold read 110 s, then 117 s, then 182 s across the day for the same work.
  2. **Check for runaway jobs before believing anything.** Two orphaned
     `lhd sim` processes pinned two cores for 38 minutes and inflated a whole
     measurement set ~8× before they were noticed (killing the child of a
     backgrounded script just makes the script run its NEXT command — kill the
     shell).
  3. **A single-threaded control probe is BLIND to the contention that matters
     here.** `bench/matrix.sh` times a fixed tiny compile at both ends of a
     matrix; across a sitting where a desktop app sat at 100% CPU it read
     36 ms → 37 ms, i.e. "no drift", while the same sitting's dino sim wall went
     from 5.5 s to 15.5 s. Every phase that dominates this loop — the host C++
     build, ABC, the solver workers — is already parallel, so one busy core
     hurts them and not the probe. That is why each row also records `loadavg`
     and the scoreboard renders it as a per-row qualifier.
  4. **`full` must be warmed, or it pays for everyone.** `full` is measured
     first, so it absorbs the one-time start of whatever external tool the phase
     reaches for — cvc5 and ABC for LEC, clang++ for sim. Before `matrix.sh`
     ran a throwaway invocation first, dino read `lec full 3006 ms` against
     `cold 184 ms`: a 16× "cache cost" that was entirely solver startup, and
     would have been published as "the incremental machinery makes clean builds
     dramatically cheaper", which is backwards.
  I11 says never compare across hosts; this says never compare across *time* on
  one host either. Bracket every claim with a control run of an untouched path.
- **T10 — the two loops interact.** An `opt_loop_synth` W2 recipe change moves
  the abc cache hit rate measured here; an L5 key change moves which netlist
  that plan is scoring. Both loops write one ledger (I10) so the interaction is
  visible; never take a cross-loop delta without checking `lhd_git_sha`.

---

## 12. Explicitly out of scope

- **Parallelism / threading.** A wall-clock win with no incremental content; it
  would mask algorithmic regressions in exactly the numbers this loop reads.
  (Same ruling as `opt_loop_synth` §9.)
- **Per-design cache tuning** (I8). The only user action a warm build may
  require is naming a `--workdir`.
- **Synthesis QoR** — delay, area, recipe, adder architecture, partition shape.
  That is `opt_loop_synth`; this loop must not move those numbers, and §8 rule 2
  will catch it if it does.
- **LEC and formal on XiangShan** (I9). `opt_loop_synth` R5 already rules LEC to
  dino for scale; the XS blocks carry no verify sidecar.
- **Functional verification of the XS blocks** (T2). The drivers are benchmarks
  and a cross-language oracle.
- **`xs_div` / `xs_exu`** as targets, unless the ledger asks for them (§2).

---

## 13. Open questions

Resolved during plan review:

- **Workdir identity:** repository-scoped, multiple tops/flows allowed. A known
  different Git remote errors; same/unknown provenance with zero overlap is a
  legal cold replacement of the affected scope.
- **Sim without workdir:** deterministic regenerate + replace-if-bytes-changed
  (L3); persistent graph/color reuse requires a user workdir.
- **Concurrency:** different scopes may run simultaneously; a duplicate
  tag/top/side scope fails cleanly and must never corrupt the previous
  generation.
- **Loop vs milestones (2026-08-17):** the loop starts now; every former
  milestone is a lever (I6). Only the harness (§6) precedes it.
- **XiangShan scope (2026-08-17):** three blocks, compile/synth/sim, with
  trivial in-house drivers so I3 is measurable there (I9, H2). The drivers are
  written; `xs_alu` is verified to produce an identical checksum from the Pyrope
  and Verilog trees. `xs_rob` / `xs_backend` await **F21**.
- **Variant mechanism (2026-08-17):** synthesized in place by
  `apply_synth_only_variant`, not checked in — settled by lhdsuite `68afbda`,
  with the `xs_rob` syntax bug fixed (F20).

Still open, intentionally measurement-gated:

1. **L8 boundary and grain.** Choose parsed LNAST, post-uPass LNAST,
   pre-recipe LGraph or post-recipe graph, then per-unit versus per-def, using
   post-L1 profiles and invalidation-fanout measurements. **F16 already rules
   per-source-unit out for `xs_rob`** — one 44.6 MB module — so the question is
   whether per-def pays for itself elsewhere, or whether Rob needs intra-file
   incremental parsing instead.
2. **Validation snapshot representation.** abc already stores two full
   GraphLibraries. LEC (both sides) and compile could make a minion or `xs_rob`
   workdir very large. Compare full bodies against a versioned compressed
   structural skeleton sufficient for the adapter's exact validator. Measure
   with `workdir_bytes` on `xs_rob`, the worst case.
3. **Retention/GC budget.** Define default size/age limits only after measuring
   real multi-top workdirs. Never evict objects referenced by a committed scope;
   `lhd workdir clean` remains the explicit immediate cleanup path.
4. **Does L6 have an I3-safe design at all?** Finer sim reuse grain tempts a
   finer TU split, which costs cycles/s. If no fingerprint/validation scheme
   restores whole-TU artifacts without fragmenting them, L6 reduces to L3 plus
   correctness fixes (F2/F4/F11) with no further compile-time win. Decide from
   the L3 numbers.

---

## Appendix — reference command shapes

**Path convention for this whole document:** every path is relative to the
**livehd repo root**, and commands are written to be run from there. The bench
suite is the sibling checkout `../lhdsuite`. No absolute paths appear anywhere in
this plan, so nothing here is tied to one user's home directory or one machine's
layout (I11).

`lhd` means `bazel-bin/lhd/lhd` from a `-c opt` build (T7), and lhdsuite bench
targets need `--override_module=livehd=../livehd` (T8). The commands below are
the ones that produced the proxy figures quoted elsewhere — re-run them on the
loop's own host to get real numbers.

**The three-pass incremental pattern** — one `--workdir` shared across passes,
which is what makes it incremental. `<Top>` is `PipelinedDualIssueCPU`, `Alu`, …

```bash
# pass 1 — cold
lhd compile src/pyrope/<Top>.prp --top <Top> --emit-dir lg:lg_p1 --workdir cw_p1
lhd pass color synth --top <Top>.<Top> lg:lg_p1 --workdir W
lhd pass abc  --top <Top>.<Top> lg:lg_p1 --emit-dir lg:net_p1 --workdir W \
              --result-json r_p1.json

# pass 2 — comment-only touch; every content-keyed cache must HIT
#   (dino/minion: apply_variant comment1;  xs_*: apply_synth_only_variant comment1)
# pass 3 — one real edit; exactly the touched region(s) must MISS
```

Gate on **milliseconds re-done**, never on hit count (I4). `bench/synth.sh`
already derives `abc_warm_speedup` from `$ic_miss_ms`; copy that shape.

**The sim phase, split into the three numbers I3 needs.** `--setup-only` is
codegen; the `--run-only` leg is codegen-free compile+simulate; re-running the
built binary isolates execution, and the remainder is the host C++ compile:

```bash
lhd sim <design> tb.prp --setup-only --set sim.vcd=false --workdir SW   # sim_setup_ms
lhd sim <design> tb.prp --run-only --arg cycles=$N --workdir SW         # = cc + exec
SW/sim/drv.bin --cycles $N                                              # sim_exec_ms (best of 3)
# sim_cc_ms = run_ms - exec_ms
```

Do **not** pin `--set sim.ninja=false` in `sim_incremental` the way the existing
`sim_pyrope` benchmark does — the incremental host build is the thing under
test there. Record which build path ran.

**The XS drivers, both language sides** (this is the cross-language oracle; both
must print the same checksum):

```bash
V=../lhdsuite/xiangshan/Backend/verilog
P=../lhdsuite/xiangshan/Backend/pyrope
S=../lhdsuite/xiangshan/Backend/sim

lhd sim $P/Alu.prp $S/xs_alu_tb.prp --arg cycles=1000 --workdir SWp
lhd compile verilog --top Alu --emit-dir lg:V_alu --workdir wv \
    -- -F $V/filelist.f -DSYNTHESIS --single-unit
lhd sim lg:V_alu $S/xs_alu_tb.prp --arg cycles=1000 --workdir SWv
# both -> xs_alu: cycles=1000 sum=888709067567740450
```

**Proxy wall times — orientation only, NOT a baseline (I11).** Measured on
`mascm1` (Apple M5 Max, 18 cores, 64 GB, arm64 Darwin 25.5.0) on 2026-08-17,
against one lhd build. Use them to recognise the *shape* of the workload — which
target is minutes rather than seconds, and which phase dominates — and to sanity
check that a fresh run is in the right order of magnitude. **Do not diff against
them**; the loop's own §9 baseline on its own host is the only comparison basis,
and these rows must be re-taken there before the first gate:

| command | proxy time (mascm1) |
|---|---|
| `lhd compile verilog --top Alu` | 0.6 s |
| `lhd sim` Alu @1000 cycles | ~3 s |
| `lhd compile verilog --top Rob` | 68 s |
| `lhd sim` Rob @1000 cycles | 3 m 24 s wall / 29 m 24 s user |
| `lhd compile verilog --top Backend` | 2 m 35 s |
| `lhd sim` Backend | 6 m 18 s, then fails (F22) |

The one figure expected to survive the move to another machine is the **Rob
user/wall ratio**, because it is a property of the work rather than the clock:
that time is host C++ compilation running wide, not simulation. If that ratio
does *not* reproduce on the loop's host, the lever ordering in §7 needs
re-deriving before anything is drawn.
