#include "verilated.h"
#include "verilated_vcd_c.h"
#include "Vpipe.h"
#include "Vpipe_pipe.h"
#include "Vpipe_pyrm_write_back_block.h"
#include "Vpipe_write_back_block_dcache.h"
#include "Vpipe_pyrm_fetch_block.h"
#include "Vpipe_fetch_block_icache.h"
#include <cstdio>

#include "riscv_verify.h"
using RISCV_Verify::Emulator;
using RISCV_Verify::word_t;
using RISCV_Verify::inst_t;

#ifndef BINFILE
#define BINFILE           "tests/dhry.bin"
#endif

#ifndef MAX_CYCLES
#define MAX_CYCLES        4000
#endif

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

void initialize(Emulator *, Vpipe *);
bool compare_memory(Emulator *emu, Vpipe *pipe);
bool advance_and_verify(Emulator *, Vpipe *);
void advance_clock(Vpipe *);
bool verify_cycle(Emulator *emulator, Vpipe *pipe);

int cycles = 0;
int inst_ctr = 0;

void dut_main()
{
  Vpipe *cliff = new Vpipe;
#ifdef TRACE
  // init trace dump
  Verilated::traceEverOn(true);
  tfp = new VerilatedVcdC;

  cliff->trace(tfp, 99);
  tfp->open("output.vcd");
#endif

  Emulator emu(BINFILE);

  initialize(&emu, cliff);

  for ( ; cycles < MAX_CYCLES; cycles++) {
    if (!advance_and_verify(&emu, cliff))
      break;
  }

  printf("...done.  Ran %d cycles.\n", cycles);
  do_terminate();

  delete cliff;
}

bool verify_cycle(Emulator *emulator, Vpipe *pipe)
{
  static word_t last_pc = 0;

  static vluint64_t last_wb_time   = global_time;

  if (global_time > last_wb_time + 100) {
    printf("ERROR: Too many cycles without retiring instructions\n");
    return false;
  }

  if (pipe->debug_committed_pc_valid_pyro) {
    last_wb_time = global_time;
//  if (pipe->output("debug_committed_pc").get_valid() &&
//      !pipe->output("debug_committed_pc").get_retry()) {
    inst_ctr++;
    word_t last_committed_pc = pipe->debug_committed_pc_pyro;

    if (last_pc == last_committed_pc && cycles > 10) {
      printf("TB jumped to same PC: %016lX\n", last_pc);
    }

    last_pc = last_committed_pc;
    word_t emu_pc = emulator->get_pc();

    if (last_pc != emu_pc) {
      printf("PC mismatch.  UUT: %016lX\n"
             "              EMU: %016lX\n", last_pc, emu_pc);
      return false;
    }

    emulator->run_inst();

    if (pipe->debug_reg_addr_valid_pyro && pipe->debug_reg_data_valid_pyro) {
      word_t raddr = pipe->debug_reg_addr_pyro;
      word_t rdata = pipe->debug_reg_data_pyro;

      if (raddr != 0) {
        word_t emu_rdata = emulator->get_reg(raddr);
        if (emu_rdata != rdata) {
          printf("Regfile mismatch for reg[%s]\n"
                 "                  UUT: %016lX\n"
                 "                  EMU: %016lX\n",
                 emulator->reg_name(raddr), rdata, emu_rdata);
          return false;
        }
      }
    }

    inst_t inst = emulator->get_inst(last_pc);
    printf("\t(pc=%016lX) %s\n", last_pc, RISCV_Verify::inst_debug_text(inst).c_str());
  }

  return true;
}

void initialize(Emulator *emu, Vpipe *pipe)
{
  int ic_index = 0;
  int dc_index = 0;

  while (ic_index < 0x4000 && dc_index < 0x4000) {
    word_t addr = ic_index | 0x80000000U;

    pipe->pipe->fetch->fetch_block_icache_inst->icache[ic_index >> 2] = emu->get_inst(addr);

    inst_t inst = emu->get_inst(addr);
    ic_index += 4;

    addr = ic_index | 0x80000000U;

    pipe->pipe->fetch->fetch_block_icache_inst->icache[ic_index >> 2] = emu->get_inst(addr);

    ic_index += 4;

    addr = dc_index | 0x80000000U;
    pipe->pipe->wb->write_back_block_dcache_inst->dcache[dc_index >> 3] = emu->get_mem64(addr);

    dc_index += 8;
  }

  if (compare_memory(emu, pipe))
    printf("Caches loaded, initial memory check completed.\n");
  else {
    printf("Initial memory check failed...exiting.\n");
    do_terminate();
  }

  pipe->clk = 0;
  pipe->reset = 1;
  advance_clock(pipe);
  pipe->reset = 0;
  advance_clock(pipe);
}

bool compare_memory(Emulator *emu, Vpipe *pipe)
{
  int eaddr = 0;
  int paddr = 0;

  bool had_mismatch = false;

  while (eaddr < 0x4000 && paddr < 0x4000) {
    word_t full_addr = eaddr | 0x80000000U;
    word_t emem = emu->get_mem64(full_addr);
    eaddr += 8;

    word_t pmem = pipe->pipe->wb->write_back_block_dcache_inst->dcache[paddr >> 3];
    paddr += 8;

    if (emem != pmem) {
      printf("\t[%016lx] -> (emu: %016lx, uut: %016lx)\n", full_addr, emem, pmem);
      had_mismatch = true;
    }
  }

  return !had_mismatch;
}

bool advance_and_verify(Emulator *emu, Vpipe *uut)
{
  advance_clock(uut);
  return verify_cycle(emu, uut);
}

void advance_clock(Vpipe *uut)
{
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

