# Icarus vs Verilator divergences (to decide)

Cases from the random-Verilog simulation fuzzer where **Icarus Verilog and
Verilator disagree** at a cycle boundary, so there is no single reference for
`lhd sim` to match. Each may be undefined Verilog, a simulator X-model
difference, or a simulator bug, which is for a person to decide.

Flow (`/var/tmp/renau/prpfuzz/vfuzz.py`): a random `module fz` that `yosys
read_verilog -sv` accepts → `lhd compile` must accept → Icarus (`iverilog
-g2012`), Verilator 5.040 (`--binary --timing --x-initial 0 --x-assign 0`) and
`lhd sim` (slop and llvm, `sim.unknown_zero=true`) run the same testbench:
reset for two cycles, then each cycle sets the inputs, rises `clock`, waits #1
and prints every output. All registers have a synchronous reset, divisors are
forced non-zero.

Each `v<seed>/` holds `fz.v` (the design), `tb.v` (the Icarus/Verilator
testbench) and `divergence.json` (cycle, output, both values; newer cases also
record the `lhd_slop` / `lhd_llvm` values and the input vectors).

Reproduce one case:

    cd v<seed>
    iverilog -g2012 -o tb.vvp tb.v fz.v && vvp -n tb.vvp
    verilator --binary --timing -Wno-fatal --x-initial 0 --x-assign 0 --top-module tb tb.v fz.v -o vsim && ./obj_dir/vsim

When the two agree and `lhd sim` does not, the case is an lhd bug and is fixed,
not filed here.

## Races (`race_*/`)

A second kind of case: Icarus and Verilator AGREE, `lhd sim` differs, and the
design is a zero-delay race, so neither answer is the hardware one by
construction. Each `race_*/` directory has a README with the table, a minimal
`fz.v`/`tb.v`, and the reduced fuzz case that found it; the fuzzer no longer
generates the pattern.

- `race_latch_opens_on_sampling_edge`: a posedge flop reads a latch that opens
  on that same rising edge (simulators: new value; lhd/hardware: held value).
- `race_latch_enable_changes_on_opening_edge`: a clock-high latch's enable reads
  a register that changes on the opening edge (simulators: a zero-width open
  pulse captures; lhd: glitch-free gate, no capture).

The latch/ICG generator (`VLATCH=1`) keeps one clock: latches are enabled by a
clock level AND an extra enable and reset to 0; gated clocks are latch-based
ICGs. lhd's latch contract (rule C) rejects two simultaneously-transparent
latches with a combinational path between them, so the generator only lets a
latch read latches of the other phase.
