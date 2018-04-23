
#include "Vshift_barrelfast_rright.h"
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

uint64_t top_sh;


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

void advance_clock(Vshift_barrelfast_rright *top, int nclocks=1) {

  top->eval();
#ifdef TRACE
  tfp->dump(global_time);
#endif

  global_time++;
  if (Verilated::gotFinish() || global_time >= MAX_TIME)
    do_terminate();
}

int main(int argc, char **argv, char **env) {

  int sim_seed = time(0);
  srand(sim_seed);
  printf("random seed was %d\n",sim_seed);

  Verilated::commandArgs(argc, argv);
  // init top verilog instance
  Vshift_barrelfast_rright* top = new Vshift_barrelfast_rright;


  uint64_t val_a[NUM_TESTS] = {
    (uint64_t) ~0, 0, 0xAAAAAAAAAAAAAAAA, (uint64_t) 1 << 63
  };

  /*uint64_t val_b[NUM_TESTS] = {
    (uint64_t) ~0, 0, 0xAAAAAAAAAAAAAAAA, (uint64_t) 1 << 63
  };
  */

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

    advance_clock(top,1);

    top_b = top->b;
    top_sh = top->sh;

    // evaluate correctness
    printf("Test %d: ", i);
    uint64_t shift_a = (top_a >> top_sh)|(top_a << (64 - top_sh));
    // if you are testing for 32 or 128 subtract using 32 or 128.
    if (shift_a == top_b){
      printf("PASSED\n");
    } else {
      printf("FAILED\n");
    }
    printf("With values:\n");
    printf("A = %zd, B = %zd, Shift = %zd\n",
        top_a, top_b, top_sh);

  }
  do_terminate;
}

