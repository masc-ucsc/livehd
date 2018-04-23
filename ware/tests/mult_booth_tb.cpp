
#include "Vmult_booth.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <stdint.h>
#include <string.h>

#include <list>

#include <time.h>

#define MAX_TIME 2000
#define NUM_TESTS 11

vluint64_t global_time = 0;
VerilatedVcdC* tfp = 0;

uint64_t top_a;
uint64_t top_b;
unsigned __int128 top_ans;

string to_string_int128(unsigned __int128 var) {
  string str = string("");
  int loop_size = 39;
  for (int i = 0; i < loop_size; i++) {
    unsigned __int128 temp = var /
      ((unsigned __int128) pow(10, loop_size - i - 1));
    str += to_string((uint64_t) (temp % ((unsigned __int128) 10)));
  }
  return str;
}

string to_string_int128_binary(unsigned __int128 var) {
  string str = string("");
  unsigned __int128 bit_mask = 1;
  for (int i = 0; i < 128; i++) {
    str =
      to_string((uint64_t)
          ((var & ((unsigned __int128)
                  (bit_mask << ((unsigned __int128) i)))) == 0 ? 0 : 1))
      + str;
  }
  return str;
}


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

void advance_clock(Vmult_booth *top, int nclocks=1) {

  for( int i=0;i<nclocks;i++) {
    for (int clk=0; clk<2; clk++) {

      top->eval();
#ifdef TRACE
      tfp->dump(global_time);
#endif

      //top->clk = !top->clk;

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
  Vmult_booth* top = new Vmult_booth;


  uint64_t val_a[NUM_TESTS] = {
    (uint64_t) ~0, 0, 0xAAAAAAAAAAAAAAAA, (uint64_t) 1 << 63, 2, 3, 8, 16, 32,
    64, 10
  };

  uint64_t val_b[NUM_TESTS] = {
    (uint64_t) ~0, 0, 0xAAAAAAAAAAAAAAAA, (uint64_t) 1 << 63, 2, 3, 8, 16, 32,
    64, 10
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

    top_ans = ((unsigned __int128) top->ans[0]) +
      ((unsigned __int128) top->ans[1] << 32) +
      ((unsigned __int128) top->ans[2] << 64) +
      ((unsigned __int128) top->ans[3] << 96);

    // evaluate correctness
    printf("Test %d: ", i);
    unsigned __int128 product = ((unsigned __int128) top_a) *
      ((unsigned __int128) top_b);
    if (product == top_ans) {
      printf("PASSED\n");
    } else {
      printf("FAILED\n");
    }
    printf("With values:\n");
    printf("a = %lu, b = %lu\ne a = %s\na a = %s\n",
        top_a, top_b,
        to_string_int128_binary(product).c_str(),
        to_string_int128_binary(top_ans).c_str());
    printf("=======================================\n");
  }
  do_terminate;
}

