
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

int64_t top_a;
int64_t top_b;
__int128 top_ans;

// please just use the binary print, this doesn't always work
string to_string_int128(__int128 var) {
  string str = string("");
  int loop_size = 39;
  for (int i = 0; i < loop_size; i++) {
    __int128 temp = var /
      ((__int128) pow(10, loop_size - i - 1));
    str += to_string((uint64_t) (temp % ((__int128) 10)));
  }
  return str;
}

// please use this
string to_string_int128_binary(__int128 var) {
  string str = string("");
  __int128 bit_mask = 1;
  for (int i = 0; i < 128; i++) {
    str =
      to_string((uint64_t)
          ((var & ((__int128)
                  (bit_mask << ((__int128) i)))) == 0 ? 0 : 1))
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
    64, 1
  };

  uint64_t val_b[NUM_TESTS] = {
    (uint64_t) ~0, 0, 0xAAAAAAAAAAAAAAAA, (uint64_t) 1 << 63, 2, 3, 8, 16, 32,
    64, 1
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

    if (val_a[i] & ((uint64_t) 1 << 63)) {
      top_a = (int64_t) -1 * (~val_a[i] + 1);
    } else {
      top_a = (int64_t) val_a[i];
    }
    if (val_b[i] & ((uint64_t) 1 << 63)) {
      top_b = (int64_t) -1 * (~val_b[i] + 1);
    } else {
      top_b = (int64_t) val_b[i];
    }
    top->a = val_a[i];
    top->b = val_b[i];

    advance_clock(top,1);

    top_ans = ((__int128) top->ans[0]) +
      ((__int128) top->ans[1] << 32) +
      ((__int128) top->ans[2] << 64) +
      ((__int128) top->ans[3] << 96);

    // evaluate correctness
    printf("Test %d: ", i);
    __int128 product = ((__int128) top_a) *
      ((__int128) top_b);
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

