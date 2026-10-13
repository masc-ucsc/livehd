# 2c-cvc5 — cvc5 1.4.2, source build, solver profiles, LEC portfolio

Status: PLAN (2026-10-13). Nothing implemented. Baseline: cvc5 1.3.4 prebuilt
(`packages/cvc5_repo.bzl`, `packages/cvc5.BUILD`), pass/lec + pass/formal talk to
cvc5 through its C++ API only (`pass/lec/query.cpp`, `pass/formal/prove.cpp`).

## Findings that drive the plan (verified 2026-10-13 against cvc5-1.4.2 source)

- LiveHD never sets `incremental`; cvc5 defaults it to `true`. `set_defaults.cpp`
  then forces the CDCL(T) SAT solver to **MiniSat** whenever `sat-solver` is not
  set by the user. Every `bitblast-internal` query we run today is bit-blasted
  into MiniSat, including `Prover::solve` (prove.cpp:817) which never pushes.
  1.4.0 made CaDiCaL the default only for `incremental=false`.
- `sat-solver=cadical` is allowed in incremental mode (user-set values are not
  overridden). The CaDiCaL CDCL(T) wrapper (`src/prop/cadical/cadical.cpp`)
  implements `push`/`pop` via activation literals and solve-under-assumptions,
  so `checkSatAssuming` and our push/assert/check/pop windows keep working.
- What LEC's incremental use actually reuses across windows: the level-0 base
  (BMC unrolling equalities query.cpp:5944, reset pins :5996, memory dout ties
  :6099/:8208; ind's base is ~empty since state is tied by shared symbols) is
  preprocessed once at `push()` (`SolverEngine::push` -> `refreshAssertions`),
  and the symbolic bit-blast cache (`NodeBitblaster::d_bbAtoms`, plain map)
  lives for the solver. Everything else is discarded at `pop()`: MiniSat
  `removeClausesAboveLevel` on persistent AND learnt clauses, the CNF maps are
  user-context `CDInsertHashMap`, and `BVSolverBitblastInternal::addBBLemma`
  re-sends every bit-blast lemma (non-removable => stamped at the window's
  level). So the decomposed cuts rebuild the CNF of the shared cone every
  window and learn nothing from one cut to the next.
- Verify (`prove_properties_impl`, query.cpp:11882..13598) is genuinely
  incremental: `checkSatAssuming` without push keeps clauses + learnts, proven
  obligations are re-asserted at level 0 for later cycles (:11188).
- The 2026-06-16 fallback to `bitblast-internal` (query.cpp:4255 comment:
  lazy `bv-solver=bitblast` returned a spurious SAT, then `getValue` aborted
  inside CaDiCaL) was observed on a 1.3.x build; must be re-tested on 1.4.2
  because the lazy solver is the only mode where `bv-abstraction` (new 1.4.0)
  works.
- `unconstrained-simp` is auto-on for non-incremental QF_ABV without models,
  and off (error if user-set) under incremental. `bitblast=eager`,
  `deep-restart`, `learned-rewrite` are non-incremental only.
- 1.4.2 `model-verify` (default on, needs `produce-models`): after a timeout
  Unknown cvc5 checks the candidate model and answers SAT if it satisfies the
  assertions. We only enable models with `opts.witness`, so today we never see
  it. Models disable `unconstrained-simp`, so it belongs on a witness arm.
- Build: 1.4.0 renamed build types (`production` -> `unrestricted`; `safe`
  forces `bitblast-internal` and turns `arrays-exp` off, which we set, so it
  must be `unrestricted`); MPFR >= 4.2.1 became required unless `--no-mpfr`;
  CaDiCaL >= 2.1.0, SymFPU >= 1.2.0, GMP >= 6.3, LibPoly >= 0.2.1 (`--poly`,
  needed by `nl-cov` on the int-blast path); Python 3 + `pyparsing` + `tomli`
  at build time; `--auto-download` cannot run inside the Bazel sandbox.

## Rulings (user, 2026-10-13)

1. Update to cvc5 1.4.2 and **compile the libraries from source** (the Linux
   prebuilt is linked against a newer glibc than some servers have).
2. LEC (`prove_equal_impl`, `Prover::solve`): `incremental=false`, a fresh
   solver per obligation. Verify (`prove_properties_impl`): `incremental=true`
   with `sat-solver=cadical`, structure unchanged.
3. LEC parallelism = **portfolio on the same obligation**: N differently
   configured solvers race on one miter; the first decisive verdict wins, the
   rest are killed. BMC is the engine that matters most.
4. All forked solver arms, across defs and engines, take a slot from **one
   global pool** (no oversubscription). The pool is not a fixed `formal.jobs`
   count: it is a **shared job manager** that admits work on machine load
   (free CPUs) and free memory with a simple moving-average predictor of
   per-job memory. When the running jobs consume over 1/2 of system memory
   no further job is spawned, but one job is always allowed. The same manager
   serves formal, sim compile, and usyn. ABC stays out for now (concurrency
   issues). **One knob, `lhd.jobs`**, is the maximum number of jobs the
   manager may run at once across every class; unset = the system's
   available CPUs. `formal.jobs`, `sim.jobs` and `synth.threads` go away as
   separate caps.
5. Standing rules: tests < 30 s opt / 90 s dbg; LEC tests < 2 min with
   `formal.timeout <= 120`; timeout/Unknown = PASS, Refuted/crash = FAIL;
   prefer drop-in `.v`/`.prp` fixtures over new `*.sh` tests.

## Milestones

### M0 — bump the prebuilt to 1.4.2 (1 day, throwaway but de-risks M1)

- `_CVC5_VERSION = "1.4.2"` + new sha256s in `packages/cvc5_repo.bzl`; check
  the archive layout (`lib/libcadical.a`, `libpicpoly*.a`) still matches
  `cvc5.BUILD`'s `srcs`; the API changed `Term::const_iterator`'s ctor and
  made term managers outlive dependents (1.4.0) — grep our iterator uses.
- `bazel test //pass/lec/... //pass/formal/...` green on macOS + Linux.
- Re-run the 06-16 spurious-SAT miter (recover the fixture from commit
  `6768d65d6`'s test diff) under `bv-solver=bitblast` + `bv-sat-solver=cadical`
  + `produce-models`; record PASS/FAIL in this file. This decides whether the
  `lazy` profile in M2 is enabled by default or stays opt-in.

### M1 — build cvc5 from source under Bazel (3-5 days)

- New `packages/cvc5_src.BUILD` using `rules_foreign_cc` `cmake()` (already a
  dep; `packages/slang.BUILD` is the template). Hermetic inputs as their own
  `http_archive`s: cvc5 `cvc5-1.4.2` tarball, CaDiCaL >= 2.1.0 (build with
  `rules_foreign_cc` too, or let cvc5's CMake find a prebuilt via
  `--dep-path`), SymFPU 1.2.0 (header-only), LibPoly >= 0.2.1, `@gmp` (BCR,
  already present). Python from `rules_python` with `pyparsing` + `tomli`
  as `pip` requirements (needed by cvc5's `mkoptions`).
- configure: `./configure.sh unrestricted --static --no-static-binary --no-gpl
  --poly --no-mpfr --kissat --cryptominisat --prefix=$INSTALLDIR`
  + `-DENABLE_IPO=ON`; no parser lib, no bindings, no docs, assertions off.
- Keep the CaDiCaL symbol localization genrule exactly as in `cvc5.BUILD`
  (the clash with Berkeley-abc's vendored CaDiCaL is unchanged by building
  from source); feed it the source-built `libcvc5.a`/`libcadical.a`/LibPoly
  plus the new `libkissat.a`/`libcryptominisat5.a` (check CryptoMiniSat for
  symbol clashes with abc's CaDiCaL/Kissat as well).
- Switch `//packages:cvc5_repo.bzl` so `@cvc5` resolves to the source build;
  delete the prebuilt rows once Linux x86_64/arm64 + macOS arm64 are green.
  Keep a `--define=cvc5_prebuilt=1` escape hatch for one release only.
- Tests: `cvc5_link_smoke` extended with a `bv-sat-solver=kissat` and a
  `bv-sat-solver=cryptominisat` QF_BV `checkSat` (proves the optional
  backends are actually linked); `bazel test //pass/lec/... //pass/formal/...`.
- Doc: `pass/lec/lec.md` "Build facts (cvc5)" rewritten (version, deps,
  flags, why source).

### M2 — solver profiles + non-incremental LEC (4-6 days)

- One `Solver_profile` (`pass/lec/solver_profile.{hpp,cpp}`): `name`, the
  ordered `(option, value)` list, `incremental`, `needs_models_off`,
  `pure_bv_only`, `arrays_ok`. One `configure_solver(cvc5::Solver&, const
  Solver_profile&, const Solver_query_ctx&)` replaces every ad-hoc
  `setOption` site: `prove_equal_impl` (query.cpp:4161..4278),
  `slice_solver` (:9564), `prove_properties_impl` (:11775..11810),
  `Prover::solve` / `assumes_consistent` (prove.cpp:817/:1334). The ctx
  carries `has_mem`, `has_boxes`, `witness`, `timeout_ms`, `rlimit` so the
  existing gating (`bitblast-internal` only without arrays/UF, tlimit-per,
  rlimit) moves into one place.
- Profiles (initial set; everything beyond `internal` is measured in M4):
  | name | options | use |
  |---|---|---|
  | `internal` | `incremental=false`, `bv-solver=bitblast-internal` (=> CaDiCaL by default), `unconstrained-simp` left to auto | LEC default arm, `Prover::solve` |
  | `lazy` | `incremental=false`, `bv-solver=bitblast`, `bv-sat-solver=cadical`, `bv-abstraction=true`, `bv-abstraction-size=8` | multiplier/div miters; gated on the M0 re-test |
  | `eager-kissat` | `incremental=false`, `bitblast=eager`, `bv-sat-solver=kissat` | pure QF_BV, array-free, no models |
  | `intblast` | existing retry (`solve-bv-as-int=iand`) + `iand-mode`/`bvand-integer-granularity` variants | replaces the serial `int_blast_retry` with a parallel arm |
  | `witness` | `internal` + `produce-models=true` (so `model-verify` can turn a timeout into SAT) | the arm whose SAT result builds the witness |
  | `verify` | `incremental=true`, `sat-solver=cadical`, `bitblast-internal` (no arrays), models/cores/learned-literals as today | `prove_properties_impl` only |
- LEC windows become fresh solvers: keep the base as `std::vector<cvc5::Term>
  base_asserts` instead of asserting at level 0; `solve_obligation(term,
  profile)` builds a solver, asserts base + term, one `checkSat`. Replace the
  push/pop windows at query.cpp:6363, 6413, 6446, 7549, 7683, 7976, 10087,
  10137 and the level-0 asserts at :5944/:5961/:5996/:6099/:6102/:7568/:7571/
  :7795/:7818/:8208/:8211. `solve_check` keeps counting checks and budget.
  `TermManager` stays shared (term DAG is not rebuilt).
- BMC base slicing: per decomposed cut, assert only the base equalities in the
  cut's cone of influence over the term DAG (reuse the observability walk,
  :8063) so a fresh solver per cut does not re-preprocess the whole
  unrolling. Measure with and without; keep whichever is faster on lhdtrack.
- Witness: a SAT from a non-model arm triggers one re-solve on the `witness`
  profile (SAT is the cheap direction); `build_witness` unchanged otherwise.
- `rlimit` tier (prove.cpp, compile-time deterministic budget): measure that
  CaDiCaL's resource accounting is reproducible run-to-run on the satopt
  corpus; if not, pin that profile to `sat-solver=minisat` and say so in
  lec.md. (The CaDiCaL wrapper's SAT-level "solve with a budget" is
  `Unimplemented`; we only use cvc5-level `rlimit`/`tlimit-per`, which is a
  different path — verify.)
- Tests: `query_test` gains a table test that every profile is accepted by
  cvc5 for QF_BV and QF_ABV (no `CVC5ApiOptionException`), and that
  `internal` on a 16-bit semiprime miter returns the same verdict as 1.3.4
  (`solve_stats.hpp:18` fixture). Existing lec `.sh` tests are the regression
  net; expect `lec_budget_test`/`lec_min_timeout_default_test` to need
  re-baselining if timing-sensitive.

### M3a — shared job manager (load + memory admission) (3-4 days)

What exists today, to be unified rather than re-invented:
- sim: `livehd::sim::available_cpus()` (affinity, capped to 2 under
  `bazel test`) and `available_compile_jobs()` = cpus minus 1-minute loadavg,
  floor 1 (`inou/cgen/sim_compile_jobs.hpp`); consumed by
  `lhd/lhd_kernel_sim.cpp:2137..2158` (`sim.jobs`, plus a load-independent
  `stable_jobs` for anything that lands in a build stamp such as the PCH
  decision).
- memory probes: `livehd::cost::physical_ram_bytes()` (cgroup-aware) and
  `process_rss_bytes()` (`pass/cost/host_mem.{hpp,cpp}`), with the ABC
  `budget_bytes()` admission + `RLIMIT_AS` backstop.
- synth: `synthesis_thread_limit()` (auto cap 8) and `admit_abc_worker(limit,
  actual, baseline, active_projection, next_projection)` memory admission
  (`pass/synth/lane_scheduler.hpp`), driven by `pass/synth/region_driver.cpp`.
- formal: `tf::Executor(formal.jobs)` in `lhd/lhd_kernel_formal.cpp:2760`,
  fixed count, no load or memory input; forked racers outside any pool.

Design — `core/job_manager.{hpp,cpp}`, one process-wide instance:
- `Job_class` enum {formal_arm, sim_compile, usyn_region}; `acquire(cls,
  units) -> Job_slot` (blocks), `try_acquire`, slot release on destruction.
- **Single knob `lhd.jobs`** (new member of the `lhd.*` kernel namespace next
  to `seed`, `top`, `stats`, `incremental`: `lhd_kernel_common.cpp:951`
  validation list, `apply_lhd_settings`, `list_set_options`, the `lhd.hpp`
  option table). It is the hard ceiling on concurrently running jobs of all
  classes combined; unset or 0 = `available_cpus()` (affinity-aware, and
  still 2 under `bazel test`). The load and memory rules below only ever
  lower the admitted count beneath `lhd.jobs`. A single value stamped in
  `lhd.stats` (`labels["jobs"]`, `lhd_kernel_common.cpp:2254`) replaces the
  per-domain ones.
- Retired knobs: `formal.jobs`, `sim.jobs`, `synth.threads` (and the
  `sim.jobs` -> ninja `-j` use on the C++ path, which now also reads
  `lhd.jobs` at launch). Each becomes a **directed rename** error pointing at
  `lhd.jobs` through the existing rename tables (`renamed_sim_set`,
  `kRemovedFlags`, `lhd_kernel_common.cpp:1116`), never a silent alias.
  `abc.threads` stays: ABC is outside the manager until its concurrency
  issues are fixed. Verify's `verify_race_deadline_ms(opts, n_units)` and
  the hier driver's wall-budget comments that mention `formal.jobs`
  (`query.hpp:278`) are re-pointed at the manager's admitted count.
- CPU rule: `free_cpus = available_cpus() - ceil(loadavg_1m)` re-sampled on
  every admission (loadavg is already the OS's moving average), floor 1.
  Move `available_cpus()` / `available_compile_jobs()` out of
  `inou/cgen/sim_compile_jobs.hpp` into the manager; sim keeps `stable_jobs`
  for stamped decisions.
- Memory rule: every job comes with a **fast size-based guess**
  `predicted = base_bytes(cls) + slope(cls) * units`, where `units` is a
  cheap size the caller already has, and `slope` is an EWMA (alpha 0.25) of
  the observed `(peak - base) / units` of finished jobs of that class
  (threaded jobs: `process_rss_bytes()` delta divided by active jobs at
  release; forked jobs: the child's `ru_maxrss` at reap). The seed slope is
  a conservative constant calibrated once in M3a; the guess exists to stop a
  burst of large arms from launching together, it is never a substitute for
  the sampled admission (same stance as synth's `projected_abc_memory`,
  `pass/synth/lane_scheduler.cpp:30`, which stays the `usyn_region` guess).
  | class | units | base | seed slope |
  |---|---|---|---|
  | `formal_arm` | `flat_node_count_pruned(ref)+(impl)` (already computed by the oversize gate, query.cpp:61) x cycles (`bmc`: bound N, else 1) x profile factor (`internal`/`lazy` 1, `eager-kissat` 1.5, `intblast` 2) | cvc5 startup + encode, measure | calibrate on the lhdtrack sweep (bytes per node-cycle) |
  | `sim_compile` (LLVM kernel path, `tune.backend=llvm`) | per-color `cost_flat` from the Color_plan (`inou/cgen/sim_color_plan.hpp:304`), or the module's IR instruction count at `submit` time | LLVM context + TargetMachine, measure | calibrate (bytes of RSS per cost unit); each job is an in-process `Cgen_llvm::write_object` on a `Compile_workers` thread (`inou/cgen/sim_compile_workers.hpp`), so the peak is sampled as process RSS delta / active jobs at release |
  | `usyn_region` | AIG nodes, liberty bytes | `projected_abc_memory` as today | as today |
  Admit iff `rss_now + sum(active predicted remainder) + predicted(cls) <=
  physical_ram_bytes()/2`; **always admit when no job of any class is
  active**. Free memory is measured, not assumed: Linux `MemAvailable`, macOS
  `host_statistics64` free+inactive, folded in as `min(ram/2 - in_use,
  available)`. A job whose guess alone exceeds ram/2 still runs, alone, under
  the always-one rule (the oversize gate, not the manager, is what refuses).
- Cross-process: forked arms (M3) cannot share the in-process mutex, so the
  manager owns a counting semaphore in `MAP_SHARED|MAP_ANONYMOUS` memory
  sized to its current admission count and re-sized (posts/waits) as the
  load/memory verdict changes; the parent records each reaped child's
  `ru_maxrss` into the class EWMA. A SIGKILLed child's slot is released by
  the parent.
- `--stats` / `lhd.stats`: per class admitted, queued, peak concurrent,
  memory waits, EWMA bytes (mirrors `Parallel_stats`).
- Consumers in this plan: formal (`tf::Executor` sized to `lhd.jobs` and
  each def worker + fork arm holds a manager slot), sim compile on the
  **LLVM kernel path** (`Compile_workers::submit` acquires a `sim_compile`
  slot with the color's cost as `units` instead of its fixed
  `limit_ = available_compile_jobs()`), usyn (`region_driver` lanes acquire
  `usyn_region` slots; `synthesis_thread_limit`'s automatic cap of 8 is
  dropped in favor of `lhd.jobs`). The clang/ninja C++ TU path is out of
  scope for admission: ninja's `-j` is fixed at launch and cannot be
  throttled once spawned, so it is sized once from `min(lhd.jobs,
  available_compile_jobs())` and otherwise untouched. ABC (`pass/abc`, `admit_abc_worker`) untouched until its
  concurrency issues are fixed; then it becomes a fourth class.
- Tests: `core/job_manager_test.cpp` with injected probes (fake cpus, load,
  RAM, RSS): one job always admitted; no admission past ram/2; EWMA
  converges; `lhd.jobs` ceiling respected across mixed classes; fork
  semaphore handoff with a child that is killed. A `bazel test` run keeps
  the 2-CPU default. A CLI test that `--set formal.jobs=2`, `sim.jobs=2`
  and `synth.threads=2` each fail with the directed rename to `lhd.jobs`,
  and that `lhd list options lhd` shows `jobs`.

### M3 — LEC portfolio race under the job manager (3-4 days)

- Arms = engines x profiles. Extend `run_auto_portfolio` (query.cpp:2109) /
  `fork_race` (`pass/lec/fork_race.hpp`): today 2 arms (`ind`, `bmc`); new
  default arm list `bmc:internal, bmc:lazy, ind:internal` (+ `bmc:intblast`
  when `formal.lec.int_blast != off`). Knob `formal.portfolio` = comma list of
  `engine:profile`, default `auto`. `trust` is unchanged in spirit: a `bmc`
  Refuted or an `ind` Proven is decisive; a bounded `bmc` Proven settles
  only when no `ind` arm is still running (same asymmetry as today). First
  decisive result SIGKILLs the others.
- Arms still fork before any cvc5 object exists in the parent (fork_race
  precondition, `fork_race.hpp:14`): each arm re-encodes. Encoding is
  deterministic and cheap relative to solving; do not share solvers across
  processes.
- Pool = the M3a job manager (`formal_arm` class). A def worker thread holds
  its slot while it runs single-solver work; when it races, it releases its
  own slot and each arm acquires one (through the fork-shared semaphore)
  before encoding and releases on exit/kill (parent releases on behalf of a
  SIGKILLed child and records its `ru_maxrss`). Arms beyond the admitted
  count wait; the race deadline (`race_deadline_ms`) starts when the arm
  actually starts, so queued arms do not burn their budget. `lhd.jobs` is
  the only ceiling. Non-forking paths (`--workdir`,
  `lhd.incremental=false`) unchanged.
- `--stats`: per-arm (engine, profile, verdict, ms, won/killed/queued) via the
  existing result codec (tail field, see the fork-codec warnings at
  query.cpp:622..640).
- Tests: `fork_race_test` extended with a pool of 1 and 3 arms (serialization
  observed, winner still wins); `lec_auto_test.sh` with `formal.portfolio` set
  to a single arm and to the default; a `formal.jobs=1` run of the hier test
  (`lec_hier_test.sh`) must not deadlock (slot handoff between the def worker
  and its arms).

### M4 — option experiments (ongoing; each is a profile edit + a measurement)

Corpora: `inou/prp/tests/equiv`, lhdtrack LEC sweep, logikbench, minion
(few-minutes rule, never block on hour-long runs). Report per experiment:
Proven/Refuted/Unknown counts, wall, cvc5 ms, vs the `internal` arm.

1. `bv-to-bool=true` + `ite-simp` (+ `early-ite-removal`): the encoder emits
   zero Boolean sorts (all control is BV1), so the ITE simplifier never sees
   mux trees today.
2. `lazy` arm `bv-abstraction-size` 8 vs 16 vs 33; `bv-abstraction-value-limiter`.
3. `eager-kissat` vs `bv-sat-solver=cryptominisat` on XOR-heavy cones (CRC,
   LFSR, parity, ECC fixtures).
4. `deep-restart=input-and-prop` and `learned-rewrite` on the `internal` arm.
5. `intblast` variants: `iand-mode=sum|bitwise`, `bvand-integer-granularity=2..4`,
   `bv-to-int-use-pow2`, `nl-ext-tplanes-interleave`, `nl-cov-linear-model=initial`.
6. Arrays (memory BMC): `arrays-prop=2` (we set 1; the default is 2 — find out
   why), `arrays-weak-equiv` (non-witness arms only), `arrays-eager-lemmas`,
   `arrays-reduce-sharing`, `ee-mode=central`.
7. `witness` arm: does `model-verify` convert timeouts into real refutations
   on the lhdtrack sweep? Count verdict flips; every flip must replay in sim.
8. Verify: `produce-difficulty` + `getDifficulty` as the ranking signal for
   the timeout-core report; `minimal-unsat-cores`; `unsat-cores-mode=sat-proof`.
9. Seeds: `seed`/`sat-random-seed` per arm (free diversification once M3
   exists); cvc5's own `use-portfolio`/`compute-partitions` last, they fork
   their own processes and may fight the pool.

Skip (reviewed, not relevant to netlists): `bv-gauss-elim`, `bv-intro-pow2`,
`bv-assert-input`, `bv-eager-eval`, `relevance-filter`.

### M5 — docs

- `pass/lec/lec.md`: profiles table, `formal.portfolio`, pool semantics under
  `lhd.jobs`, the incremental-vs-fresh-solver rationale (the findings
  section above, condensed), updated build facts; drop `formal.jobs` from
  the CLI table.
- `docs/07-formal.md` CLI table: `formal.portfolio`, profile names,
  `formal.jobs` -> `lhd.jobs`.
- The `lhd.*` option docs (wherever `lhd.seed`/`lhd.incremental` are
  described) gain `lhd.jobs` with the load/memory semantics; sim docs drop
  `sim.jobs`; synth docs drop `synth.threads` (keep `abc.threads`).

## Risks / open questions

- The spurious-SAT lazy-bitblast miter (M0) — if it still fails on 1.4.2,
  file it upstream with the SMT-LIB dump and keep `lazy` opt-in.
- Job manager + `tf::Executor`: a def worker that blocks waiting for arm
  slots while holding its own slot deadlocks at `jobs=1`; the handoff in M3
  is the fix, the `jobs=1` hier test is the guard. The "always admit one"
  rule must be per manager, not per class, or two classes could each hold
  their one job past ram/2.
- Memory predictor cold start: before any job of a class finishes the slope
  is the seed constant, so the first batch is sized by the guess alone. A seed
  that is too low lets a burst through (the sampled admission then blocks the
  next ones); too high serializes needlessly. Report seed vs learned slope in
  stats so it can be re-seeded from real runs.
- `Compile_workers` bounds *outstanding* jobs (queued + running) and starts
  threads on demand; moving its admission into the manager must keep the
  "completion by any worker releases a submitter" behavior, or a slow first
  color idles the other CPUs again (the comment at the top of the header).
- Fresh solver per BMC cut re-preprocesses the unrolling unless sliced; if
  slicing is not enough, fall back to one non-incremental solver for the
  monolithic solve and keep the per-cut sweep on `internal` with
  `incremental=true` + `sat-solver=cadical` (still strictly better than today).
- rlimit reproducibility with CaDiCaL (M2) — measured, not assumed.
- Source build on Linux arm64 and the glibc floor: confirm the oldest server
  we care about links the result (that is the whole point of M1).

## Checklist

- [ ] M0 prebuilt 1.4.2 green; spurious-SAT re-test recorded
- [ ] M1 source build green on linux-x86_64 / linux-arm64 / macos-arm64; kissat + cms linked
- [ ] M2 profiles + fresh-solver LEC; verify on cadical incremental; rlimit reproducibility measured
- [ ] M3a `core/job_manager` (load + memory EWMA admission, fork-shared semaphore); single `lhd.jobs` ceiling (default = available CPUs); `formal.jobs`/`sim.jobs`/`synth.threads` retired with directed renames; sim LLVM + usyn + formal on it; ABC excluded
- [ ] M3 portfolio race under the job manager; `--stats` per arm; jobs=1 no deadlock
- [ ] M4 experiments 1-9 measured and recorded here
- [ ] M5 lec.md / 07-formal.md updated
