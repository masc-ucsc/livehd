//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "Vkogg_stone_64.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <stdint.h>

#include <list>

#include <time.h>

#define MAX_TIME 2000
#define NUM_TESTS 4

vluint64_t global_time = 0;
VerilatedVcdC* tfp = 0;

uint64_t top_a;
uint64_t top_b;
uint64_t top_s;


void do_terminate() {
#ifdef TRACE
      tfp->dump(global_time);
#endif
#ifdef TRACE
  tfp->close();
#endif

  printf("simulation finished at cycle %lld\n",(long long)global_time);

  exit(0);
}

void advance_clock(Vkogg_stone_64 *top, int nclocks=1) {

  for( int i=0;i<nclocks;i++) {
    for (int clk=0; clk<2; clk++) {

      top->eval();
#ifdef TRACE
      tfp->dump(global_time);
#endif

      top->clk = !top->clk;

      top->eval();
#ifdef TRACE
      tfp->dump(global_time);
#endif

      global_time++;
      if (Verilated::gotFinish() || global_time >= MAX_TIME)
        do_terminate();
    }
  }
}

int main(int argc, char **argv, char **env) {

  int sim_seed = time(0);
  srand(sim_seed);
  printf("random seed was %d\n",sim_seed);

  Verilated::commandArgs(argc, argv);
  // init top verilog instance
  Vkogg_stone_64* top = new Vkogg_stone_64;


  uint64_t val_a[NUM_TESTS] = {
    (uint64_t) 1 << 13, 0, 0x0AAAAAAAAAAAAAAA, (uint64_t) 1 << 30
  };

  uint64_t val_b[NUM_TESTS] = {
    (uint64_t) 1 << 63, 0, 0x0AAAAAAAAAAAAAAA, (uint64_t) 1 << 30
  };


#ifdef TRACE
  // init trace dump
  Verilated::traceEverOn(true);
  tfp = new VerilatedVcdC;

  top->trace(tfp, 99);
  tfp->open("output.vcd");
#endif

  // initialize simulation inputs
  for (int i = 0; i < NUM_TESTS; i++) {

    top_a = top->a = val_a[i];
    top_b = top->b = val_b[i];

    advance_clock(top,1);

    top_s = top->s;
//    top_carry = top->carry;

    // evaluate correctness
    printf("Test %d: ", i);
//    uint8_t lower_sum = (top_a & 0x1) + (top_b & 0x1);
    uint64_t upper_a = (top_a >> 1);
    uint64_t upper_b = (top_b >> 1);
    uint64_t shifted_sum = upper_a + upper_b;
 //   if (lower_sum == 2) shifted_sum++;
    if(top_a + top_b == top_s) {
      printf("PASSED\n");
    } else {
      printf("FAILED\n");
    }
    printf("With values:\n");
  printf("A = %lld, B = %lld, topsum = %lld, Sum = %lld\n",
        top_a, top_b,(top_a + top_b), top_s);
  }
  do_terminate;
}

