#include <cstdio>
#include <stdlib.h>

#include "Vtest_and2.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

vluint64_t     global_time = 0;
VerilatedVcdC *tfp         = 0;

#define HALF_CLOCK_SCALE 1000 // 1000ps -> 2000ps clock period or 500MHz

void do_terminate() {
#ifdef VM_TRACE
  tfp->dump(global_time);
  tfp->close();
#endif

  printf("simulation finished at cycle %lld\n", (long long)global_time);

  exit(0);
}

void advance_clock(Vtest_and2 *uut) {
  uut->clk ^= 1;
  uut->eval();
#ifdef VM_TRACE
  tfp->dump(global_time);
#endif
  uut->clk ^= 1;

  global_time+=HALF_CLOCK_SCALE;
  uut->eval();
#ifdef VM_TRACE
  tfp->dump(global_time);
#endif

  global_time+=HALF_CLOCK_SCALE;
}

int main() {
  Vtest_and2 top;
#ifdef VM_TRACE
  // init trace dump
  Verilated::traceEverOn(true);
  tfp = new VerilatedVcdC;

  top.trace(tfp, 99);
  tfp->open("output.vcd");
#endif

  top.clk     = 1;
  advance_clock(&top);
  advance_clock(&top);
  advance_clock(&top);
  advance_clock(&top);
  top.in1 = 1;
  top.in2 = 0;

  int test_and2 = 0;

  int toggle = 1;
  int toggle_counter = 0;

  while (global_time < (HALF_CLOCK_SCALE*100000)) {

    top.in1 = toggle;
    top.in2 = toggle;

    if (global_time < (HALF_CLOCK_SCALE*25000)) {
      toggle = !toggle; // 100%
    }else if (global_time < (HALF_CLOCK_SCALE*50000)) {
      if ((toggle_counter & 0x1) == 0) // 50%
        toggle = !toggle;
    }else if (global_time < (HALF_CLOCK_SCALE*75000)) {
      if ((toggle_counter & 0x3) == 0) // 25%
        toggle = !toggle;
    }else{
      if ((toggle_counter & 0xF) == 0) // 6.25% toggle rate
        toggle = !toggle;
    }

    toggle_counter++;

    test_and2 = top.in1 & top.in2;

    advance_clock(&top);

    if (top.out != test_and2) {
      fprintf(stderr, "ERROR: unexpected output of %d vs %d\n", top.out, test_and2);
      //do_terminate();
    }
  }

  do_terminate();

  return 0;
}

// Not part of bazel:
//
// verilator -O3 --top-module test_and2 ./pass/opentimer/tests/test_and2.v ../synth/bazel_rules_hdl_test/model/sky130_fd_sc_hd.v --cc --trace --exe ./pass/opentimer/tests/dut_test_and2.cpp
//
// make -C ./obj_dir/ -f Vtest_and2.mk
//
// ./obj_dir/Vtest_and2
//
// lgshell> inou.verilog files:pass/opentimer/tests/test_and2.v |> pass.compiler |> pass.opentimer.power files:sky130_fd_sc_hd__ff_100C_1v95.lib,output.vcd
//
// ....
// average activity rate 0.459731800893997
// MAX power switch:6.135334160717321e-06 internal:3.113572131852038e-05 voltage:1.95

