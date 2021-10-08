//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <stdint.h>
#include <time.h>

#include <list>

#include "Vcgen_driver.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#define MAX_TIME  2000
#define NUM_TESTS 4

vluint64_t     global_time = 0;
VerilatedVcdC *tfp         = 0;

//inputs
uint16_t top_rd_addr;
uint16_t top_rd_enable;
uint16_t top_wr_addr;
uint16_t top_wr_enable;
uint16_t top_wr_din;
//outputs
uint16_t top_rd_dout;


void do_terminate() {
#ifdef TRACE
  tfp->dump(global_time);
#endif
#ifdef TRACE
  tfp->close();
#endif

  printf("simulation finished at cycle %lld\n", (long long)global_time);

  exit(0);
}

void advance_clock(Vcgen_driver *top, int nclocks = 1) {
  for (int i = 0; i < nclocks; i++) {
    for (int clk = 0; clk < 2; clk++) {
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
  printf("random seed was %d\n", sim_seed);

  Verilated::commandArgs(argc, argv);
  // init top verilog instance
  Vcgen_driver *top = new Vcgen_driver;

  uint16_t val_rd_addr[NUM_TESTS] = {0,0,0,0};
  uint16_t val_rd_enable[NUM_TESTS] = {1,1,1,1};
  uint16_t val_wr_addr[NUM_TESTS] = {0,0,0,0};
  uint16_t val_wr_enable[NUM_TESTS] = {1,1,1,1};
  uint16_t val_wr_din[NUM_TESTS+2] = {0xaaaa, 0xaaab, 0xaaaf, 0x5555, 0x0000, 0x0000};  //extra two values for longer latency param settings

#ifdef TRACE
  // init trace dump
  Verilated::traceEverOn(true);
  tfp = new VerilatedVcdC;

  top->trace(tfp, 99);
  tfp->open("output.vcd");
#endif

  // initialize simulation inputs

  // FWD = 0 LATENCY = 0 WRENSIZE = 1
  printf("\nNow testing FWD=0 LATENCY=0 WRENSIZE=1\n");
  // Pipe in first value
  top_rd_addr = top->rd_addr_0 = val_rd_addr[0];
  top_rd_enable = top->rd_enable_0 = val_rd_enable[0];
  top_wr_addr = top->wr_addr_0 = val_wr_addr[0];
  top_wr_enable = top->wr_enable_0 = val_wr_enable[0];
  top_wr_din = top->wr_din_0 = val_wr_din[0];

  advance_clock(top, 1);

  for (int i = 0; i < NUM_TESTS; i++) {
    top_rd_addr = top->rd_addr_0 = val_rd_addr[i];
    top_rd_enable = top->rd_enable_0 = val_rd_enable[i];
    top_wr_addr = top->wr_addr_0 = val_wr_addr[i];
    top_wr_enable = top->wr_enable_0 = val_wr_enable[i];
    top_wr_din = top->wr_din_0 = val_wr_din[i+1];

    advance_clock(top, 1);

    top_rd_dout = top->rd_dout_0;

    // evaluate correctness
    printf("Test %d: ", i);
    if(top_rd_dout == val_wr_din[i]) {
      printf("PASSED\n");
    } else {
      printf("FAILED \n");
    }
    printf("With values:\n");
    printf("rd_addr = 0x%X, wr_addr = 0x%X, wr_din = 0x%X, rd_dout = 0x%X\n", top_rd_addr, top_wr_addr, top_wr_din, top_rd_dout);
  }


  // FWD = 0 LATENCY = 1 WRENSIZE = 1
  printf("\nNow testing FWD=0 LATENCY=1 WRENSIZE=1\n");
  // Pipe in first 2 values
  for (int j = 0; j < NUM_TESTS-2; j++) {
    top_rd_addr = top->rd_addr_1 = val_rd_addr[j];
    top_rd_enable = top->rd_enable_1 = val_rd_enable[j];
    top_wr_addr = top->wr_addr_1 = val_wr_addr[j];
    top_wr_enable = top->wr_enable_1 = val_wr_enable[j];
    top_wr_din = top->wr_din_1 = val_wr_din[j];

    advance_clock(top, 1);
  }

  for (int i = 0; i < NUM_TESTS; i++) {
    top_rd_addr = top->rd_addr_1 = val_rd_addr[i];
    top_rd_enable = top->rd_enable_1 = val_rd_enable[i];
    top_wr_addr = top->wr_addr_1 = val_wr_addr[i];
    top_wr_enable = top->wr_enable_1 = val_wr_enable[i];
    top_wr_din = top->wr_din_1 = val_wr_din[i+2];

    advance_clock(top, 1);

    top_rd_dout = top->rd_dout_1;

    // evaluate correctness
    printf("Test %d: ", i);
    if(top_rd_dout == val_wr_din[i]) {
      printf("PASSED\n");
    } else {
      printf("FAILED \n");
    }
    printf("With values:\n");
    printf("rd_addr = 0x%X, wr_addr = 0x%X, wr_din = 0x%X, rd_dout = 0x%X\n", top_rd_addr, top_wr_addr, top_wr_din, top_rd_dout);
  }

  // FWD = 1 LATENCY = 0 WRENSIZE = 1
  printf("\nNow testing FWD=1 LATENCY=0 WRENSIZE=1\n");
  for (int i = 0; i < NUM_TESTS; i++) {
    top_rd_addr = top->rd_addr_2 = val_rd_addr[i];
    top_rd_enable = top->rd_enable_2 = val_rd_enable[i];
    top_wr_addr = top->wr_addr_2 = val_wr_addr[i];
    top_wr_enable = top->wr_enable_2 = val_wr_enable[i];
    top_wr_din = top->wr_din_2 = val_wr_din[i];

    advance_clock(top, 1);

    top_rd_dout = top->rd_dout_2;

    // evaluate correctness
    printf("Test %d: ", i);
    if(top_rd_dout == val_wr_din[i]) {
      printf("PASSED\n");
    } else {
      printf("FAILED \n");
    }
    printf("With values:\n");
    printf("rd_addr = 0x%X, wr_addr = 0x%X, wr_din = 0x%X, rd_dout = 0x%X\n", top_rd_addr, top_wr_addr, top_wr_din, top_rd_dout);
  }

  // FWD = 1 LATENCY = 1 WRENSIZE = 1
  printf("\nNow testing FWD=1 LATENCY=1 WRENSIZE=1\n");
  for (int i = 0; i < NUM_TESTS; i++) {
    top_rd_addr = top->rd_addr_3 = val_rd_addr[i];
    top_rd_enable = top->rd_enable_3 = val_rd_enable[i];
    top_wr_addr = top->wr_addr_3 = val_wr_addr[i];
    top_wr_enable = top->wr_enable_3 = val_wr_enable[i];
    top_wr_din = top->wr_din_3 = val_wr_din[i];

    advance_clock(top, 1);

    top_rd_dout = top->rd_dout_3;

    // evaluate correctness
    printf("Test %d: ", i);
    if(top_rd_dout == val_wr_din[i]) {
      printf("PASSED\n");
    } else {
      printf("FAILED \n");
    }
    printf("With values:\n");
    printf("rd_addr = 0x%X, wr_addr = 0x%X, wr_din = 0x%X, rd_dout = 0x%X\n", top_rd_addr, top_wr_addr, top_wr_din, top_rd_dout);
  }

  // FWD = 1 LATENCY = 1 WRENSIZE = 2
  printf("\nNow testing FWD=1 LATENCY=1 WRENSIZE=2\n");
  for (int i = 0; i < NUM_TESTS; i++) {
    top_rd_addr = top->rd_addr_4 = val_rd_addr[i];
    top_rd_enable = top->rd_enable_4 = val_rd_enable[i];
    top_wr_addr = top->wr_addr_4 = val_wr_addr[i];
    top_wr_enable = top->wr_enable_4 = val_wr_enable[i];
    top_wr_din = top->wr_din_4 = val_wr_din[i];

    advance_clock(top, 1);

    top_rd_dout = top->rd_dout_4 & 0xff;

    // evaluate correctness
    printf("Test %d: ", i);
    if(top_rd_dout == (val_wr_din[i] & 0xff)) {
      printf("PASSED\n");
    } else {
      printf("FAILED \n");
    }
    printf("With values:\n");
    printf("rd_addr = 0x%X, wr_addr = 0x%X, wr_din = 0x%X, rd_dout = 0x%X\n", top_rd_addr, top_wr_addr, top_wr_din, top_rd_dout);
  }
  do_terminate;
}