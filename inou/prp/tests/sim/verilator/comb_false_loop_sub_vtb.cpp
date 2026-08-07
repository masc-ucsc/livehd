// Verilator twin of inou/prp/tests/sim/comb_false_loop_sub.prp.
//
// THIS ONE IS NOT A "LIVEHD FAILS, VERILATOR PASSES" DIFFERENTIAL — the fixture
// passes under `lhd sim` too. It is here for the other reason a second simulator
// is worth having: `lhd sim` gets the right answer through a STOPGAP
// (`flatten_false_loop_subs` inlines the callee before scheduling, cgen_sim.cpp
// :1332-1405), and step 5 of todo_sim_pipeline.md deletes that stopgap in favour
// of per-partition callee methods. When that lands, "the fixture still passes"
// is a much weaker claim than "the fixture still agrees with an event-driven
// simulator that never had a scheduling problem to begin with".
//
// The design is the canonical false loop: `split` computes o1 from {a,b} and o2
// from {c,d}, and the caller wires o1 -> back -> c. There is no bit-level cycle;
// the cycle exists only because an atomic all-inputs-then-all-outputs call
// cannot express "evaluate o1, then me, then o2". Verilator settles the
// combinational region, so it simply does not have the problem — which is what
// makes it a valid oracle here rather than a second opinion of the same kind.
//
// Note the module's clock port is `clock` (not `clk`): that is what
// inou.cgen.verilog names the implicit clock of a `pub mod` whose regs declare
// no explicit clock_pin.

#include <cstdint>
#include <cstdio>

#include "Vftop.h"
#include "verilated.h"

namespace {

int failures = 0;

void check(int cycle, const char* what, uint32_t got, uint32_t want) {
  if (got != want) {
    std::printf("MISMATCH cycle %d: %s = %u, expected %u\n", cycle, what, got, want);
    ++failures;
  }
}

// One full period, both edges evaluated. The design here is posedge-only, but
// the fall eval is kept because it costs nothing and because a twin that omits
// it silently stops being a valid oracle the moment the fixture grows any
// negedge or latch state (see hier_gate_port_vtb.cpp, where it is load-bearing).
void advance_clock(Vftop* dut) {
  dut->clock = 1;
  dut->eval();
  dut->clock = 0;
  dut->eval();
}

}  // namespace

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Vftop dut;

  dut.clock = 0;
  dut.reset = 1;
  dut.a     = 0;
  dut.b     = 0;
  dut.d     = 0;
  dut.bump  = 0;
  dut.eval();
  advance_clock(&dut);
  dut.reset = 0;

  for (int c = 0; c < 6; ++c) {
    const uint32_t av   = 10 + c;
    const uint32_t bv   = 20 + 2 * c;
    const uint32_t dv   = 3 + c;
    const uint32_t bump = 7;

    dut.a    = av;
    dut.b    = bv;
    dut.d    = dv;
    dut.bump = bump;
    dut.eval();
    advance_clock(&dut);

    // The ONLY evaluation order that exists: o1, then the caller's back edge,
    // then o2. The fixture's Pyrope testbench computes exactly this.
    const uint32_t g_o1   = av + bv;
    const uint32_t g_back = (g_o1 + bump) & 0xFF;
    const uint32_t g_o2   = g_back * dv;

    check(c, "sum", dut.sum, g_o1);    // CONTROL: outside the false loop
    check(c, "prod", dut.prod, g_o2);  // the cone fed by the back edge
  }

  dut.final();

  if (failures != 0) {
    std::printf("FAIL comb_false_loop_sub: %d mismatch(es) under verilator\n", failures);
    return 1;
  }
  std::printf("PASS comb_false_loop_sub: the false loop through an atomic call "
              "produces the same values under verilator as under lhd sim\n");
  return 0;
}
