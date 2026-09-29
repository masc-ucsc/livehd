// Verilator twin of inou/prp/tests/sim/hier_gate_port.prp.
//
// THE POINT OF THIS FILE. `lhd sim` REFUSES the fixture with
// `gated-clock-unsupported`: the gate and the state element it clocks live in
// DIFFERENT definitions, and cgen.sim resolves clock cones on one graph at a
// time. The design itself is ordinary — this driver runs the Verilog LiveHD
// itself emits for the SAME source and gets the fixture's golden values, which
// is what makes the refusal a tool limitation rather than a bad testbench.
//
// WHY THE CLOCK IS DRIVEN BY HAND. In `lhd sim` one `step` is ONE event. Here
// the gated clock is real logic: `gp = clk & en` only produces a rising edge in
// a cycle where `en` was already high when `clk` rose, so the driver must
// present inputs while the clock is LOW, eval, drive the RISE, eval, then drive
// the FALL and eval again. Collapsing that into a single eval() is precisely the
// approximation that makes a gate look like dead code.
//
// Enables move only while the clock is low — an enable rise racing the clock
// rise is the glitch hazard a real ICG interposes a latch for, and its outcome
// is legitimately model dependent (icg_enable_sampling.prp covers the latched
// form).

#include <cstdint>
#include <cstdio>

#include "Vhgp.h"
#include "verilated.h"

namespace {

int failures = 0;

void check(int cycle, const char* what, uint32_t got, uint32_t want) {
  if (got != want) {
    std::printf("MISMATCH cycle %d: %s = %u, expected %u\n", cycle, what, got, want);
    ++failures;
  }
}

// One full period: inputs are already presented (the caller drove them while
// clk was low), then the RISE, then the FALL. Both edges are evaluated.
void advance_clock(Vhgp* dut) {
  dut->clk = 1;
  dut->eval();
  dut->clk = 0;
  dut->eval();
}

}  // namespace

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Vhgp dut;

  // Power-on: hold reset through one period with BOTH gates open, so the gated
  // flops actually see the edge that resets them. A gated flop held in reset
  // with its gate closed keeps whatever it powered up with.
  dut.clk   = 0;
  dut.reset = 1;
  dut.en    = 1;
  dut.d     = 0;
  dut.eval();
  advance_clock(&dut);
  dut.reset = 0;

  // The fixture's schedule, cycle for cycle:
  //   c0,c1 gate OPEN  -> load;  c2,c3 CLOSED -> hold;  c4 reopens -> load.
  const uint32_t dv[6]     = {5, 6, 99, 77, 88, 0};
  const bool     env[6]    = {true, true, false, false, true, false};
  const uint32_t golden[6] = {5, 6, 6, 6, 88, 88};

  for (int c = 0; c < 5; ++c) {
    dut.en = env[c];
    dut.d  = dv[c];
    dut.eval();  // settle the combinational cone while the clock is still low
    advance_clock(&dut);

    check(c, "qdown", dut.qdown, golden[c]);
    check(c, "qup", dut.qup, golden[c]);
    check(c, "ctl", dut.ctl, dv[c]);  // CONTROL: ungated reference clock
  }

  dut.final();

  if (failures != 0) {
    std::printf("FAIL hier_gate_port: %d mismatch(es) under verilator\n", failures);
    return 1;
  }
  std::printf("PASS hier_gate_port: a gate crossing a module boundary in BOTH "
              "directions gates its state correctly (verilator)\n");
  return 0;
}
