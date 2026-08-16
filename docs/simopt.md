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

### 3.2 HLOP range operations and SROA-related lowering

The simulator no longer lowers constant contiguous bit extraction to a large
number of shifts. `inou/cgen/cgen_sim.cpp` calls HLOP's optimized
`get_mask_op_opt(value, lo, hi)` range API. Contiguous set masks use
`set_mask_op_opt(lo, hi, value)` or `clear_mask_op_opt(lo, hi)`; full-range
replacement forwards the value directly. The ranges are half-open `[lo, hi)`.

The packed-array/vector SROA work is already committed. It reduces array-like
values before simulation where semantics allow it, but persistent memories
remain memory cells and register names are preserved for LEC aggregation.

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
measurement logic is in `bench/sim.sh` and `bench/sim_verilator.sh`.

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

### 7.1 Minion

```bash
"${LHD}" compile verilog --top minion_top \
  --emit-dir "lg:${RUN_ROOT}/minion/lg" \
  --workdir "${RUN_ROOT}/minion/compile" -- \
  -F minion/verilog/filelist.f -DSYNTHESIS \
  --relax-enum-conversions --allow-use-before-declare

"${LHD}" sim "lg:${RUN_ROOT}/minion/lg" minion/sim/minion_prog_tb.prp \
  --setup-only --set sim.vcd=false --set sim.init_zero=true \
  --workdir "${RUN_ROOT}/minion/SW"

"${LHD}" sim "lg:${RUN_ROOT}/minion/lg" minion/sim/minion_prog_tb.prp \
  --run-only --arg cycles=100000 --set sim.ninja=false \
  --set sim.init_zero=true --workdir "${RUN_ROOT}/minion/SW"

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
  --setup-only --set sim.vcd=false \
  --workdir "${RUN_ROOT}/dino/SW"

"${LHD}" sim "lg:${RUN_ROOT}/dino/lg" dino/sim/dino_prog_tb.prp \
  --run-only --arg cycles=4000000 --set sim.ninja=false \
  --workdir "${RUN_ROOT}/dino/SW"

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
6. Compile and run both binaries with identical options.
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
