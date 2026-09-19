# `lhd sim` optimization handoff

This is the handoff for continuing the Minion/DINO simulator optimization work
on a quieter Linux machine with hardware performance counters. Paths and
commands are relative to the `livehd` and sibling `lhdsuite` repositories so
the checkout location can change.

The main objective is to reduce the steady-state instruction count of the
generated Minion simulator and close the remaining gap with Verilator without
regressing correctness, host compile time, or maintainability. Minion is the
main stress case; DINO is the smaller guard against optimizing only one graph
shape.

## 1. Transfer snapshot

At the time of this handoff:

- `livehd` HEAD is `ecefa3da0` (`another pass over simulation`).
- `origin/master` is behind this local simulator work.
- The HLOP pin in `MODULE.bazel` is
  `ec14d40406cbcced7d10ad6bc8b47f1dbf1ec79b`. This is the upstream version
  with the optimized contiguous-range `get_mask_op_opt` implementation. There
  is no required local HLOP patch.
- The simulator has additional **uncommitted** changes in:
  - `inou/cgen/cgen_sim.cpp`
  - `inou/cgen/sim_color_plan.cpp`
- Other dirty/staged files are concurrent Slang, 2-D-array, uPass, and LEC
  work. They are not simulator experiments and must not be reverted while
  isolating simulator patches. Start by recording:

```bash
git status --short
git diff -- inou/cgen/cgen_sim.cpp inou/cgen/sim_color_plan.cpp
git diff --cached --name-only
git diff --binary -- inou/cgen/cgen_sim.cpp inou/cgen/sim_color_plan.cpp \
  > simopt-current-simulator.patch
```

That patch is only a transfer aid for the two simulator files. Transfer the
rest of the dirty/staged work separately; do not use the patch as a reason to
reset the shared tree.

The last register-boundary experiment described below was fully removed. No
`LIVEHD_SIM_REGISTER_REGION*`, `register_region`, or
`crosses_register_boundary` hook should exist in the transferred source.

## 2. Simulator model and measurement contract

The color plan partitions the occurrence graph into acyclic regions. In the
serial simulator, a scheduled color is called at most once in its execution
slot; colors are not repeatedly evaluated to reach a fixed point. Dirty state
is an inter-cycle activation cache: it skips a color when none of its inputs
changed. It is not needed to make an acyclic region converge.

Minion exercises all of the awkward cases together:

- positive-edge and negative-edge registers;
- latches;
- multiple execution/commit slots;
- extensive clock gating;
- large repeated module hierarchies;
- a large direct color-boundary ABI.

Clock gates are structurally folded into state-update enables where legal. A
successful setup prints diagnostics such as ``clock-gate-inlined``. The
generated state-commit path then has to make those enables cheap enough that a
mostly gated design does not scan all state every cycle.

`lhd sim` is a two-state runtime. Unknown LGraph bits are resolved at
initialization (randomly, or zero with `--set sim.init_zero=true`). Minion must
use `sim.init_zero=true` when compared with Verilator's two-state zero
initialization.

For performance measurements:

- build `lhd` with `bazel build -c opt`; the `lhd` CLI itself has no `-c opt`
  switch;
- use the Verilog input path for the apples-to-apples Verilator comparison;
- disable VCD in the timed build;
- run the generated `drv.bin` directly after the host compile;
- use 100,000 cycles for Minion and 4,000,000 cycles for DINO;
- keep the exact same LGraph, testbench, compiler, and arguments across an A/B
  simulator experiment;
- pin the tune vector on BOTH the setup and the run-only command:
  `--set sim.tune.profile=off` (the built-in defaults) or
  `--set sim.tune.file=F`. Under the default `sim.tune.profile=auto` a
  persistent workdir profiles its runs (zero-filled `?`, no checkpoints) and a
  later setup may regenerate the color root with a trial vector (§13);
- use retired instructions as the primary low-noise metric, cycles/IPC as the
  next metric, and elapsed time only on an otherwise idle machine;
- run samples serially, not concurrently;
- correctness is a gate, not another metric.

Checkpoint/VCD/query support is useful but should not distort a pure runtime
profile. The official benchmark tests use their normal public defaults. For a
lean hotspot experiment, generate both sides with
`--set sim.checkpoint=false`, run with `--no-checkpoint`, and state clearly that
this is the lean mode rather than the public default.

## 3. What is implemented and retained

### 3.1 Acyclic color runtime

The large implementation is in:

- `inou/cgen/sim_color_plan.cpp`
- `inou/cgen/sim_color_plan.hpp`
- `inou/cgen/cgen_sim.cpp`

It provides occurrence-wide versioning, execution slots, direct boundary
storage, deterministic topological scheduling, serial dirty activation,
shared color kernels, separate state-current/state-pending storage, and
checkpoint/VCD/query integration.

The generated plan report is the first structural diagnostic:

```bash
rg '^counts ' simopt_runs/minion/SW/sim/minion_top.color-plan.txt
```

Important counts are `version-sites`, `colors`, `boundary-slots`,
`boundary-bits`, `kernel-classes`, and `kernel-reuses`.

### 3.2 HLOP range operations and packed-array lowering

The simulator no longer lowers constant contiguous bit extraction to a large
number of shifts. `inou/cgen/cgen_sim.cpp` calls HLOP's optimized
`get_mask_op_opt(value, lo, hi)` range API. Contiguous set masks use
`set_mask_op_opt(lo, hi, value)` or `clear_mask_op_opt(lo, hi)`; full-range
replacement forwards the value directly. The ranges are half-open `[lo, hi)`.

Packed-array SROA (splitting an array into per-element leaves) was REMOVED in
2026-08. It fired on very little -- 23 register arrays across all of minion,
10 across all of XiangShan Backend, zero in dino/cva6/xs_alu/xs_renametable --
and each split leaf had to carry six `sroa_*` provenance attributes from the
slang reader through LNAST and upass into `pass/semdiff`, whose state pairing
then undid the split to recover the original register name. That collapse also
punched a hole in LEC: every leaf shared one compare-point key, so all but the
first obligation was dropped. Packed arrays now lower as a flat bus or, when
the storage classifier says so, as a memory cell.

### 3.3 Proven-unsigned storage

The generated simulator uses `Slop_u<W>` for values proven unsigned, including
direct boundary slots and memory/state paths. This avoids repeated carrier
masking while retaining ordinary `Slop<W>` for signed or unproven values. The
option `sim.slop_u=false` remains a correctness/debug fallback.

### 3.4 Generated-code partitioning and reuse

Large root evaluators are split into translation units at roughly 16K version
members. State commits are also emitted in separate parts. This was needed so
GCC/Clang do not receive one enormous C++ function/file and so host compilation
can use parallel jobs.

Exact compatible color bodies use canonical shared kernels. This reuses code
for some repeated module occurrences, but it does not currently turn every
repeated RTL module into one evaluator. Each occurrence still has its own
version/boundary context, and only exact kernel signatures share generated
code.

Reset/activation slow paths and checkpoint-related control are marked unlikely
where appropriate. Do not put checkpoint creation or query work back on the
ordinary cycle path.

### 3.5 State commit activation

Commit flags are dense over state updates rather than sparse over every version
site. Commits are divided into 64-member shards so a gated design can skip
inactive shards.

The current **uncommitted** `cgen_sim.cpp` change replaces a bool flag plus
shard-active bookkeeping with one `uint64_t` mask per shard. Each enabled state
sets its bit; the shard commit tests the mask and clears it after committing.
This avoids clearing/scanning a second large bool array and is part of the
baseline that should be remeasured on Linux.

### 3.6 Current uncommitted color-plan changes

The current **uncommitted** `sim_color_plan.cpp` changes retain two ideas:

1. On very large plans, when a dependency makes a consumer ready, prefer the
   widest newly-ready edge within the same occurrence body. This keeps a
   producer and its local consumers adjacent for the linear coarsener.
2. Select at most one strict repeated hierarchical closure as a reuse boundary.
   The current thresholds are:
   - 2,048 to 12,000 gate equivalents;
   - at most 8 live cut slots and 128 cut bits;
   - 4 to 128 occurrences;
   - score at least 200.

The hierarchy selection is intentionally conservative. Broad module-boundary
isolation creates too many colors and boundary values.

## 4. Results already observed

Wall-clock measurements from the final macOS session were contaminated by
heavy machine load. Treat only correctness, structural counts, and retired
instruction comparisons as useful evidence. Linux numbers must be collected
again.

### 4.1 Correctness workloads

The current Verilog benchmarks passed:

- Minion, 100K cycles:
  `retired=41171 last_pc=12, active through cycle 100000`.
- DINO, 4M cycles:
  `x2=100 -x3=102, done at cycle 506, IPC=602`.

The latest wall-time observation still put Minion roughly 2.5x behind
Verilator, but this must be remeasured on Linux. It is not a reliable transfer
baseline.

### 4.2 macOS retired-instruction baseline

The macOS counter was `/usr/bin/time -lp`. These absolute counts will not match
Linux `perf`, but their relative A/B behavior was stable.

- Exact-input Minion baseline samples: 12.735B and 12.773B instructions for
  100K cycles.
- DINO median: 15.312B instructions for 4M cycles.

Because concurrent Slang work changed six Minion version sites during an early
attempt, the final comparison compiled the Minion LGraph once and generated
both simulator variants from that identical graph. Continue using that method.

### 4.3 Rejected all-register-input/output module regions

The attempted policy was:

1. find acyclic module occurrences whose live inputs all come from registers or
   top IO, or whose live outputs all go to registers or top IO;
2. keep the outermost same-property parent;
3. schedule input-qualified regions first, ordinary regions next, and
   output-qualified next-state regions last;
4. keep persistent Q/state-read versions outside output-late regions;
5. prevent coarsening across each selected module boundary.

On the exact same Minion LGraph it found 20 input regions and 84 output regions,
covering about 114K gate equivalents. The result was clearly worse:

| metric | baseline | experiment | change |
| --- | ---: | ---: | ---: |
| retired instructions, two-sample mean | 12.754B | 14.038B | **+10.1%** |
| colors | 210 | 617 | +194% |
| boundary slots | 37,715 | 42,921 | +13.8% |
| boundary bits | 426,871 | 447,004 | +4.7% |
| kernel classes | 125 | 423 | +238% |
| kernel reuses | 85 | 194 | +128% |

Both variants were correct, but the additional reuse did not compensate for
the extra unique kernels, boundaries, and dispatch. The entire experiment and
its temporary environment switches were removed. The post-revert color plan
was byte-identical to the A/B baseline.

### 4.4 Other experiments that should not be repeated unchanged

- Disabling dirty tracking/branching did not explain the Minion gap.
- A special clock-gate cone split, including a minimum-size-32 heuristic, added
  planning/counting cost without a useful speedup and was removed. Clock gating
  is currently exploited through folded enables and state-commit activation,
  not separate cone colors.
- A general direct-register-update attempt did not actually eliminate the
  compute/pending/commit path and was reverted. A future version needs a real
  liveness proof that every old-Q consumer has run before overwriting Q.
- Indiscriminate human-module isolation and several simple boundary shifts
  increased colors/boundary ABI or did not improve runtime. The strict single
  hierarchical candidate and local producer affinity are the surviving forms.
- Coarse support-shadow/memoization and indiscriminate trailing-settle
  suppression were either slower or incorrect in earlier work.
- Redundant zero-extension-chain cleanup was instruction-count neutral; do not
  assume prettier generated expressions reduce runtime without counters.
- The generated C++ was once too large for practical GCC compilation. Splitting
  evaluator/commit code across files fixed the immediate problem. Compiler
  choice still needs a controlled Linux comparison.

## 5. Build and correctness tests

From the `livehd` root:

```bash
git rev-parse HEAD
bazel --version
clang++ --version
g++ --version
verilator --version
perf --version
ninja --version
lscpu

bazel build -c opt //lhd:all

bazel test -c opt \
  //inou/cgen:sim_color_plan_test \
  //lhd/tests:sim_color_staged_flat_test \
  //lhd/tests:sim_color_memory_test \
  //lhd/tests:sim_color_conditional_test \
  //lhd/tests:sim_color_kernel_reuse_test \
  //lhd/tests:lhd_sim_checkpoint_test \
  //lhd/tests:lhd_setmask_bitread_test \
  //lhd/tests:lhd_getmask_range_test \
  --test_output=errors

git diff --check
```

For a final change, also run the full repository gate when the machine is
available:

```bash
bazel test //...
```

Do not edit a test or benchmark whose filename contains `contract`; fix the
implementation instead.

## 6. Authoritative benchmark targets

From `livehd`, build first, then run the sibling benchmark targets:

```bash
bazel build -c opt //lhd:all
cd ../lhdsuite

bazel test //bench:minion_sim_verilog --test_output=all --cache_test_results=no
bazel test //bench:dino_sim_verilog --test_output=all --cache_test_results=no

bazel test //bench:minion_sim_verilator --test_output=all --cache_test_results=no
bazel test //bench:dino_sim_verilator --test_output=all --cache_test_results=no

bazel run //bench:show -- --core minion
bazel run //bench:show -- --core dino
```

Before running, confirm `../lhdsuite/MODULE.bazel` still has the
`local_path_override(module_name = "livehd", path = "../livehd")`. Otherwise
the suite may benchmark a registry/pinned LiveHD instead of the transferred
working tree.

The benchmark definitions and cycle counts are in `bench/defs.bzl`; shared
measurement logic is in `bench/sim.sh` and `bench/sim_verilator.sh`. Every
target starts from a fresh workdir, so the `sim.tune` learning loop never
converges there: a core that wants a tuned vector pins it through its
`sim_sets` (§13.6).

The Verilator benchmark is single-threaded simulation unless `--threads N` is
explicitly passed to Verilator. `make -j$(nproc)` only parallelizes the host
compile; it does not make the generated simulation multithreaded.

## 7. Retain a generated LHD binary for Linux `perf`

Bazel tests clean their sandbox, so use explicit relative output directories
when the generated binary must survive. From the `lhdsuite` root:

```bash
export LHD=../livehd/bazel-bin/lhd/lhd
export RUNFILES_DIR="${LHD}.runfiles"
export RUN_ROOT=simopt_runs/baseline
mkdir -p "${RUN_ROOT}/minion" "${RUN_ROOT}/dino"
```

Every `lhd sim` below pins `--set sim.tune.profile=off`, so the retained
binary is the built-in default vector and no run is profiled. To measure a
tuned vector instead, replace it with `--set sim.tune.file=F` (§13.6). The
direct `drv.bin` runs need nothing: a driver never profiles unless it is given
`--set sim.tune.profile=on`.

### 7.1 Minion

```bash
"${LHD}" compile verilog --top minion_top \
  --emit-dir "lg:${RUN_ROOT}/minion/lg" \
  --workdir "${RUN_ROOT}/minion/compile" -- \
  -F minion/verilog/filelist.f -DSYNTHESIS \
  --relax-enum-conversions --allow-use-before-declare

"${LHD}" sim "lg:${RUN_ROOT}/minion/lg" minion/sim/minion_prog_tb.prp \
  --setup-only --set sim.vcd=false --set sim.init_zero=true \
  --set sim.tune.profile=off --workdir "${RUN_ROOT}/minion/SW"

"${LHD}" sim "lg:${RUN_ROOT}/minion/lg" minion/sim/minion_prog_tb.prp \
  --run-only --arg cycles=100000 --set sim.ninja=false \
  --set sim.init_zero=true --set sim.tune.profile=off \
  --workdir "${RUN_ROOT}/minion/SW"

"${RUN_ROOT}/minion/SW/sim/drv.bin" --cycles 100000 \
  --result-json "${RUN_ROOT}/minion/SW/sim/direct-result.json" \
  --no-checkpoint
```

### 7.2 DINO

```bash
"${LHD}" compile verilog --top PipelinedDualIssueCPU \
  --emit-dir "lg:${RUN_ROOT}/dino/lg" \
  --workdir "${RUN_ROOT}/dino/compile" -- \
  -F dino/verilog/filelist.f -DSYNTHESIS

"${LHD}" sim "lg:${RUN_ROOT}/dino/lg" dino/sim/dino_prog_tb.prp \
  --setup-only --set sim.vcd=false --set sim.tune.profile=off \
  --workdir "${RUN_ROOT}/dino/SW"

"${LHD}" sim "lg:${RUN_ROOT}/dino/lg" dino/sim/dino_prog_tb.prp \
  --run-only --arg cycles=4000000 --set sim.ninja=false \
  --set sim.tune.profile=off --workdir "${RUN_ROOT}/dino/SW"

"${RUN_ROOT}/dino/SW/sim/drv.bin" --cycles 4000000 \
  --result-json "${RUN_ROOT}/dino/SW/sim/direct-result.json" \
  --no-checkpoint
```

If `lhd` cannot find `slop.hpp` or `iassert.hpp`, keep `RUNFILES_DIR` exported
or explicitly point `sim.hlop_dir`/`sim.iassert_dir` at the sibling checkouts.

## 8. Linux `perf` procedure

Pin one otherwise idle CPU. Start with a small non-multiplexed counter set:

```bash
export CPU=2
export BIN="${RUN_ROOT}/minion/SW/sim/drv.bin"

taskset -c "${CPU}" perf stat -r 5 \
  -e cycles,instructions,branches,branch-misses \
  -- "${BIN}" --cycles 100000 --no-checkpoint
```

Then collect cache/TLB counters in a separate run so the PMU does not
multiplex too many events:

```bash
taskset -c "${CPU}" perf stat -r 5 \
  -e cache-references,cache-misses,dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses \
  -- "${BIN}" --cycles 100000 --no-checkpoint
```

Check the `perf stat` percentage-running column. If an event is unsupported or
multiplexed, remove it and run a smaller group. Record:

- instructions and instructions/cycle;
- branch misses per thousand branches;
- cache and TLB misses per thousand instructions;
- binary text size;
- generated color-plan counts;
- host compile time with the same compiler/job count.

For a startup-corrected instruction rate, collect two cycle counts and subtract:

```text
instructions_per_cycle = (instructions(N2) - instructions(N1)) / (N2 - N1)
```

Use an active interval for both points. Minion remains active throughout its
100K workload. DINO finishes useful work near cycle 506 and then spins, so its
delta measures steady-state spin throughput rather than program execution.

### 8.1 Hotspot profile

```bash
taskset -c "${CPU}" perf record -F 999 -g --call-graph dwarf \
  -o "${RUN_ROOT}/minion/perf.data" -- \
  "${BIN}" --cycles 100000 --no-checkpoint

perf report --stdio -i "${RUN_ROOT}/minion/perf.data"
```

Generated functions of interest include the color evaluator parts, shared
color kernels, state-commit parts, boundary refresh, and the testbench driver.
Map them back through:

- `${RUN_ROOT}/minion/SW/sim/minion_top.color-plan.txt`
- `${RUN_ROOT}/minion/SW/sim/minion_top.color-runtime.hpp`
- `${RUN_ROOT}/minion/SW/sim/minion_top.color-kernel-*.cpp`
- `${RUN_ROOT}/minion/SW/sim/minion_top.color-commit-*.cpp`
- `${RUN_ROOT}/minion/SW/sim/build.ninja`

The optimized binary is not stripped, so function-level symbols should be
available. If source-line annotation is required, make a separate profiling
build with `-g -fno-omit-frame-pointer` added to the generated `build.ninja`,
rebuild that temporary simulation directory, and do not use its compile time
as the normal benchmark result.

Useful footprint commands are:

```bash
size "${BIN}"
nm -S --size-sort "${BIN}" | tail -n 100
wc -c "${RUN_ROOT}/minion/SW/sim/"*.cpp
```

## 9. Retain a Verilator binary for the same counters

The official Bazel target is the correctness oracle. For a persistent binary,
from the `lhdsuite` root:

```bash
export VROOT=simopt_runs/verilator/minion
mkdir -p "${VROOT}"

verilator --cc --exe --Mdir "${VROOT}/vobj" \
  --top-module minion_top -Wno-fatal -DSYNTHESIS \
  -Iminion/verilog -F minion/verilog/filelist.f \
  minion/sim/minion_prog_tb_verilator.cpp

make -C "${VROOT}/vobj" -f Vminion_top.mk \
  -j"$(nproc)" Vminion_top

taskset -c "${CPU}" perf stat -r 5 \
  -e cycles,instructions,branches,branch-misses -- \
  "${VROOT}/vobj/Vminion_top" --cycles 100000
```

For an explicit one-thread build, add `--threads 1` to the Verilator command.
Do not compare a `--threads N` Verilator build with the serial LHD runtime and
call the ratio single-threaded.

Repeat the same compiler matrix on both simulators:

- LHD generated host C++: set `CXX=clang++` or `CXX=g++` before
  `lhd sim --run-only`.
- Verilator generated C++: build once with `make CXX=clang++ ...` and once with
  `make CXX=g++ ...`, using separate object directories.

Keep compiler version, optimization level, and link mode in the result. The
earlier macOS GCC/Clang check did not remove the Minion gap, but Linux code
generation and perf attribution may explain where the instruction difference
comes from.

## 10. Safe A/B experiment loop

Try one technique at a time.

1. Record the exact `livehd`, `lhdsuite`, and HLOP revisions, dirty diffs,
   compiler versions, CPU model, and kernel version.
2. Compile the Verilog design to one shared `lg:` directory once.
3. Generate the baseline simulator from that LGraph into `SW.base`.
4. Apply only one simulator technique, rebuild `lhd`, and generate from the
   same LGraph into `SW.exp`.
5. Verify that both plan reports have identical site/version/value-use input
   counts. Differences in colors/boundaries are expected; differences in
   version sites mean the input changed and invalidate the A/B test.
6. Compile and run both binaries with identical options, including the same
   pinned tune vector: check that both envelopes report the same
   `sim_tune.applied.vector` (a vector difference is a second, unattributed
   change).
7. Gate on architectural correctness before profiling.
8. Collect at least five serial `perf stat` samples, plus compile time and
   structural/code-size counts.
9. Revert a failed technique completely. Keep no hidden environment switch for
   a clearly worse option.
10. Rebuild the baseline after unrelated Slang/uPass changes; do not compare
    against an old generated graph or old binary.

Acceptance policy used so far:

- keep a clear simulation improvement when correctness and compile cost remain
  acceptable;
- keep a compile-time/code-cleanliness improvement if simulation is within
  roughly 2%;
- discard a simulation slowdown over 2% unless the compile-time or cleanliness
  improvement is exceptional and explicitly justified;
- preserve options only when they serve real debugging, VCD, or checkpoint use;
- if an experiment fails structurally or semantically, move the idea to the end
  of the list rather than weakening correctness tests.

## 11. Recommended next experiments

Profile first. The order below depends on what Linux `perf` reports.

### 11.1 Attribute the instruction gap by generated function

Measure LHD and Verilator instructions on the same Minion workload, then break
LHD samples into:

- color evaluator/shared-kernel arithmetic;
- boundary loads/stores and dirty propagation;
- rise/fall state commit;
- reset/activation checks;
- testbench/runtime overhead.

This is the highest-priority missing evidence. Code footprint and i-cache
locality matter, but the current problem first appears to be excess retired
instructions, not merely low IPC.

### 11.2 Improve active commit-shard execution

If commit parts are hot, the current mask still emits a test for each member in
an active 64-state shard. Try iterating only set bits with
`std::countr_zero(mask)`/`mask &= mask - 1` and dispatching the selected commit.
Measure the switch/dispatch cost; do not assume sparse masks automatically win.

Also sweep the 64-state shard size one value at a time. Smaller shards skip
more gated state but add calls/code; larger shards do the opposite.

### 11.3 Reduce boundary outputs without broad module isolation

The current topological affinity prefers the widest newly-ready local edge.
Possible bounded refinements are:

- choose among ready local consumers by incremental live-out cut, not only edge
  width;
- shift a completed color boundary over a small fixed window to minimize live
  boundary values;
- merge adjacent colors with many shared edges when the merged set remains
  acyclic and below the size target.

Avoid a global min-cut and avoid counting the whole graph repeatedly. Planning
must remain near-linear on very large designs.

### 11.4 Carefully extend hierarchical kernel reuse

The current planner selects one strict repeated hierarchical definition. A
next experiment could select multiple non-overlapping candidates, ordered by
duplicated gate-equivalents per cut bit. Add only one candidate at a time and
stop when colors, boundary slots, or unique kernel classes grow faster than
reuse.

Do not retry the all-register-input/output policy unchanged; its exact result
is recorded in section 4.3.

### 11.5 Revisit direct state updates only if commit dominates

The LGraph edge out of a register identifies old-Q consumers. In principle Q
can be overwritten after all of those consumers execute, avoiding separate
pending storage and a later commit. The previous generic implementation did
not establish this condition and therefore did not remove the update step.

A viable retry needs an explicit per-slot proof:

- every old-Q edge has been consumed;
- the state has one legal writer for the slot;
- memory write conflicts retain deterministic ordering;
- posedge, negedge, latch, checkpoint, and observation semantics remain intact.

Do not pursue this before `perf` shows state staging/commit is a major Minion
hotspot.

### 11.6 Compiler comparison and code footprint

Compile the same generated sources with GCC and Clang and compare both LHD and
Verilator. Record instructions, cycles, text size, and the largest symbols.
This can distinguish generator overhead from a compiler-specific layout or
inlining decision.

Repeated RTL instances still often have occurrence-specific evaluator code.
Use `nm --size-sort` and the color-plan kernel-class/reuse counts to quantify
how much, rather than inferring it from source file count alone.

## 12. Things to preserve while experimenting

- The Minion and DINO benchmark assertions must remain live.
- Do not enable VCD in timed comparisons.
- Keep checkpoint/probe/query support correct even when measuring a lean build.
- Keep `sim.slop_u=false` working as a diagnostic fallback.
- Preserve positive/negative-edge and latch execution-slot ordering.
- Preserve deterministic generated plans and source bytes for the same input.
- Keep unrelated Slang/2-D-array/uPass/LEC changes intact.
- Never report a result from stale generated sources after a frontend change.

Historical `todosim.md` describes the earlier Minion correctness bring-up, but
its performance numbers predate the current color runtime and must not be used
as a baseline.

## 13. Profile-guided tuning (`sim.tune.*`)

Some simulator knobs change only speed, never a simulated value, and the best
setting depends on the design's activity, not on its size. A mostly idle design
(xs_renametable) wants dirty tracking on; a small design whose state toggles
every cycle (the LFSR-driven lhdtrack benches) wants it off, and is 2-3x slower
with it. Those knobs live under `sim.tune.*`. With a persistent `--workdir`,
`lhd sim` measures the design while it runs and picks them per design. Ruling
**I14** in [`opt_loop_incr.md`](opt_loop_incr.md) §9 states what such a
decision may and may not do: the short version is that it never changes a
result, and an explicit `--set` always wins.

### 13.1 Knobs

| key | values | default | what it controls |
|---|---|---|---|
| `sim.tune.profile` | `auto`\|`on`\|`off` | `auto` | the learning switch (§13.2) |
| `sim.tune.dirty` | `auto`\|`on`\|`off` | `auto` = on | the cross-cycle color activation cache: a color runs only when an input changed, a flop commit compares before it writes, and a fully quiescent period exits early. `true`/`false`/`1`/`0` are accepted aliases |
| `sim.tune.fence` | `auto`\|`none`\|N (0..2^20) | `auto` = `none` with dirty off, 16 with it on | fence a module used once into its own colors when it has at least N sites per interface word; `0` fences every such module. A fence pays only when dirty gating can skip what it isolates |
| `sim.tune.live_words` | `auto`\|N (1..2^20) | `auto` = 256 | per-color live-value budget, in 64-bit words |
| `sim.tune.backend` | `auto`\|`slop`\|`llvm` | `auto` = slop | color-kernel backend. `llvm` needs a clang host, and a color it cannot lower fails setup rather than falling back |
| `sim.tune.file` | PATH | "" | apply a vector written by `sim.tune.export` (§13.6) |
| `sim.tune.export` | PATH | "" | write the applied vector, with its provenance, to PATH |
| `sim.tune.profile_dir` | DIR | "" | drv.bin only: where a profiling run writes its raw run file |
| `sim.tune.profile_stride` | N (0..2^20) | 0 = auto | drv.bin only, for tests: a fixed sampling stride with no jitter |

`dirty`, `fence`, `live_words` and `backend` form the **tune vector**, printed
canonically as `tv1:d=on;f=16;lw=256;be=slop` (that string IS the
default: dirty gating on, because the large designs it exists for win with it,
and small always-toggling DUTs such as lhdtrack's pin `sim.tune.dirty=off` or
let the ladder take them to L0). The automatic ladder moves only `dirty` and
`fence` today; `live_words` and `backend` are applied from `--set` or a tune
file and are never trialed on their own.

Never tunable, and so never under `tune`: `sim.unknown_zero`, `sim.init_zero`
and `lhd.seed` (they change simulated values), `sim.vcd*` and `sim.checkpoint*`
(observability), `sim.jobs`, `sim.ninja`, `sim.hlop_dir`, `sim.iassert_dir` and
`sim.compile_only` (build plumbing), and `sim.slop_u` / `sim.debug` (validation
fallbacks).

### 13.2 Modes

- **`auto`** (the default). A workdir with no converged tune data PROFILES the
  run. When the profile predicts a gain, the next setup builds ONE better
  vector (a trial). The trial's own run is profiled too, and its verdict keeps
  the trial only if it is at least 7% faster per simulated cycle AND its
  results are byte-identical; otherwise it reverts and bans that vector. Once
  converged, `auto` reuses the decision and samples nothing.
- **`on`**. Profile this run even when the decision has converged, appending to
  the history. It can re-open a converged decision (after a testbench change,
  say). Deleting the store (§13.4) starts from scratch.
- **`off`**. Ignore the workdir's tune data and use the built-in defaults.
  Nothing is read or written; `sim.tune.file` and explicit `--set` still apply.

The tuner is active only with a user-named `--workdir` and
`lhd.incremental=true`, and never on an observation run (VCD, `--probe`,
`--break-when`, `--query`, `--restart-cycle`, `--vcd-from`, `--list-signals`).
Otherwise `auto` and `on` behave as `off`; `on` or `sim.tune.export` without a
workdir warns (`tune-disabled`). An observation run does not learn, but under
`auto`/`on` it still builds the workdir's decision (read without the lock and
without writing anything), so `--list-signals` or `--restart-cycle` reuses the
tuned tree and replays the very binary the plain runs use.

A profiling run differs from a plain one in three ways, all printed in the
envelope:

- drv.bin samples pairs of consecutive cycles (hashing the state and the root
  inputs at t and t+1) on a jittered stride calibrated to at most 1% overhead;
- unless the user set `sim.unknown_zero`, every `?` literal bit is ZERO-filled
  at run time (`--set sim.unknown_zero=true` to drv.bin), so two vectors can be
  compared byte for byte. An explicit user setting is always followed; an
  explicit `false` profiles with random fill and the verdict then decides on
  speed alone (`oracle=skipped(random-fill)`);
- it takes no checkpoints.

A run counts toward a decision (and can be a trial's baseline) only with at
least 64 sampled pairs, 10^4 post-warm-up cycles and 0.2 s of CPU. Smaller runs
are "smoke" rows: they never propose a trial, and three consecutive smoke runs
converge on the current vector (`smoke-only`). So a short test never pays a
rebuild; it pays only the zero fill of its profiled runs (at most three per
workdir). The 0.2 s floor is the BASELINE's noise floor: a trial's own run is
judged once it has 10^4 post-warm-up cycles and 20 ms of CPU, because a big win
makes the trial run short.

`I_s`, the support idleness the ladder reads, weighs each idle class of the
plan's support table by its simulation cost (`cost_flat`: the 64-bit words of
C++ its sites execute, a loop or opaque instance counting its own words only).
The envelope and the store also report the same idleness weighted by gate
equivalents (`I_ge`), by site count (`I_sites`) and by cost with loop bodies
multiplied out (`I_cost`); a driver that predates them reports `I_ge` only, and
`I_s` then falls back to it. The ladder (its window of runs, and the busy-run
) averages idleness under `cost_flat` only: a stored
run carrying `I_cost_flat` uses it, and a run recorded under another weight (a
driver before the variants, or a store kept across a model change) is ignored
by the ladder instead of being mixed in. A plan without support tables is
measured by the walker, which the ladder accepts. `support: true` means `I_s`
came from the support tables, also when every class of the root is pure wiring
(zero gate equivalents) and only the word-cost weights are non-zero.

**The ladder** (one lever per step, measured stats cycle-weighted over tests):

| step | vector | tried when |
|---|---|---|
| L1 | `d=on f=16` | the default; from L0 when idle support `I_s >= 0.45` or quiescent fraction `q >= 0.05` |
| L0 | `d=off f=none` | from any dirty-gated vector when `I_s < 0.45` and `q < 0.05` |
| L2 | `d=on f=0` | from L1 when `I_s >= 0.7` |

Every step also needs: the target not banned, a qualifying run of the incumbent
on this structure, and (under `auto`) the payoff gate. The up (L1) and down
(L0) predicates are complements for one window, so they cannot oscillate. A plan
with runtime-random memory collisions never trials. The payoff gate asks
whether the predicted gain, times the median run CPU time, times 20 future
runs, repays one root rebuild. The predicted gain is `I_s` going up (passing
outright when `I_s > 0.7`) and `1 - I_s` going down to L0 (optimistic, since
the verdict decides; passing outright in the always-toggling regime,
`I_s < 0.3` with `q < 0.01`). At most three flips per profile generation.

These thresholds are the `cm-2` model, calibrated on 25 lhdsuite + lhdtrack
design/source pairs (2026-09-18) against measured L0/L1/L2 retired instructions
per simulated cycle. Every pair where dirty gating loses measured `I_s <= 0.39`
(dino, matched_filter, the LFSR-driven lhdtrack DUTs: 1.1-3.6x MORE
instructions under L1); every pair where it wins measured `I_s >= 0.54`
(xs_alu 4x, xs_renametable 6x on Verilog/unrolled Pyrope and 1.7x at L2 on
pyrope2, minion 2-3x, xs_rob Verilog 158x). Gate-equivalent weights
misclassify xs_renametable pyrope2 and xs_rob, which is why `I_s` uses
`cost_flat`. `q` was 0 on every design.

**The verdict** compares the trial with the most recent qualifying run of the
incumbent vector on the same structure, testbench, host, seed, fill, test
selection, arguments and non-tune codegen settings (`sim.debug`, `sim.slop_u`,
runtime support: the baked codegen table minus the four vector lines). Oracle
first: every common test's end-state digest, output digest and status must
match. A mismatch is an ERROR (`sim-tune-divergence`, both vectors named), and
the vector is banned. The oracle checks EVERY run of the trial that has a
comparable baseline, however short: a miscompile that skips work, or that ends
a run-until-done testbench early, makes exactly such a run. Only the speed half
waits for a judgeable run. Then speed: the CPU-share-weighted ratio of CPU cycles
per simulated cycle must be at most 0.93. Retired instructions decide instead
when the two ratios differ by more than 0.2, or when either run spent less than
90% of its time on performance cores. The incumbent's generated tree is kept as
a clone under `<workdir>/sim_tune/`, so a revert is a directory swap, not a
rebuild. The clone is swapped back only when it simulates the same design
(scope, structure and testbench) as the tree it replaces; a clone taken before
an edit is discarded, and the next setup rebuilds the incumbent, so a
`--run-only` never runs a stale design.

When the incumbent cannot be swapped back (a filesystem without copy-on-write,
a clone of another design, a failed swap), the trial tree stays, and its
`tune_applied.json` is marked `closed` with the verdict; a diverged tree also
loses its `drv.bin`. A later `--run-only` of that tree is a usage error naming
the vector and the verdict, in every mode, so a rejected, abandoned or wrong
vector is never run and reported as the workdir's decision. A `--setup-only`
or a full `lhd sim` rebuilds the incumbent. An unclosed trial tree that is
neither the open attempt nor an accepted vector of the store is refused the
same way; with the tuner off, it runs as built with a `sim-tune-trial-tree`
warning. A `--run-only` of a tree that another design generated in a shared
workdir runs as built (source `built`), unprofiled, with a
`sim-tune-foreign-tree` warning naming both designs.

**Trial attempts.** A pending trial is applied only at a setup whose run will
profile it (for `--setup-only`, the `--run-only` that follows): never under
`auto` with explicit checkpoint settings, and never on an observation run. Each
setup that applies it is one ATTEMPT, and every attempt ends with its run, in
exactly one of:

- a verdict (accepted, rejected, divergence, or random-ineligible);
- `abandoned`: no comparable qualifying run (other arguments, test selection,
  an edit), or a later setup replaced the trial tree before it ran;
- `build-failed` / `run-failed`: cgen or the host build failed, or drv.bin
  crashed or finished without its raw run record.

A setup whose tree already holds the open trial (a repeated `--setup-only`,
or a full run after a `--setup-only`) rebuilds that same tree, a no-op for the
generation keys, and KEEPS the attempt: the run that follows judges it. Only a
setup that replaced the trial tree (another design sharing the workdir, an
`off` or observation setup) supersedes it, as a charged `abandoned`; a setup
that will not profile it (`sim.compile_only`, or explicit checkpoint settings
under `auto`) closes it without charging an attempt.

A trial that did not end in a verdict is reverted at the end of that same
invocation. The step is proposed again only after the incumbent has run again,
which gives the next attempt a baseline for the changed conditions. After two
attempts of one vector on one structure, `auto` stops proposing it and
converges (`exhausted`); an edit (a new structure) re-opens it. An explicit
`on` re-opens an exhausted vector ONCE: the attempts made under `on` get the
same budget of two, after which `on` converges `exhausted` as well. It keeps
profiling but never re-applies that trial, so an `on` loop over changing
arguments cannot rebuild the root every other run forever. A build or run
failure bans the vector only when it happens twice on the same structure, since
a bad `$CXX` or a full disk is not the vector's fault. A pending trial whose
moved knob gets pinned (`--set` or `sim.tune.file`), or whose `from` is no
longer the resolved vector, is STALE: it is closed without counting an attempt
and never applied.

### 13.3 Precedence

Per knob, highest first:

1. an explicit `--set sim.tune.X=V` or a `--config` `[sim.tune]` entry (a
   CLI `--set` overrides the `--config` entry; two CLI `--set` of one key with
   different values are a usage error); `auto` means "not set";
2. `sim.tune.file`;
3. the workdir's converged decision;
4. the built-in default.

Explicit knobs are frozen: the ladder never moves them, and a step that would
only move pinned knobs does not exist. Their runs still feed the ledger. When
`dirty` comes from (1) or (2) and `fence` does not, `fence` follows the default
rule for that `dirty` (not the store's `fence`).

### 13.4 The store

One store per compile scope:
`<workdir>/incr/scopes/sim/<scope>/tune.jsonl`. It is append-only JSONL with
`run`, `trial`, `attempt`, `verdict` and `decision` records; the last
`decision` is the workdir's default vector and everything else is the history
behind it.

- It survives `rm -rf <workdir>/sim`; delete the `.jsonl` to start fresh. A
  malformed or foreign-schema file is renamed `.bad` and ignored, and a
  trailing partial line (a crash mid-append) is ignored.
- `lhd` holds `<workdir>/.lhd_sim.lock` from setup through the host build and
  while it appends. Past 2000 records the file is compacted to a bounded set:
  the newest 32 runs, the newest decision (and the newest converged one, where
  the flip count restarts), the pending trial and its attempt, and the verdicts
  a replay still needs. Those are every ban, the accepted verdicts of this
  profile generation plus the newest per vector, every verdict on a structure a
  kept run has (the attempt budgets), the failures behind a failure ban, and the
  newest verdict per vector. Replaying the compacted file gives the same
  decision, bans, budgets and pending trial.
- Raw run files: an lhd-driven profiling run writes to
  `<workdir>/sim_tune/inbox/`; a hand-run `drv.bin --set sim.tune.profile=on`
  writes to `<workdir>/sim/tune_runs/`. The next `lhd sim` on that workdir
  ingests both, but only into the store of the design whose tree produced them
  (per DUT class structure); a file from another design sharing the workdir, or
  from a tree an edit replaced, is discarded with a note. A record is appended
  before its raw file is removed.
- Nothing measured enters a generation key. Only the resolved vector does, and
  only on the COLOR ROOT, so a flip regenerates only the root's TUs, a
  default-equal decision costs nothing, switching between `on` and `auto`
  rewrites no generated file, and no `sim.*` knob invalidates the compile
  cache. `off` means the true defaults: in a workdir whose decision differs
  from them, an `off` run regenerates the root, and so does the next `auto`
  run.

### 13.5 Reading a run

Every `lhd sim` envelope carries a `sim_tune` member: the mode, whether the
tuner was enabled (and why not), whether this run profiled and with which fill,
the `applied` vector with each knob's `source` (`default`, `store`, `file` or
`explicit`; `built` when a `--run-only` tree's provenance cannot be vouched
for: no label, another design's tree, a refused trial tree), the run's stats (`I_s` and the `weight` it used, `I_ge`,
`I_sites`, `I_cost`, `I_cost_flat`, `I`, `q`, `pairs`, `qualifies`,
`judgeable`), any trial and verdict, the rejected list, the pending trial,
`converged`, and `reproduce`: the exact `--set` list that rebuilds the applied
vector, including `sim.unknown_zero=true` when the binary zero-filled (`fill`
reports what the binary actually got: baked at setup, forwarded because the
user set it, or the implicit zero fill of a profiling run). The pretty output
prints one line such as:

```
sim.tune: applied d=on f=16 lw=256 be=slop (default) | I_s=0.12 q=0.00 pairs=812 | next setup: TRIAL d=off f=none
```

A generated simulator identifies itself: `<stem>.tune-id.cpp` bakes the
vector, a structure id and the codegen settings into `drv.bin`. `drv.bin`
accepts `--set key=value` with lhd's spellings. A codegen key
(`sim.tune.dirty|fence|live_words|backend`, `sim.slop_u`, `sim.debug`,
`sim.vcd`, `sim.vcd_fake_delay`) that differs from the baked value is an error
telling you to re-run `--setup-only`; an equal value (or `auto`) is accepted,
so one `--set` list can be passed to both setup and run.

### 13.6 Export and import

```bash
# converge in a persistent workdir (repeat until the envelope says converged)
lhd sim DUT TB --workdir P --arg cycles=N --result-json r.json
# write the decision, then pin it anywhere else
lhd sim DUT TB --workdir P --setup-only --set sim.tune.export=design.simtune.json
lhd sim DUT TB --workdir W --set sim.tune.profile=off --set sim.tune.file=design.simtune.json
```

```json
{"schema":"lhd-sim-tune-file-1","vector":"tv1:d=on;f=0;lw=256;be=slop",
 "knobs":{"dirty":"on","fence":"0","live_words":"256","backend":"slop"},
 "provenance":{"structure":"...","converged":true,
               "stats":{"I_s":0.93,"I":0.91,"q":0.00,"pairs":812},"created":"..."}}
```

The file's knobs apply with source `file`, in every mode, with or without a
workdir. A structure mismatch (the design changed since the export) is a
warning, not an error. A hand-written file may spell `dirty` as a JSON
`true`/`false` and `fence` / `live_words` as JSON integers; any other JSON type
is a config error naming the knob. A file that was read is listed in the
envelope's `inputs` and in `--depfile`, so a make/ninja flow regenerates when it
changes. This is how hermetic flows use the tuner: lhdsuite's
bazel targets and lhdtrack start from a fresh workdir on every run, so `auto`
could never converge there. They pin the vector instead: explicit knobs
(lhdsuite's xs_renametable `sim_sets`), or `sim.tune.profile=off` plus a
checked-in tune file.

### 13.7 Renamed keys

The old spellings are directed usage errors (`--set`, `--config` and the
`compile.sim.*` form alike), each with a copy-pasteable replacement:

| old | new |
|---|---|
| `sim.color_dirty=true` / `false` | `sim.tune.dirty=on` / `off` |
| `sim.fence_ratio=N` | `sim.tune.fence=N` (empty = `auto`; a ratio meant as "never fence" = `none`) |
| `sim.live_words=N` | `sim.tune.live_words=N` (`0` = `auto`) |
| `sim.backend=slop` / `llvm` | `sim.tune.backend=slop` / `llvm` |

`inou.cgen.sim` keeps its internal label names (`color_dirty`, `fence_ratio`,
`live_words`, `backend`); `lhd` always hands it concrete resolved values.

### 13.8 Measuring a tuned vector

Report setup ms, host C++ ms, exec ms and KHz (cycles / exec ms) per design and
variant, on a quiet machine, serially.

1. `bazel build -c opt //lhd:lhd`, then copy `bazel-bin/lhd/lhd` and its
   `lhd.runfiles` to a frozen directory so a rebuild mid-run cannot change the
   binary being measured.
2. Variants, each passed identically to setup AND run-only:
   - **V0** defaults: `--set sim.tune.profile=off`;
   - **V1** a hand-pinned vector, e.g. V0 plus
     `--set sim.tune.dirty=on --set sim.tune.fence=0`;
   - **V2** converged: in a persistent workdir P, repeat
     `lhd sim DUT TB --arg cycles=N --set sim.vcd=false --set sim.ninja=false --workdir P --result-json rI.json`
     until `sim_tune.converged` (cap it at 5 runs), recording each run's
     `inou.cgen.sim` and `sim.hostbuild` phases as the convergence cost; then
     export (§13.6) and measure V0 plus `--set sim.tune.file=F` in a fresh
     workdir.
3. Per variant, in a fresh workdir W:
   - setup: `lhd sim DUT TB --setup-only --set sim.vcd=false <variant> --workdir W --result-json setup.json`;
   - run: `lhd sim DUT TB --run-only --arg cycles=N --set sim.ninja=false <variant> --workdir W`,
     gated on the testbench's marker and expected checksum;
   - exec: best of 3 `W/sim/drv.bin --cycles N`; host C++ = run - exec.
4. Prefer retired instructions over wall time. Each `--result-json` test row
   carries `sim_cycles`, `cpu_ns`, `cpu_cycles`, `instructions` and
   `pcore_frac`; on Apple Silicon a loaded machine moves work to efficiency
   cores, which shifts cycles far more than the 7% trial margin.
5. Never compare a profiled run's time with an unprofiled one, and never a
   zero-filled run's checksum with a random-filled one on a design with `?`
   literals.
