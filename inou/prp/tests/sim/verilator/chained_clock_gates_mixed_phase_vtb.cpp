// Verilator twin of inou/prp/tests/sim/chained_clock_gates_mixed_phase.prp.
//
// THE POINT OF THIS FILE. `lhd sim` refuses the fixture with
// `gated-clock-unsupported`, for the RIGHT reason: the chain resolves, but its second cell is the ACTIVE-LOW
// flavour, which gates the FALLING edge — and cgen.sim commits at the tick's
// rise, so folding it into a per-tick guard would move the commit half a period
// silently. Refusing is correct; being unable to simulate the design is the
// limitation. This driver simulates it.
//
// WHY THE TWO-EDGE EVAL IS LOAD-BEARING HERE, not a formality. Cell A's enable
// latch is transparent while `clk` is LOW and closes at the RISE. Cell B sits on
// the INVERTED gated clock, so its latch is transparent while `ga` is HIGH and
// closes at the FALL. A driver with a single eval() per cycle has no fall at
// which to close cell B's latch, and `qn` never moves. The two enables are
// sampled at DIFFERENT PHASES OF THE SAME PERIOD, which is exactly what a single
// folded `guard_before_fall` bit cannot represent.
//
// c2 is the discriminating cycle: e1 high, e2 low, so the ordinary cell commits
// (qp takes 99) while the active-low cell holds (qn stays 6). An implementation
// that conjoined both cells' enables and applied the result to both endpoints
// would hold qp at 6 too — wrong, and wrong silently.

#include <cstdint>
#include <cstdio>

#include "Vmixphase.h"
#include "verilated.h"

namespace {

int failures = 0;

void check(int cycle, const char* what, uint32_t got, uint32_t want) {
  if (got != want) {
    std::printf("MISMATCH cycle %d: %s = %u, expected %u\n", cycle, what, got, want);
    ++failures;
  }
}

// One full period, BOTH edges evaluated. The rise closes cell A's enable latch
// and clocks `fp`; the fall closes cell B's and clocks `fn`.
void advance_clock(Vmixphase* dut) {
  dut->clk = 1;
  dut->eval();
  dut->clk = 0;
  dut->eval();
}

}  // namespace

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Vmixphase dut;

  // Power-on with both cells open so the reset edge reaches the gated flops.
  dut.clk   = 0;
  dut.reset = 1;
  dut.e1    = 1;
  dut.e2    = 1;
  dut.d     = 0;
  dut.eval();
  advance_clock(&dut);
  dut.reset = 0;

  // The fixture's schedule. c2: only the ACTIVE-LOW cell closed (the
  // discriminating row). c3: only the ordinary cell closed. c5: both closed.
  const uint32_t dv[6]  = {5, 6, 99, 77, 88, 41};
  const bool     e1v[6] = {true, true, true, false, true, false};
  const bool     e2v[6] = {true, true, false, true, true, false};
  const uint32_t gp[6]  = {5, 6, 99, 99, 88, 88};
  const uint32_t gn[6]  = {5, 6, 6, 6, 88, 88};

  for (int c = 0; c < 6; ++c) {
    dut.e1 = e1v[c];
    dut.e2 = e2v[c];
    dut.d  = dv[c];
    dut.eval();  // settle while the clock is low (cell A's latch tracks e1 here)
    advance_clock(&dut);

    check(c, "qp", dut.qp, gp[c]);
    check(c, "qn", dut.qn, gn[c]);
    check(c, "ctl", dut.ctl, dv[c]);  // CONTROL: ungated reference clock
  }

  dut.final();

  if (failures != 0) {
    std::printf("FAIL chained_clock_gates_mixed_phase: %d mismatch(es) under verilator\n", failures);
    return 1;
  }
  std::printf("PASS chained_clock_gates_mixed_phase: each cell of a mixed-flavour "
              "gate chain samples its OWN enable at its own phase (verilator)\n");
  return 0;
}
