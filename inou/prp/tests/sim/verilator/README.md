# Verilator twins for `tests/sim/` fixtures

A `.prp` fixture in `../` that carries

```
:verilator: <name>_vtb.cpp
:vtop: <emitted top module>
```

gets a second bazel target, `prp-vsim-<name>`, alongside its `prp-sim-<name>`.
The two run the **same source** two ways:

| target | path | what it proves |
|---|---|---|
| `prp-sim-<name>` | `lhd sim` → `inou.cgen.sim` → Slop C++ driver | LiveHD's own simulator |
| `prp-vsim-<name>` | `lhd compile --emit verilog` → Verilator → this C++ twin | an independent event-driven simulator |

**The two are allowed to disagree — that is the point.** `hier_gate_port` is in
`_SIM_FIXME` because `lhd sim` refuses it with `gated-clock-unsupported`; its
`prp-vsim` target passes, which is what turns "LiveHD says no" into "LiveHD
cannot do this and the design is fine".

## Why the driver is hand written

It cannot be generated from the fixture's `test` block by `lhd sim`'s driver
generator, and the reason is structural rather than a matter of effort:
`lhd sim --setup-only` runs `inou.cgen.sim` **before** it writes `drv.cpp`, so
for exactly the fixtures worth a differential no driver is ever produced. The
verilator path therefore never touches `cgen.sim` at all — it goes through
`inou.cgen.verilog`.

## Why every twin drives the clock by hand

In `lhd sim` one `step` is ONE event. In an event-driven simulator a period is
two, and a design with negedge state, a latch, or an active-low clock gate is
only correct if the driver performs both:

```cpp
void advance_clock(Vtop* dut) {
  dut->clk = 1;  dut->eval();   // the RISE
  dut->clk = 0;  dut->eval();   // the FALL
}
```

Inputs are presented **before** `advance_clock`, while the clock is still low,
and read **after** it. In `chained_clock_gates_mixed_phase_vtb.cpp` the fall eval
is load-bearing: cell A's enable latch closes at the rise and cell B's at the
fall, so a single-eval driver never closes B's latch and `qn` never moves.

The clock port is named `clock` when the fixture's `reg`s declare no explicit
`clock_pin`, and whatever the fixture named it otherwise (`clk` in the gate
fixtures, which take it as a real input). Every emitted module also has `reset`;
hold it through one full period at power-on, and with any clock GATE **open**, or
the gated flops never see the edge that resets them.

## Skipping

Verilator is an outside point of comparison, not something this repo ships. A
machine without it SKIPs (and says so) rather than failing. `.bazelrc` passes
`$VERILATOR` through the test sandbox because bazel's test `PATH` does not carry
homebrew/apt install prefixes.
