# todo_incr — the incremental compile loop (LEC, formal, synthesis, sim)

**Status:** loop, running. The audit behind it was taken 2026-08-14 (read-only,
over the four flows plus the shared `--workdir` plumbing); findings below were
checked against the code and, where marked *reproduced*, against
`bazel-bin/lhd/lhd`. **Converted from a milestone plan into a repeatable loop on
2026-08-17**, with three XiangShan blocks added as targets. Six iterations are
logged in §9b; the **L8 front-end tier landed 2026-08-18** and a fourth XS
target, `xs_renametable`, was added the same day.

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

**Current implementation window (2026-08-18): compile, LEC and synthesis.** The
window opened onto the front end when L8 landed (I-5/I-6); `pass.formal` and sim
findings below remain useful evidence but are not the active queue. Optimize
warm `lhd compile` closure reuse, warm `lhd lec` verdict reuse and warm
partitioned `pass.abc` reuse; keep cold correctness, diagnostic parity and
LEC-equivalence as gates. Do not spend routine iteration time on `xs_backend` or
whole-core `XSCore`.

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
| **I9** | **Current XS synthesis targets are `xs_alu` (routine smoke), `Dispatch` (next medium target), and `xs_rob` (periodic stress); `xs_backend` is excluded because it is too large for useful iteration latency.** LEC stays on dino/minion, whose paired Verilog/Pyrope sources provide a real oracle. A 2026-08-17 import-cone audit of the checked-in Backend snapshot found `Rob` = 566,236 lines, `Dispatch` = 832,115, `Rename` = 378,907, and `Backend` = 2,578,417. There are not two additional >1M-line sub-block cones in this snapshot; do not relabel smaller blocks to satisfy that threshold. A later `xs_rob` probe reached the 30-minute ABC cap without completing its first region, so it is not an every-edit target at the current partition grain. If more scale points are needed before another snapshot is imported, use `Dispatch` first, then `CtrlBlock`/`MemBlock`, based on measured runtime rather than the top file's line count. |
| **I10** | **One ledger, shared with `opt_loop_synth`; rows carry a `flow` field.** Both loops run on the same box, PDK and lhd sha, and they interact in both directions (an abc recipe change moves cache hit rate; a cache-key change changes which QoR number is being reported). Ownership split: the abc **key and salt soundness fixes** (F5/F8, i.e. that plan's W4.1/W4.2) are implemented **here** and consumed there; the abc **recipe/arithmetic/partition knobs** are theirs and never touched here. |

---

## 2. Targets and cadence

All designs live in `../lhdsuite`.

| target | top | source | phases exercised | cadence | role |
|---|---|---|---|---|---|
| `dino` | `PipelinedDualIssueCPU` | `dino/` | **lec**, synth correctness | every LEC iteration | fast paired-language proof and netlist LEC oracle |
| `minion` | `minion_top` | `minion/` | **lec**, synth correctness | periodic | deeper hierarchy; use after dino, within a fixed budget |
| `xs_alu` | `Alu` | `xiangshan/Backend/` | **synth**, sim, compile | every synthesis iteration | cheap cache and QoR smoke test |
| `xs_renametable` | `RenameTableWrapper` | ″ | **compile**, sim, synth | every iteration | **added 2026-08-18.** The medium XS point: 5,973 lines at the top over five near-identical `RenameTable*` children plus three DPI wrappers, so it exercises hierarchical and replica reuse at a size where the whole five-phase matrix still fits the budget (cold compile ~25 s, cold sim ~63 s). Cross-language oracle: both trees print `sum=7374455199715033088` at 1000 cycles |
| `xs_dispatch` | `Dispatch` | ″ | **synth** | candidate | 792,007-line import cone; the next scale point above `xs_rob` |
| `xs_rob` | `Rob` | ″ | **synth**, compile, sim | periodic stress | compile/color completes, but the first ABC region exceeded the 30-minute cap in the 2026-08-17 probe |
| `xs_ctrl` / `xs_mem` | `CtrlBlock` / `MemBlock` | full XS snapshot | **synth** | candidate | manageable hierarchy-heavy alternatives; add only after a measured cold run fits the iteration budget |

`xs_backend` and `XSCore` are robustness runs outside this loop, not land gates.
The active large-target budget is 30 minutes for the complete three-pass
scenario; a candidate that cannot fit is replaced rather than made routine.
`xs_rob` currently fails that cadence even when compile/color are reused, so it
remains a stress probe until partitioning makes its first region cacheable.

---

## 2b. Start here — the harness is up; run the loop

Read §1 (rulings) and §8 (the gate) first; they are short and everything else
assumes them.

**The four setup items this section used to list are done** (2026-08-17/18): the
DPI stubs are a genrule (F21), the XS cores emit sim scenarios (H1/H2), the two
missing scenarios and the ledger exist (H4/H6), and the baseline was taken (I-0).
So the entry point is now §9's protocol, not a setup checklist:

```bash
# from the lhdsuite checkout — one sitting, one config_id, live scoreboard
MATRIX_CONFIG_ID=<lever-name> ./bench/matrix.sh dino
MATRIX_CONFIG_ID=<lever-name> ./bench/matrix.sh xs_renametable compile synth sim
```

Each `matrix.sh` run produces one row per (phase, mode), stamps it into
`bench/ledger.jsonl`, and re-renders `docs/current_opt_loop_incr.html` after
**every** row (H6b). Pin `PDK_VERSION` **and** `HAGENT_TECH_DIR` together before
you start (T14) or the synth columns will be stamped with a library they did not
run against.

**Pick the lever from §7 and read §9b first** — six iterations are logged there,
two of them negative, and the residual bottlenecks named at the end of §9b are
the current queue. One lever per iteration (I7), gate on §8, append the row
whether it lands or reverts.

---

## 3. What exists today

| component | store | key | compare | salt / version | enabled by |
|---|---|---|---|---|---|
| compile / elaborate (**L8, landed 2026-08-18**) | `<wd>/incr/scopes/compile/<top>/{pyrope,ln,lg}` (§5.2 layout, first tenant) + `graph_inventory.json` | two tiers: **A** per source unit, keyed on the hermetic source snapshot; **B** the whole post-recipe graph closure, keyed on the transitive import-closure digest + context | exact: per-unit type/name tree compare (Tier A) and per-graph `interface_hash`/`h0`/`h1`/owner compare against the inventory (Tier B) | **auto** `kCompileSrcSalt` (`lhd/BUILD:37` genrule over the front end + lowering closure) | user `--workdir` **and** `lhd.incremental` (default true; the one switch shared with abc/formal) |
| `pass.formal` (in the O1 recipe) | **none** | — | — | — | never |
| LEC / `formal verify` | `<wd>/formal_cache.json` | 128-bit 2-lane Merkle canonical digest **pair** + options | digest only | **auto** `kFormalSrcSalt` (`lhd/BUILD:17` genrule over `pass/lec` + `pass/semdiff` + the cvc5 pin) | user `--workdir` **and** `lec.cache` |
| synthesis `pass.abc` | `<wd>/abc_cache/` + `abc_cache_pre/` (two GraphLibraries + json) | region **module name** `<top>__c<N>` + verbatim recipe | **exact structural compare vs the stored old graph**, traversal-bijection fallback | **manual** `"abc-incr-v6"` + liberty content + modes (`pass/abc/abc_incr.cpp:528`) | user `--workdir` **and** `lhd.incremental` |
| sim `inou.cgen.sim` | `<odir>/gen_digests.json` | module name | **64-bit single-lane FNV**, traversal-order and name sensitive, **not hierarchical** (`inou/cgen/cgen_sim.cpp:1994`) | **manual** `kSimGenVersion = "simgen-62"` (`cgen_sim.cpp:2110`) | any `--emit-dir sim:` odir; `--workdir` only indirectly |
| sim host build | `<wd>/sim/build.ninja` | mtime + depfile | — | — | ninja on PATH |
| color kernel reuse (intra-run) | memory | 128-bit class hash | occurrence verify | — | always |
| `color_reduce` (intra-run) | memory | private 2-lane digest | exact walk | — | `pass.color reduce` |

All four flows now implement the target rules somewhere, and `compile` — which
implemented none of them when this table was first written — is the only one on
the §5.2 shared layout. No two components implement them the same way yet; that
convergence is still L7.

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

- **Path:** `docs/current_opt_loop_incr.html`, beside this plan. Its sibling loop
  writes `docs/current_opt_loop_synth.html`. `../lhdsuite/bench/matrix.sh`
  defaults `MATRIX_HTML` to exactly that path — it used to default to the repo
  root, where nothing ever read it.
- **Derived, never authored.** The ledger is the single source of truth; the
  HTML is a pure rendering and must be reproducible by re-running the renderer
  over `../lhdsuite/bench/ledger.jsonl`. It **is** committed (the `.gitignore`
  entries are deliberately commented out) so the page travels with the plan;
  that makes the per-host caveat (I11) load-bearing rather than optional —
  a committed page is one host's page, and its header says which. Nothing may be
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
>
> **And again on 2026-08-18.** L8 landed (I-5) and I-6 unblocked it, which
> re-ranks the pool a second time: on a **total** warm restore the compile
> pipeline — `pass.upass`, `lnast.tolg`, `pass.cprop` and `pass.formal` alike —
> is skipped wholesale, so **L1 no longer moves the warm number at all** on any
> design whose edit is comment-only. What L1 still owns is the **cold** run
> (`pass.formal` is 2,270 ms of minion's 5,256 ms cold compile, 43%) and the
> **partial**-restore path. Current order: **per-graph diagnostic attribution**
> (the missing piece of I-6, which is what unblocks partial restores on any
> design that warns) > L1-for-cold > L11 > L5.

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

- **Moves:** `compile` **`cold_ms`** on dino and minion, plus `edit_ms` on the
  partial-restore path. **It no longer moves `warm_ms`** — I-6 skips
  `pass.formal` entirely on a total restore (re-scoped 2026-08-18).
- **Depends on:** nothing.
- **Measured here, 2026-08-18:** `pass.formal` is **2,270 ms of minion's
  5,256 ms cold compile (43%)** and **0 ms of its 63 ms warm compile**. The
  43.4s → 5.1s pass-*off* figure is an upper bound from a different box — not a
  promised warm number and not a target (I11).

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

### L8 — the frontend / elaboration tier *(was M7)* — **LANDED 2026-08-18**

The Pyrope implementation plan that drove this lever (`todo/livehd/2f-incr`,
milestones S1–S5) is **retired 2026-08-19**: every one of its milestones landed
and its §10 acceptance set is now locked by `lhd/tests/lhd_compile_cache_test.sh`
(gating and telemetry, exact comment-only Tier-B reuse, exporter-`pub` and
leaf-statefulness invalidation, context-descriptor mismatch, Tier-A and graph
damage as refused cold misses, `store_failed` as a hard fail, structural H5 over
a mixed dirty cone, ghost-def pruning, shared-workdir coexistence, and the I-6
warning replay). This section is now the only tracker for the lever.

Four rulings from that page that the code still obeys and that are not restated
in §5: incremental is active **only** under a user-named `--workdir` with a
`compile.cache=false` off-switch; the `pyrope/` snapshot is a hermetic *sandbox
input*, not a change detector; warm≡cold is **structural**
(`semdiff::structural_identical` + IO/attr equality) with srcmap/loc explicitly
exempt, because a comment-only edit restamps every srcid; and a Merkle digest
only *proposes* a candidate — an exact compare against the preserved snapshot
authorizes the reuse.

**Landed shape**, measured in **I-5** and completed by **I-6**: boundary (1)
*and* (4) — a Tier-A per-source-unit parse cache and a Tier-B whole-closure
post-recipe LGraph cache, in `<wd>/incr/scopes/compile/<top>/` (§5.2), keyed on
the hermetic source snapshot and the transitive import-closure digest, with
`kCompileSrcSalt` auto-derived from the front end + lowering sources. It serves
`lhd compile` **and** `lhd sim` / the LEC prereq compiles, because all three go
through the same `compile_sources`. Still open on this lever: per-graph
diagnostic attribution (I-6), which is what a *partial* restore of a
warning-carrying design needs before it can reuse anything.

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

**Residual levers on this tier, measurement-gated** (the retired page's S6, kept
here because nothing else records them):

- **Dependency-indexed Tier-A materialization.** A semantic edit today reparses
  the one dirty file and then materializes the *whole* hermetic Tier-A forest
  before mixing restored graphs with fresh lowering, because lexical import
  edges alone are not a sound selector. **A dirty-roots-only materialization was
  tried and rejected**: it passed the small H5 case, then lost package `pub`
  namespaces and produced a structurally different Minion top. Partial Tier-A
  loading stays closed until the source-level dependencies actually consumed
  during elaboration — `pub` values, named types, inferred widths/signs,
  templates, elaboration order — are recorded explicitly and exactly. Gate any
  reduction in forest loads on dino + minion + XiangShan edit H5.
- **Interface digest.** Tier-B keys a unit on its whole transitive import
  closure, so a leaf *body* edit invalidates its entire reverse import cone even
  when the boundary is untouched. Digest the interface instead (`pub` values,
  `io_meta` widths/signs, statefulness, clock-input interface, declared stages)
  and the cascade stops at the leaf.
- **`compile.threads` knob** (0/1 = sequential) for the incremental machinery;
  note `discover_imports` already parses with up to 16 threads today. Sequential
  efficiency first.
- Sub-file parse grain and snapshot cost are **open questions 1 and 2** below,
  not separate levers.

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

> ### Investigated 2026-08-17/18 — the cycle is NAMED now, and it is not what F22 guessed
>
> Four measurements, in order, each killing the previous hypothesis:
>
> | tried | result |
> |---|---|
> | node-creation budget x8 in `split_selfref` | **0** extra reads dissolved (+5.5% wall, 10.4 GB RSS) |
> | recursion guard 64 -> 256 | **0** change; 64 -> 1024 **stack overflow** |
> | run the split pass on a **512 MB stack**, guard 16384 | depth wall gone — the stop reason moved from `recursion-depth` to **`no-rule`**, and the same 914 reads survive. So `split_selfref` is missing RULES, not stack or budget. |
> | self-edge guard in the version DAG | `self-edges-dropped=0` — there were none. That fix was chasing a **bug in the diagnostic**, not in the planner (see below). |
>
> Both of those landed, but neither is free, so both landed *guarded*: the deep
> walk is an ESCALATION (shallow guard on the caller's stack first; the 512 MB
> worker only after a descent actually reports `recursion-depth`), because
> `pass/cprop` calls the splitter once per module from up to 16 `std::thread`
> workers whose default stack is 512 KB on macOS — a guard sized for 8 MB is not
> safe there, and a 512 MB mapping per module is not free. And the self-edge is
> dropped but the plan is still FAILED, since a site preceding itself is
> unsatisfiable, not vacuous: silently dropping it would trade a loud refusal
> for a stale read.
>
> **The diagnostic was lying, and that mattered.** The first cycle extractor
> followed any residual out-edge and reported the path when it dead-ended, which
> printed `cycle-length=1` and sent a whole iteration at self-edges that do not
> exist. The residual subgraph contains sites merely DOWNSTREAM of the cycle, so
> only a proper back-edge search is sound. Replaced with an iterative
> gray/black DFS.
>
> **What `Backend` actually has**, once the extractor was correct:
>
> ```
> blocked-count=307612  cycle-length=77
> cycle=s:ea2d8c75[pre-rise-eval/data/pre-rise] -> s:2273b911[pre-rise-eval/data/pre-rise]
>       -> ... 77 hops, EVERY ONE pre-rise-eval / data / pre-rise
> ```
>
> A **77-node pure combinational ring**: one slot, one role, no state, no
> carry — so no existing cut applies and none should. That is either a real
> combinational loop in the design (implausible: it synthesizes) or, far more
> likely, the **false loop from word-level tracking of packed structs** that F22
> suspected — the same family as `split_selfref`'s `no-rule` residue. The chain
> is: a packed read the splitter cannot dissolve -> a word-level false
> dependency -> a 77-site ring -> versioning fails -> coarsening off -> 2.95 M
> colors, 0 merges -> cyclic condensation -> 700,854 dominance errors -> refusal.
> ONE root error; everything else is downstream.
>
> ### 2026-08-18 — the mechanism, end to end, and a 99.2% fix that is still not enough
>
> **The chain, confirmed:**
>
> 1. XiangShan is wall-to-wall **packed structs**. A module port is one very wide
>    bundle and its fields are read with `get_mask` and rebuilt with
>    `concat`/`or`. The dependency graph tracks the WORD, not the field.
> 2. `split_selfref` exists to dissolve exactly that — rewrite a slice-of-a-concat
>    into a direct reference to the lane, so independent fields become
>    independent dependency nodes.
> 3. It was failing at scale. On `Backend`, **~165,000 undissolved on-cycle reads
>    across 13 modules**, every one `stopped by recursion-depth`: the recursive
>    descent hit the 64-frame guard, which existed only because a frame of that
>    470-line lambda costs ~8 KB against a default 8 MB stack.
> 4. **FIXED** by running the pass on a 512 MB worker stack (guard raised to
>    16384): `Backend`'s residue fell to **1,337 reads across 2 modules — 99.2%
>    gone** — and every remaining one now stops for a different reason,
>    `no-rule`, i.e. an operand shape the splitter has no descent rule for.
> 5. **Still blocked**, and that is consistent rather than surprising: ONE
>    surviving false edge closes a ring. The plan's errors fell 700,856 ->
>    667,551 and the ring shrank from 77 hops to 49, but a cyclic version DAG is
>    a boolean.
>
> **What the surviving ring is** (`Backend`, post-fix):
>
> ```
> cycle-length=49  cycle-ops=10xget_mask,9xconcat,8xor,5xmux,5xand,3xsub,3xeq,3xxor,2xsra,1xshl
>   0. op=sub  name=inner_vecRegion depth=1 ge=1   via p2192->p0/control
>   1. op=get_mask  depth=1                        via p0->p16/data
>   2. op=concat    depth=4                        via p0->p0/data
>   ...
> ```
>
> **19 of the 49 hops (39%) are pure bit-field plumbing** (`get_mask` +
> `concat`), and the ring is entered at a *named module instance*,
> `inner_vecRegion`, through **port 2192** — a bundle port thousands of bits
> wide. So the loop is: one field of that instance's wide output feeds, through
> slicing and reassembly, a DIFFERENT field of its own wide input. At the bit
> level that is acyclic; at word level it is a self-dependency. The same
> granularity artifact as (1), one level up — at the Sub boundary rather than
> inside a body.
>
> **Confidence.** Not proven false, but a real combinational loop through a
> vector-region submodule would also have to be rejected by synthesis, and the
> design synthesizes. Treat "false, from word-level tracking" as the strong
> hypothesis and the 39% plumbing plus the 2192-wide port as the evidence.
>
> **Why one edge costs the whole design:** cyclic version DAG ->
> `versioning_complete=false` -> `coarsen_supported` IS that flag
> (`sim_color_plan.cpp`) -> coarsening skipped entirely -> **3,074,582 colors, 0
> merges** (`Rob` gets 5,855 from 638 k sites) -> the condensation graph over 3 M
> colors is cyclic -> dominance fails at 667 k boundary slots ->
> `!color_runtime_root` -> `lhd sim` refuses the module.
>
> **Next**, in order: (a) teach `split_selfref` the `no-rule` operand shapes —
> its `And`/`Or` descents require provable footprint disjointness and that is
> what fails; (b) failing that, split the dependency node at a **Sub port
> boundary** by field rather than by word, which is what the `p2192` hop says is
> needed and would break this ring without touching the splitter.

> **So the next step is `split_selfref` rule COVERAGE, not the planner.** Run
> `LIVEHD_SIM_SPLIT_DEBUG=1` (output lands in `<workdir>/logs/*pass_cprop*.log`,
> not the terminal) and attack the operand shapes that refuse; on `Rob` the
> refusals propagate through `And` (174,816) and `Or` (86,037), whose descent
> rules have disjointness preconditions that are failing.

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
6. the selected §2 medium/large target completes within its fixed budget;
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
3. **Measure** on the flow-specific routine set: dino for LEC; `xs_alu` and the
   selected medium XS block for synthesis. Use `xs_rob` periodically as a
   partitioning stress probe. Add minion LEC before landing a broad proof-policy
   change.
4. **Gate** against §8. Do not make `xs_backend` a land gate.
5. **Land or revert**, and append the row either way — a measured negative
   result is worth as much as a positive one and stops the idea being retried.
   Then **regenerate `current_opt_loop_incr.html`** (H6b), on land *and* on
   revert, so the scoreboard never lags the ledger.
6. Every land bumps `config_id`. **The baseline column does NOT move** — it stays
   the §9 baseline for this host, so the page always answers "how far have we
   come since the loop started", not merely "since last week". `config_id`
   tracks the current column; re-baseline only on a host, PDK or toolchain
   change (I11), and say so in the page header when it happens.

**Periodic:** the Verilog input path and dino/minion LEC. `xs_backend` is an
explicit robustness experiment only.

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

**What a live row looks like today** — `bench/matrix.sh` writes exactly this
shape, one JSON line per (phase, mode), and the same run renders
`docs/current_opt_loop_incr.html`. The 2026-08-18 `I5-diag-replay` sitting on
`mascm1` (18 cores, PDK pinned to `e3262351…`, control drift 1.04) reads:

| target | phase | full (ms) | cold (ms) | incremental (ms) | speedup |
|---|---|---:|---:|---:|---:|
| dino | compile | 67 | 90 | 32 | 2.8× |
| dino | synth | 1,190 | 1,243 | 122 | 10.2× |
| dino | sim | 3,339 | 3,000 | 90 | 33.3× |
| dino | sim_llvm | 3,830 | 3,749 | 91 | 41.2× |
| dino | lec | 126 | 127 | 37 | 3.4× |
| minion | compile | 4,244 | 5,256 | 63 | 83.4× |
| minion | synth | 52,188 | 51,277 | 1,323 | 38.8× |
| minion | sim | 49,190 | 48,779 | 1,072 | 45.5× |
| minion | sim_llvm | 54,650 | 55,733 | 1,108 | 50.3× |
| xs_alu | compile | 58 | 77 | 29 | 2.7× |
| xs_alu | synth | 1,484 | 1,528 | 110 | 13.9× |
| xs_alu | sim | 3,249 | 3,256 | 82 | 39.7× |
| xs_alu | sim_llvm | 3,258 | 3,298 | 83 | 39.7× |
| xs_renametable | compile | 18,919 | 24,753 | 327 | 75.7× |
| xs_renametable | synth | 108,385 | 113,363 | 3,769 | 30.1× |
| xs_renametable | sim | 64,618 | 64,785 | 4,030 | 16.1× |
| xs_renametable | sim_llvm | 734,734 | 734,457 | 4,416 | 166.3× |
| xs_rob | compile | 91,799 | 108,121 | 20,861 | 5.2× |
| xs_rob | sim | 225,526 | 232,941 | 33,850 | 6.9× |

The **I3 guardrail column** for the same sitting — the number that must NOT
move, measured best-of-3 by re-running the binary the row just built:

| target | backend | `sim_exec_ms` full / cold / incremental | cycles |
|---|---|---|---:|
| dino | slop | 39 / 38 / 38 | 200,000 |
| dino | llvm | 46 / 46 / 46 | 200,000 |
| minion | slop | 3,208 / 3,126 / 3,150 | 200,000 |
| minion | llvm | 3,216 / 3,328 / 3,312 | 200,000 |
| xs_alu | slop | 32 / 32 / 32 | 500,000 |
| xs_renametable | slop | 779 / 781 / **782** | 5,000 |
| xs_renametable | llvm | 2,551 / 2,562 / 2,525 | 5,000 |
| xs_rob | slop | 2,607 / 2,613 / 2,499 | 20,000 |

> **The dino and `xs_alu` rows above do not count.** At 200k / 500k cycles they
> measure process startup, not the simulator (T1); the counts were raised to 2M
> and 5M straight after this sitting, so their guardrail is *absent* here rather
> than passing. minion, `xs_renametable` and `xs_rob` are correctly sized and do
> count — and all three are flat across the three modes, which is what I3 asks.

Read the SHAPE, not the absolutes (I11). Six durable facts in it:

1. **`full` → `cold` is the price of admission, and it is real:** minion +24%,
   `xs_renametable` +31%, `xs_rob` +18%, plus **67 MB / 277 MB / 1.06 GB** of
   workdir against 114 KB / 8.9 KB / 7.9 KB with the cache off. Nothing collects
   it (L4).
2. **The two `sim` backends converge on the same warm number** on dino, minion
   and `xs_alu`, because after L8 both are front-end bound and the front end is
   now the same cached thing.
3. **dino and `xs_alu` have hit the process-startup floor.** The fixed control
   compile is 24–25 ms in this sitting, so a 29–32 ms warm compile is startup;
   nothing in the compile column can move them further, and they are smoke
   tests from here on rather than optimization targets.
4. **`xs_rob`'s warm compile is 20,861 ms of which 20,273 ms is `inou.prp`** —
   re-parsing the single touched 44.6 MB source unit. Everything downstream is
   restored in 1.7 ms. F16/T9 has become the whole cost: the Pyrope PARSER at
   sub-file granularity is now this target's only lever.
5. **`sim.backend=llvm` is a bad trade on `xs_renametable`,** and this is the
   first target that shows it at scale: 652,913 ms of `--setup-only` against the
   slop backend's 32,619 ms (**20×** the codegen) to produce a binary that then
   runs **3.3× slower** (2,551 ms vs 779 ms for 5,000 cycles). dino (46 vs 39)
   and minion (3,328 vs 3,126) hint at the same direction; `xs_renametable` makes
   it unarguable. That is an I3-shaped result about a *backend choice*, not
   about a lever, and it belongs to whoever owns the llvm backend.
6. **minion LEC records `refuted` in every mode** — a real, pre-existing
   correctness failure, not a timing result. The timings are valid (the prover
   reached a verdict) but the phase is not green, so §8 rule 5 is not satisfied
   on minion and that is tracked separately, not by this loop.

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

### I-3 — lazy ABC/Liberty startup on cache hits — **IMPLEMENTED, validated**

`Mapper::start()` used to run before region decomposition, so even an all-hit
warm synthesis paid `Abc_Start`, alias installation, a full Liberty parse and
DFF-cell discovery. Startup is now idempotent and occurs at the first real
cache miss. An all-hit run restores mapped bodies without starting ABC; a mixed
run starts it once at the first miss. The run-level mapping options are kept
separately from temporary per-region overrides, and a destructor guarantees
`Abc_Stop` if a diagnostic unwinds the callback.

Same-host A/B on `xs_alu`, identical source, cache, PDK and 2/2 cache hits:

| measurement | eager startup | lazy startup | delta |
|---|---:|---:|---:|
| warm `pass.abc` | 2,846.6 ms | **957.5 ms** | **2.97x faster** |
| exact cache validation (`hit_ms`) | 245.9 ms | 246.5 ms | noise |
| cold `pass.abc` | 6,074.3 ms | 6,021.8 ms | noise |

The result JSON now reports `incremental.abc_started` as 0/1. The end-to-end
incremental test requires 0 for both all-hit passes and 1 for cold/one-miss
passes, while retaining the byte-identical Verilog and LEC gates.

### I-4 — lazy LEC clock-forest construction — **IMPLEMENTED, validated**

Hierarchical LEC used to build the ref and impl design-wide clock forests before
checking a single verdict-cache key. Those forests are phase-planning inputs;
an all-hit run returns before phase planning and discarded both complete graph
scans. Forest construction now uses `std::call_once` at the first real cache
miss. Concurrent misses still see one fully constructed immutable pair, while
an all-hit run never creates it.

Same-host optimized-build Dino probe, 17/17 verdict hits and a true one-line
comment append:

| measurement | eager forests | lazy forests | delta |
|---|---:|---:|---:|
| warm `pass.lec` | 18.5 ms | **17.0 ms** | **8% faster** |
| cold `pass.lec` | 237.2 ms | 231.8 ms | noise |

`lec_cache_test` runs with phase tracing: the cold miss must print a clock
forest, and the 3/3 all-hit replay must not. The full 35-target LEC suite remains
green. The cache digest, option key, and proof policy are unchanged.

The checked-in Dino `tests/comment1/ALU.prp` is not currently a comment-only
variant: it has drifted into a structurally different equivalent ALU. That
fixture correctly misses for `ALU` and its Merkle parent; a fresh append to the
current ALU hits 17/17. Do not weaken the semantic key to conceal fixture drift.

### I-5 — L8: the compile / elaboration tier — **LANDED** *(commit `feec06838`)*

The lever §7 ranked last and I-0 ranked first. Two tiers in one scope under
`<wd>/incr/scopes/compile/<top>/` (§5.2's first tenant):

- **Tier A, per source unit.** The workdir keeps a hermetic *copy* of each
  Pyrope source and its compact post-parse LNAST. The copy is the sandbox input,
  not a change detector: a warm compile reads the snapshot, so a mid-compile
  edit cannot be half-consumed.
- **Tier B, the whole post-recipe graph closure.** Keyed on the transitive
  import-closure digest plus the resolved option context, validated per graph
  against `graph_inventory.json` (`interface_hash`, `h0`/`h1`, owner). A
  *total* hit skips upass, tolg, cprop, pass.formal and `lg.save` outright; a
  clean lg-only consumer skips even the deserialize and materializes the stored
  library as a filesystem generation (`compile.cache.lg_artifact`).
- **`kCompileSrcSalt`** (`lhd/BUILD:37`) hashes the front end and lowering
  closure into the key, so the T4 discipline extends to this cache too.
- **It is not just `lhd compile`.** `lhd sim` and the LEC prereq compiles reach
  the front end through the same `compile_sources`, so they inherit the tier.

**Measured on `mascm1` the morning after it landed, before I-6** (config
`I5-compile-cache`; `compile` phase, ms, one sitting, control drift 1.04):

| target | full (cache off) | cold | incremental (comment-only) | speedup |
|---|---:|---:|---:|---:|
| dino | 63 | 77 | 69 | 1.1× |
| minion | 4,149 | 5,005 | **4,549** | 1.1× |

**That is the entry that matters, and it is a near-total miss.** minion reported
178 hits and 1 miss and saved 10% of the wall clock: `inou.prp` fell 345 ms → 8 ms
while `pass.upass` (1,532 ms), `lnast.tolg` (398 ms), `pass.cprop` (112 ms) and
`pass.formal` (2,223 ms) all re-ran in full. Tier A was working; **Tier B had
never been written at all** — the cold run's phase list has no
`compile.cache.lg_store`. That is I-6.

### I-6 — the generation carries its own warnings — **LANDED**

**Root cause, one branch.** `compile_cache_store_graphs` returned early — before
writing anything — if `pass.formal` had emitted any WARNING, on the reasoning
that a restored graph never re-enters pass.formal, so replaying nothing would
silence a verdict a cold run prints. Correct concern, wrong remedy: minion has
24 `onehot-deferred` warnings and dino has one, so for both designs *no graph
generation was ever stored*, and the warm compile paid the whole pipeline
forever. `xs_alu` has zero warnings, which is exactly why it looked fine.

**Fix.** The generation now CARRIES the warnings the graph pipeline produced —
not only `pass.formal`'s: `Result::compile_cache_diag_mark` records the sink
index right after the parse, and every warning past it is stored in
`graph_inventory.json` (schema 3) with its pass, code, category, message, hint,
span, `see` and notes. A **total** restore replays them verbatim; the pipeline
ran over no graph in that process, so the replayed set is exactly the cold set.
A **partial** restore is refused and counted (I5), because the stored records
carry no per-graph attribution and a generation stored from a partial run would
itself be incomplete.

Three details worth keeping:

- **The span is load-bearing.** The sink dedups on (code, span, message). A
  first version stored message-without-span and minion's 44 cold records
  replayed as 29 — twenty distinct `negative-shift` sites collapsed into five.
  The test now compares the whole diagnostic stream as a multiset keyed on
  (pass, code, message, hint, file, line, col).
- **Replayed spans are the cold generation's spans.** After a comment-only edit
  the byte offsets in the edited file have shifted, so a replayed location can
  be stale by that insertion. That is the srcmap exemption H5 already grants;
  re-resolving would need the trees the restore exists to avoid building.
- **Errors still keep a run cold.** `has_errors()` is unchanged: a refuted
  property or any failure means no generation is stored.

**Result** (config `I5-diag-replay`, same host and sitting, `compile` phase, ms):

| target | full | cold | incremental | speedup | vs I-5 incremental |
|---|---:|---:|---:|---:|---:|
| dino | 67 | 90 | **32** | 2.8× | 69 ms → 32 ms |
| minion | 4,244 | 5,256 | **63** | **83.4×** | 4,549 ms → 63 ms (**72×**) |
| xs_alu | 58 | 77 | **29** | 2.7× | already hitting (0 warnings) |
| xs_renametable | 18,919 | 24,753 | **327** | **75.7×** | already hitting (0 formal warnings) |
| xs_rob | 91,799 | 108,121 | **20,861** | 5.2× | already hitting (0 formal warnings) |

dino and `xs_alu` are at the floor: the fixed control compile costs 24–25 ms in
this sitting, so a 29–32 ms warm compile IS process startup. minion's warm
compile is now `compile.cache.lg_artifact` (14 ms) plus one reparsed file.
`xs_rob` is the exception that names the next lever: its 20,861 ms warm compile
is 20,273 ms of `inou.prp` re-parsing the one touched 44.6 MB unit and 1.7 ms of
everything else (F16/T9).

The lever also reaches `lhd sim`, which shares `compile_sources`. Same sitting,
`sim` phase, cold → incremental: minion 48,779 → 1,072 ms, `xs_renametable`
64,785 → 4,030 ms, `xs_rob` 232,941 → 33,850 ms. minion's llvm-backend sim fell
from 3,682 ms warm *before* the fix to 1,108 ms after, entirely inside
`sim_setup_ms` (3,306 → 712 ms) — that is the front end, not the emitter.

**Warm now equals cold in DIAGNOSTICS too, which it did not before**, and that
half of the change applies to every design — *including the ones that were
already hitting, where it is a correctness fix rather than a speedup*. A design
that hit took the artifact fast path and ran no pipeline stage, so it printed
**none** of that design's post-parse warnings; that is structural, not a
measurement, and it is why the number below is stated as "0 by construction":

| target | cold warnings | warm warnings, before | warm warnings, after (measured) |
|---|---:|---:|---:|
| dino | 21 | 21 — nothing was ever cached, so the pipeline re-ran | 21 |
| minion | 44 | 44 — same reason | 44 |
| xs_renametable | 9 | **0 by construction** — it hit, and a hit replayed nothing | 9 |

**Gate (§8).** Warm improves on every target; cold moves within the sitting's
spread (minion 5,005 → 5,256 ms, of which `compile.cache.lg_store` is 175 ms —
the price of admission for a tier that was previously never paid *because it was
never stored*); `sim_exec_ms` is unmoved (minion 3,126 → 3,150, xs_renametable
781 → 782, xs_alu 32 → 32); `store_failed` is 0 everywhere and `refused` is 0 on
these runs and attributed in the test; the structural H5 checker reports
`identical` on every incremental compile row. `bazel test //...` **1998/1998**.

**The price, and it is not small:** `workdir_bytes` after a cold compile is
67 MB for minion and **277 MB** for `xs_renametable`, against 114 KB and 8.9 KB
with the cache off. Cold wall time rises 24–31% on the two big targets (`full`
→ `cold`). Feeds open question 3; GC is L4.

**Still not covered:** a *semantic* edit to a design that warns re-compiles cold,
because the partial restore is refused. Per-graph diagnostic attribution — the
pipeline tagging each record with the graph it concerns — is the one missing
piece, and it is now the top item in the queue below.

### Still open after I-1 … I-6 — the current queue

Ranked by where the 2026-08-18 rows actually show the time going. **The front
end, which headed this list, is closed by I-5/I-6.**

1. **Per-graph diagnostic attribution.** The one missing piece of I-6, and it
   gates a whole mode rather than a percentage: a *semantic* edit to any design
   that emits a warning is refused a partial restore and recompiles cold. The
   fix is for each pipeline record to name the graph it concerns (the diag
   `Builder` already has `.attr(k,v)`, and `pass.formal` iterates per graph),
   after which a partial restore replays only the restored half. Until then
   `edit_ms` on dino/minion is unchanged from I-5.
2. **`pass.formal` on the COLD path (L1).** 2,270 ms of minion's 5,256 ms cold
   compile — 43% — and untouched by any cache. It no longer helps the warm
   number (I-6 skips the whole pipeline), which makes it a `cold_ms` and
   price-of-admission lever, not a latency one.
3. **Cache size (open question 3 / L4).** A cold compile leaves 67 MB for minion
   and 277 MB for `xs_renametable`, against 114 KB / 8.9 KB with the cache off,
   and nothing collects it. `full` → `cold` is +24% / +31% of wall time. Both
   numbers grow with every target added to a shared workdir.
4. **`sim` codegen on the medium targets.** With the front end cached,
   `xs_renametable`'s warm sim is 4,030 ms of which 3,824 ms is `--setup-only` —
   i.e. the emitter, not the parser. That is the residue I-1 left, now visible
   because everything in front of it got cheap.
5. **`pass.abc` region shape (L5).** Warm synthesis is already 30–39× on the big
   targets, but `xs_renametable` re-does 1,134 ms of *hit* validation across 25
   regions, and `xs_rob`'s first region still exceeds the 30-minute cap (I9).
6. **L11, the residual fine-color cycle.** Unchanged and still negative (I-2):
   the slice resolver needs to become iterative. It is what blocks `xs_backend`
   sim outright.

**Correctness, not performance, but it sits in this loop's routine set:**
minion LEC records `refuted` in all three modes. That is a real failure of the
§8 rule-5 gate, tracked separately; the timing rows for that phase are valid
(the prover reached a verdict) but the phase is not green.

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

**Landed against `lhd/tests/lhd_compile_cache_test.sh` (I-6).** The compile
tenant's own acceptance test now also covers the diagnostic contract, which is
the half a timing benchmark cannot see:

- a fixture that emits a `pass.formal` DEFERRED warning must still WRITE its
  graph generation (`graph_inventory.json` exists);
- a warm restore replays the whole diagnostic stream — compared as a
  **multiset keyed on (pass, code, message, hint, file, line, col)**, because a
  replay that keeps the message and drops the span silently collapses N sites
  into one through the sink's own dedup;
- the stored generation is inspected directly, so a store-side drop fails here
  rather than on some later restore;
- a semantic edit re-runs the pipeline, reproduces the warnings live, and is
  counted in `refused` — a principled refusal, visible (I5).

**Harness additions the same day**, all in `../lhdsuite`: the `xs_renametable`
core (`bench/defs.bzl` + `bench/BUILD` + the `bench/matrix.sh` case), its driver
`xiangshan/Backend/sim/xs_renametable_tb.prp`, and an
`incremental_edit_unit = "RenameTable"` override — `RenameTableWrapper` is pure
structure and has not one `io_… = … & 1` line, so the shared `bug1` anchor found
no site and both incremental scenarios failed until the edit was pointed at the
instantiated leaf (same escape hatch `xs_backend` uses for `DelayN_6`).
Seven of its nine bazel scenarios pass; the two `*_synth` ones fail only on T15.

---

## 11. Known traps

- **T1 — 1000 cycles measures process startup, not the simulator.** The XS
  drivers' `sim_cycles=1000` is a smoke/marker count. The I3 guardrail needs
  `sim_perf_cycles`, tuned per block against the noise floor. dino's history is
  the warning: 20k cycles was ~2.2s until one cgen fix took dino from ~9k to
  ~383k cycles/s, after which the same 20k measured 52 ms of mostly startup.
  **Re-check every cycle count after any large sim speedup** — a benchmark that
  has outrun its own count reports startup, not the simulator, and would let an
  I3 violation through. **It happened again on 2026-08-18**: dino read 38 ms at
  200k cycles and `xs_alu` 32 ms at 500k, so both targets' I3 columns in that
  sitting are startup and cannot reject anything. `matrix.sh` now uses 2M for
  dino and 5M for `xs_alu` (and `defs.bzl` matches); `minion` 200k → 3.1 s,
  `xs_renametable` 5k → 0.78 s and `xs_rob` 20k → 2.6 s were already correctly
  sized. Treat the dino/`xs_alu` guardrail rows before that fix as absent, not
  as passing.
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
- **T13 — a warning can be the reason nothing is cached.** `compile_cache_store_graphs`
  used to `return` before writing `graph_inventory.json` if pass.formal had
  emitted any WARNING, so a design with one unprovable obligation cached no
  graph at all — forever, on every run. minion has 24 `onehot-deferred`
  warnings, so its compile cache was parse-only and its warm compile cost 91% of
  cold while the telemetry cheerfully reported 178 hits. **The telemetry lied by
  omission**: hits counted Tier-A parse units, and nothing counted the Tier-B
  generation that was never written. Fixed in I-6 (the generation carries its
  warnings and replays them). The general lesson outlives the fix: when a cache
  reports hits and saves no time, check whether the expensive tier is being
  *stored*, not just whether it is being *read* — a `has_phase(lg_store)` on the
  COLD run is the one-line diagnostic.
- **T14 — the two harnesses resolve the PDK differently, so they can disagree
  about which library a number came from.** `bench/common.sh`'s
  `require_tech_dir` deliberately ignores an inherited `HAGENT_TECH_DIR` and
  takes whatever `ciel` currently has enabled, so **every `bazel test
  //bench:*_synth` runs against `ciel current`**. `bench/matrix.sh` honours an
  explicit pin, but only when `PDK_VERSION` **and** `HAGENT_TECH_DIR` are
  exported *together* — set one alone and it silently re-resolves through ciel.
  A session that inherits `e3262351…` from a shell profile while `ciel current`
  says `e8daeda7…` therefore gets bazel benches on one library and a matrix on
  the other. Export both, or neither, and never mix rows from the two. The
  2026-08-18 matrix rows were taken with both pinned to `e3262351…` to stay
  comparable with the previous sitting — one version BEHIND the resolution T6
  describes — so their synth columns must be re-baselined before being read
  against `opt_loop_synth`.
- **T15 — `bazel test //bench:*_synth` needs `ciel` on the test PATH, and the
  sandbox usually does not have it.** Every synthesis bench target fails
  immediately with `FAIL: synthesis benchmarks require 'ciel' on PATH (see
  README.md)` when `ciel` lives somewhere like `/opt/homebrew/bin`, because the
  bazel test environment does not inherit the interactive PATH. It is a setup
  precondition, not a result — `xs_alu_synth` and `xs_renametable_synth` fail
  identically — and `bench/matrix.sh`, which runs outside bazel, is unaffected.
  Do not read those two failures as a regression in a core you just added.
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
- **XiangShan scope (2026-08-17, extended 2026-08-18):** four blocks now —
  `xs_alu`, `xs_renametable`, `xs_rob`, plus `xs_backend` as a robustness run —
  compile/synth/sim, with trivial in-house drivers so I3 is measurable there
  (I9, H2). `xs_alu` and `xs_renametable` are both verified to produce an
  identical checksum from the Pyrope and Verilog trees
  (`888709067567740450` and `7374455199715033088` at 1000 cycles).
  `xs_renametable` was added because the gap between `xs_alu` (a 3.2 s cold sim)
  and `xs_rob` (233 s) left nothing in the middle: it is the only target where a
  full five-phase matrix and a real hierarchy coexist.
- **Variant mechanism (2026-08-17):** synthesized in place by
  `apply_synth_only_variant`, not checked in — settled by lhdsuite `68afbda`,
  with the `xs_rob` syntax bug fixed (F20).

Still open, intentionally measurement-gated:

1. **~~L8 boundary and grain~~ → intra-file incremental parsing.** *Answered
   2026-08-18 (I-5): per source unit for Tier A and the whole post-recipe graph
   closure for Tier B.* What F16 predicted is now measured rather than argued:
   `xs_rob`'s warm compile is **20,861 ms, of which 20,273 ms is `inou.prp`**
   re-parsing the single touched 44.6 MB unit while everything downstream is
   restored in 1.7 ms. So the remaining question is the narrow one — **does
   Pyrope want a sub-file parse grain, or a faster parser?** — and it now has a
   single, unambiguous target design and a number to beat.
2. **Validation snapshot representation.** abc already stores two full
   GraphLibraries. **Measured 2026-08-18, and it is the worst case F16
   predicted:** a cold compile leaves `workdir_bytes` of 1.35 MB (dino),
   67 MB (minion), 277 MB (`xs_renametable`) and **1.06 GB (`xs_rob`)** — the
   last against 7.9 KB with `compile.cache=false`, and `compile.cache.store`
   alone costs 4,901 ms of that target's cold run. Compare full bodies against a
   versioned compressed structural skeleton sufficient for the adapter's exact
   validator; the hermetic *source* snapshot is a large part of the Rob figure
   and is the first thing to measure.
3. **Retention/GC budget.** Define default size/age limits only after measuring
   real multi-top workdirs. Never evict objects referenced by a committed scope;
   `lhd workdir clean` remains the explicit immediate cleanup path. Now urgent
   rather than theoretical: one workdir shared across the five §2 targets holds
   ~1.4 GB of compile scopes before any sim or abc tenant is counted.
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
