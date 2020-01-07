#include "verilated.h"
#include "verilated_vcd_c.h"
#include "Vsample.h"
#include <cstdio>

vluint64_t global_time   = 0;
VerilatedVcdC     *tfp   = 0;

void do_terminate() {
#ifdef TRACE
  tfp->dump(global_time);
  tfp->close();
#endif

  printf("simulation finished at cycle %lld\n",(long long)global_time);

  exit(0);
}

void advance_clock(Vsample *uut) {
  uut->clk ^= 1;
  uut->eval();
#ifdef TRACE
  tfp->dump(global_time);
#endif
  uut->clk ^= 1;
  uut->eval();
#ifdef TRACE
  tfp->dump(global_time);
#endif

  global_time++;
}


int main() {
  Vsample sample;
#ifdef TRACE
  // init trace dump
  Verilated::traceEverOn(true);
  tfp = new VerilatedVcdC;

  sample.trace(tfp, 99);
  tfp->open("output.vcd");
#endif

  sample.clk = 0;
  sample.reset = 1;
  while(global_time<1000) {
    advance_clock(&sample);
  }
  sample.reset = 0;

  while(global_time < 100000000) {
    advance_clock(&sample);
  }

  do_terminate();

  return 0;
}

