# Projection Simulator Performance Investigation

## Priority and goal

The first project milestone remains 30 CVA6 blocks actually specialized,
simulated, and compared with `interpretDesign`. Performance investigation is
second priority. It should proceed in bounded, reproducible experiments that
do not delay the coverage sweep or overwrite in-flight artifacts.

The goal is to learn whether the verified projection path can approach the
cost of a hand-written Lean simulator, and which changes would make that
possible. “Competes” must ultimately be judged against an executable manual
simulator on the same designs and stimuli. No such apples-to-apples baseline
has yet been recorded here; `interpretDesign` is the semantic reference, not
automatically the manual-simulator performance baseline.

## Current evidence

The completed optimized real-design run is recorded in `PHASE6_PERF.md` §18
and `.perfwork/optrun-alu.log`:

| Measurement | Observed result |
|---|---:|
| Design | `rt_alu_gate.dcert`, 6,597 nodes, 6,137 sources, 0 flops |
| Backend | Native executable, `mixDriver` + `hwAPVarT`, all generated objects and link built with `-O2` |
| Specialization/check | 61,267,341 ms total; specialization 61,267,338 ms and residual check 3 ms |
| Residual | 89,996 terms; checker bound 28,899 |
| Peak RSS | 24,904 KB |
| Validation | Six seeds match `interpretDesign`; four-cycle combinational trace agrees; control interpreter agrees |
| Residual execution | Six reference runs 8.9 s total; six checked-step runs 9.9 s total |

This is one successful native execution of the same ALU certificate already
counted in coverage. The concrete specialization result was not kernel
certified, the ALU has no state, and it is not a new coverage item.

An earlier diagnostic fork produced the same residual size and checker bound
in about 1,226 seconds. That is a strong optimization signal, not a controlled
50x comparison: it used a different specialization implementation and `-O0`,
while the covered run used `mixDriver` + `hwAPVarT` and `-O2`. The still-running
O0 jobs were launched from different source snapshots. Their eventual results
must be reported with their own provenance; they cannot isolate the effect of
`-O2`.

Prior measurements in `PHASE6_PERF.md` report super-cubic behavior for the
verified host path on `chainD`, and identify repeated traversal/preparation of
the partially-static environment as the main measured cost. In particular,
the `prepare`, `PVal.shift`, and `PVal.toPRes` paths traverse or rebuild data.
The existing `env0` experiment removes one source-spine cost on its fixtures,
but is an unproved variant and leaves other chains. The `PartialEvaluatorFast`
path is also diagnostic and unproved. Neither may be described as a verified
optimization until its semantic obligation is discharged.

The low peak RSS on the completed ALU run is evidence against a shortage of
physical memory for that run. It does not rule out allocation churn, frequent
garbage collection, or a larger memory cost on other circuits. Current machine
memory availability is not a substitute for per-run measurements. At this
plan's inspection time, `free -h` showed about 434 GiB available; this is a
separate current-host snapshot, not a measurement from the O2 run itself.

## Hypotheses and tests

| ID | Hypothesis | Evidence now | Validation experiment | Decision rule |
|---|---|---|---|---|
| H1 | Specialization CPU work, rather than residual checking or simulation, dominates elapsed time. | The ALU run spent 61,267 s specializing, 3 ms checking, and under 50 s across all recorded reference, step, and control runs. | Keep stage timers separate on every probe. Profile one bounded representative run or a smaller fixture with the same hot path; report inclusive and self costs without summing overlapping inclusive values. | Optimize the stage that dominates measured wall time. Do not spend time optimizing the 3 ms checker first. |
| H2 | More physical RAM will not materially shorten the ALU specialization. | Completed run peak RSS was about 25 MB; the host has ample available memory at the time of this plan. Existing call profiles point to traversal and Lean constructor work. | On a bounded representative run, record peak RSS, CPU time, major/minor faults, swap activity, and available Lean GC/heap counters. First verify the supported counters and heap controls for the pinned Lean toolchain. If GC/heap pressure is significant, compare the same binary or source with two documented heap settings in isolated runs. | Try a larger heap only if GC frequency, paging, or allocation evidence supports it. If CPU is saturated with low RSS and negligible faults, reject “buy more RAM” as the next optimization. |
| H3 | Repeated environment traversal and rebuilding is the dominant algorithmic cost on real certificates. | `PHASE6_PERF.md` has callgrind evidence on synthetic shapes; the ALU residual has a large term count, but no exact O2 profile for its specialization. | Use a short, bounded profile on a small shape ladder and, when permitted, a short non-stopping sample of an in-flight run. Preserve compiler, source, certificate, and binary hashes. Compare hot functions and scaling against the existing callgrind results. | A representation change proceeds only when the measured hotspot and predicted complexity reduction agree. Test one representation change at a time. |
| H4 | Native `-O2` materially improves this path. | A controlled O0/O2 microbenchmark exists; the real O0 and O2 ALU runs differ in source snapshots, so their ratio is confounded. | Build identical source and generated C in separate O0 and O2 roots, applying the flag to every generated object and the final link. Run the same short fixtures, then one real-design run only if the short result justifies its cost. | Attribute improvement to compiler flags only when source, certificate, generated code, toolchain, budgets, and checks match. |
| H5 | Flops or sequential state make specialization substantially slower. | The 17-hour ALU has no flops. The simulator fixtures cover sequential behavior, but do not establish real sequential specialization cost. | First measure existing sequential fixtures at small sizes. Then choose a supported real sequential certificate whose reset/enable metadata passes validation. Record node/source/flop counts, index/dependency shape, stage times, and the same semantic checks. | Do not extrapolate from flop count alone. Compare designs with similar combinational structure and vary state size separately where possible. |
| H6 | The specialized simulator can match a hand-written Lean simulator’s run-time cost. | The ALU’s checked `stepOf` path was slightly slower than `interpretDesign` in one run; the wrappers differ and one sample is not decisive. No hand-written simulator baseline is recorded. | Implement or identify the manual simulator path, pin the same certificate and input/state schema, then compare both paths on identical warm-up and repeated input/trace workloads. Check outputs and next state against `interpretDesign` before timing. | Report compile/specialization time separately from steady-state per-step time. Do not claim competitiveness without a direct baseline and repeatable measurements. |

## Experiment sequence

1. **Close current runs.** Preserve the two original O0 binaries and logs until
   they exit. Record their actual results, source/build provenance, exit code,
   stage times, RSS, and checks. Do not relaunch the 17-hour O2 run merely to
   get another copy of the same evidence.
2. **Make a compact baseline table.** For each completed CVA6 or CORE-ET run,
   record certificate hash, design shape, source revision, dirty/untracked
   source hashes, binary/object hashes, toolchain, flags, fuel, wall and CPU
   time, peak RSS, residual terms, checker bound, validation coverage, and
   proof status. Keep native execution, theorem coverage, and kernel-certified
   concrete specialization as separate columns.
3. **Profile the cause at bounded scale.** Use existing small fixtures and
   callgrind/perf methods already documented in `PHASE6_PERF.md`. Prefer
   representative sizes that finish quickly; record the collection overhead
   and avoid profiling tools that suspend or perturb the long-running jobs.
   Compare cost, not only function-call counts. Keep inclusive and self-time
   accounting explicit.
4. **Separate algorithmic and compiler effects.** Test one data-structure or
   representation candidate at a time against the same fixture and reference.
   Separately test O0/O2 with identical sources and generated objects. Do not
   compare runs whose interpreter, source snapshot, build flags, or acceptance
   checks differ as if one factor caused the result.
5. **Test memory only when counters justify it.** Gather RSS, faults, swap,
   and supported Lean runtime allocation/GC data first. If they indicate
   collection or paging pressure, run a bounded, isolated heap-setting
   comparison. Otherwise focus effort on CPU complexity and data movement.
6. **Measure sequential designs deliberately.** Use the established tiny
   sequential fixture for fast correctness regression, then a supported real
   sequential certificate. Include reset, enable, hold, changing inputs, and a
   threaded trace with distinct states. A finite trace is a regression check,
   not universal proof of sequential equivalence.
7. **Establish the manual baseline.** Run the manual simulator and projected
   simulator against the same certificate and exact stimuli. Measure compile
   time, specialization time, steady-state cycle time, and memory separately;
   repeat enough times to report a median and spread. If the manual simulator
   is not available in this branch, record that blocker and specify the
   smallest adapter needed instead of substituting `interpretDesign` silently.
8. **Promote only proved changes.** A fast diagnostic fork can select the
   next experiment, but production uses the verified path only after its
   representation/specialization theorem and end-to-end simulator theorem
   cover the change. Keep the old path and regression fixtures until the
   promoted path passes the same checks.

## Required experiment record

Each experiment gets a unique ID and a short result entry in `PHASE6_PERF.md`
or a linked artifact. Record:

- hypothesis and predicted observation before running;
- exact command, certificate SHA256, source revision and dirty/untracked
  source list, toolchain, all compile/link flags, executable/object hashes,
  and specialization/checker fuel;
- hardware/OS context relevant to the run, process CPU and elapsed time, peak
  RSS, page faults, swap, and supported allocation/GC counters;
- separate stage timings, residual term count/shape, checker bound, and any
  runtime slot/dependency representation counts;
- semantic checks (reference seeds, reset/enable/hold, threaded trace,
  control), proof status, exit status, and whether the result is native-only,
  theorem-covered conditionally, or kernel-certified for this concrete input;
- observed result, whether the prediction held, confounders, and the next
  bounded experiment.

Never turn a timeout into success or failure, a high CPU percentage into proof
of useful progress, a residual size match into program equality, or a finite
simulation into a theorem.

## Scheduling and stop conditions

Coverage toward the 30-block milestone remains first. Performance work should
use completed runs and short fixtures while the coverage queue proceeds. Avoid
another multi-hour real-design run until a short measurement predicts a
meaningful, testable improvement. Stop a candidate when it fails a semantic
check, increases residual growth, or cannot be connected to the verified
path. At the end of each performance work unit, report a measured result,
negative result, or explicit blocker; do not leave an experiment running
without an owner and next review point.

A numeric definition of “competitive” should be set after the manual simulator
baseline and representative design set are established. Until then, report
measured ratios and scaling without claiming a pass/fail competitiveness
threshold.
