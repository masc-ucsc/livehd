# Code generation

## Verilog export

Graphs containing only module instances, ordinary RAM wrappers, and fixed
wiring operations export as structural Verilog: plain input/output and wire
declarations, continuous assignments, sized bit literals and explicit bit
connections for extension, truncation, slices and concatenations. This path
avoids procedural alias blocks, signed declarations and expression casts so
limited structural readers such as OpenSTA can read mapped netlists. Module
input connections use the callee port width explicitly. Native memories still
retain their RTL wrappers; a timing-only black-box view requires the matching
Liberty memory interface.

General behavioral graphs keep the existing emitter for arithmetic, arrays,
compact loops and runtime properties that require richer HDL constructs.
The focused `//inou/cgen:structural_verilog` regression checks emitted wiring
against an independent reference using `formal.effort=test`.

## Native kernel generation

`cgen_llvm.cpp` builds relocatable LLVM objects for the simulator. The caller in
`pass/lnast_llvm` schedules the kernels and assigns state words. The testbench
and runtime share this layout. See [simulator performance](../../docs/simopt.md)
for the measurement workflow.
