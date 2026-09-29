// Independent event-driven twin of conditional_icg_sliced_guard.prp.
#include <cstdint>
#include <cstdio>

#include "Vconditional_icg_top.h"
#include "verilated.h"

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Vconditional_icg_top dut;
  auto                 step = [&] {
    dut.clk = 0;
    dut.eval();
    dut.clk = 1;
    dut.eval();
    dut.clk = 0;
    dut.eval();
  };
  dut.reset  = 1;
  dut.active = 0x84;
  dut.gate   = 1;
  dut.d      = 0;
  step();
  dut.reset                 = 0;
  const uint32_t expected[] = {1, 3, 0, 0, 19};
  uint32_t       control    = 0;
  for (int cycle = 0; cycle < 5; ++cycle) {
    dut.active  = 0x80 | ((cycle <= 1 || cycle == 4) ? 4 : 0);
    dut.d       = 1 << cycle;
    control    += dut.d;
    step();
    if (dut.q != expected[cycle] || dut.ctl != control) {
      std::printf("FAIL cycle %d: q=%u expected=%u ctl=%u expected=%u\n", cycle, dut.q, expected[cycle], dut.ctl, control);
      return 1;
    }
  }
  dut.final();
  std::puts("PASS conditional_icg_sliced_guard (verilator)");
  return 0;
}
