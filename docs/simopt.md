# Measuring LLVM simulator performance

Measure design import, simulator setup, and execution separately. Use an optimized
LiveHD build and record the design revision, input language, test, seed, cycle count
and checkpoint settings with each result.

1. Import the RTL with `lhd compile design.sv --top TOP --emit-dir lg:lg --workdir import`.
2. Time `lhd sim lg:lg testbench.prp --setup-only --workdir work`. This lowers
   the DUT kernels and builds the testbench object. Repeat with the same workdir
   for incremental timing. After a source edit, compare the result with a fresh setup.
3. Time `lhd_simrt work/sim --seed 1 --no-checkpoint` for execution. Record the
   final program signature and cycle count as well as wall time. Run checkpoint
   generation and trace replay separately when measuring observation costs.

Use the same inputs and machine for both builds, alternate the run order, and
report the median of repeated warm measurements. See the [LLVM simulator design
notes](../todo/livehd/2sl-llvm-only-sim.md) for the state layout and width experiments.

Add `--set sim.dump_bitcode=true` to setup to write the LLVM bitcode before
optimization. Each native object has a matching `.bc` under `work/sim/`,
including `drv.tb.trace.bc` when observation is enabled. The option also works
on a warm setup. Inspect a module with `llvm-dis work/sim/drv.tb.bc -o -`
using the LLVM major version pinned in `MODULE.bazel`.
