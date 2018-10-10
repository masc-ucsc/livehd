#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>

//#include "riscvdis/disasm.h"

#include "cutils.h"
#include "iomem.h"
#include "virtio.h"
#include "machine.h"
#ifdef CONFIG_CPU_RISCV
#include "riscv_cpu.h"
#endif


VirtMachine *virt_machine_main(int argc, char **argv);
void virt_machine_run(VirtMachine *m);
void virt_machine_end(VirtMachine *m);
void virt_machine_dump_regs(VirtMachine *m);
int  virt_machine_read_insn(VirtMachine *m, uint32_t *insn, uint64_t addr);
int virt_machine_read_u64(VirtMachine *m, uint64_t *data, uint64_t addr);

uint64_t  virt_machine_get_pc(VirtMachine *m);
void      virt_machine_set_pc(VirtMachine *m, uint64_t pc);

uint64_t  virt_machine_get_reg(VirtMachine *m, int rn);
void      virt_machine_set_reg(VirtMachine *m, int rn, uint64_t val);

extern uint64_t checker_last_addr;
extern uint64_t checker_last_data;

uint64_t checker_get_reg(int rn);
uint64_t checker_get_pc();

VirtMachine *m = 0;

uint32_t checker_get_insn(uint64_t pc) {

  uint32_t insn_raw=0;

  virt_machine_read_insn(m, &insn_raw, pc);

  return insn_raw;
}

uint64_t checker_get_u64(uint64_t addr) {

  uint64_t data=0;

  virt_machine_read_u64(m, &data, addr);

  return data;
}

uint64_t checker_get_reg(int rn) {
  return virt_machine_get_reg(m, rn);
}

uint64_t checker_get_pc() {
  return virt_machine_get_pc(m);
}

void checker_run_inst() { virt_machine_run(m); }

int checker_check_next(uint64_t pc, int rd, uint64_t data, uint64_t addr) {

  uint64_t epc = virt_machine_get_pc(m);
  uint32_t insn_raw = checker_get_insn(epc);

  if (epc != pc ) {
    virt_machine_dump_regs(m);
    printf("ERROR: pc does not match checker pc:%llx vs rtl pc:%llx\n", (long long)epc, (long long)pc);
    return -1;
  }
  if (checker_last_addr) {
    virt_machine_dump_regs(m);
    printf("ERROR: spurious checker_last_addr:%llx update\n",(long long)checker_last_addr);
    return -1;
  }

  virt_machine_run(m);

  if (checker_last_addr != addr) {
    virt_machine_dump_regs(m);
    printf("ERROR: addr does not match checker addr:%llx vs rtl addr:%llx\n", (long long)checker_last_addr, (long long)addr);
    return -1;
  }
  if (rd==0 && addr) { // Store Check
    if (checker_last_data != data) {
      virt_machine_dump_regs(m);
      printf("ERROR: store data does not match checker data:%llx vs rtl data:%llx\n", (long long)checker_last_data, (long long)data);
      return -1;
    }
    //virt_machine_dump_regs(m);
    //uint64_t d0 = checker_get_u64(addr);
    //printf("1.store d0:%llx addr:%llx data:%llx\n",(long long)d0, (long long)checker_last_addr, (long long)checker_last_data);
  }
  //uint64_t addr2 = 0xffffffffffffff50ULL;
  //uint64_t d0 = checker_get_u64(addr2);
  //printf("2.store d0:%llx addr:%llx data:%llx\n",(long long)d0, (long long)checker_last_addr, (long long)checker_last_data);

  int erd = (insn_raw>>7) & 0x1F;
  if (erd != rd && rd!=0) { // if RD is provided
    virt_machine_dump_regs(m);
    printf("ERROR: rd does not match checker rd:%d vs rtl rd:%d\n", erd, rd);
    return -1;
  }

  if (rd!=0 && virt_machine_get_reg(m,rd) != data) {
    virt_machine_dump_regs(m);
    printf("ERROR: data does not match checker data:%llx vs rtl data:%llx last_data:%llx\n", (long long)virt_machine_get_reg(m,rd), (long long)data, (long long)checker_last_data);
    return -1;
  }

  checker_last_addr = 0; // To track that we did not miss stores either

  return 0;
}

int checker_main(int argc, char **argv, uint64_t ram_size) {

  m = virt_machine_main(argc,argv);

  uint64_t pc = 0x80000000;
//  uint64_t sp = 0x80000000+ram_size-128;

  virt_machine_set_pc(m, pc);
//  virt_machine_set_reg(m, 2, sp);

  //virt_machine_run(m);
  //virt_machine_dump_regs(m);

  return 0;
}

void checker_exit() {
  virt_machine_end(m);
}

