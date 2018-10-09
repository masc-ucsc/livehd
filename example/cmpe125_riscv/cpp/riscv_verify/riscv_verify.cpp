#include "riscv_verify.h"
using RISCV_Verify::Emulator;
using RISCV_Verify::inst_t;
using RISCV_Verify::word_t;
using RISCV_Verify::inst_debug_text;

#include <cassert>
#include <string>
using std::string;

#include "../riscvdis/disasm.h"

////////////////////////////////////////////////////////////////////////////
// RISCV checker backend
extern "C" uint32_t checker_get_insn(uint64_t addr);
extern "C" uint64_t checker_get_pc();
extern "C" uint64_t checker_get_reg(int rn);
extern "C" uint64_t checker_get_u64(uint64_t addr);
extern "C" int checker_main(int argc, char **argv, uint64_t ram_size);
extern "C" void checker_run_inst();
extern "C" void checker_exit();

extern uint64_t checker_last_addr;
extern uint64_t checker_last_data;
////////////////////////////////////////////////////////////////////////////

Emulator::Emulator(const char *bin_filename) : last_executed_pc(0)
{
  char first_arg[] = "riscv_verify";

  char *fn = new char[strlen(bin_filename)+1];
  strcpy(fn, bin_filename);

  char *argv[] = { first_arg, fn };

  checker_main(2, argv, EMULATOR_RAM_SIZE);
  delete[] fn;
}

Emulator::~Emulator() { checker_exit(); }

bool Emulator::check_next(word_t pc, int rdest, word_t addr, word_t data)
{
  word_t chkr_pc = checker_get_pc();

  if (chkr_pc != pc) {
    printf("\n\n\tERROR: UUT pc does not match the Emulator's.\n");
    printf("\t\tEmulator pc: %016lX - (inst: %s)\n",
        chkr_pc, inst_debug_text(get_inst(chkr_pc)).c_str());
    printf("\t\tUUT pc     : %016lX - (inst: %s)\n",
        pc, inst_debug_text(get_inst(pc)).c_str());

    printf("\n");
    printf("\t\tlast pc    : %016lX - (inst: %s)\n\n\n",
        last_executed_pc, inst_debug_text(get_inst(last_executed_pc)).c_str());
    
    dump_regs();
    return false;
  }

  last_executed_pc = pc;

  run_inst();
  word_t saved_checker_last_addr = checker_last_addr;
  word_t saved_checker_last_data = checker_last_data;

  inst_t insn_raw = get_inst(pc);

  if (addr != 0 && saved_checker_last_addr != addr) {
    printf("\n\n\tERROR for instruction: %s @ pc=%016lX\n",
        inst_debug_text(insn_raw).c_str(), pc);

    printf("\t\taddress mismatch: (uut: %016lX, emu: %016lX)\n\n\n",
        addr, saved_checker_last_addr);
    
    dump_regs();
    return false;
  }
  
  if (rdest == 0 && addr) {           // Store Check
    if (saved_checker_last_data != data) {
      printf(
          "ERROR: store data does not match checker data: %llx vs rtl data: %llx\n",
          (long long) saved_checker_last_data,
          (long long) data);
      return false;
    }
  }

  int erd = (insn_raw >> 7) & 0x1F;
  if (erd != rdest && rdest != 0) {   // if RD is provided
    printf("\tERROR for instruction: %s @ pc=%016lX\n",
        inst_debug_text(insn_raw).c_str(), pc);
    printf("\t\tUUT rd=%s [%d]\n\t\tchecker rd=%s[%d]\n",
        reg_name(rdest), rdest, reg_name(erd), erd);
    return false;
  }

  if (rdest != 0 && get_reg(rdest) != data) {
    printf("\n\n\tERROR for instruction: %s @ pc=%016lX\n",
        inst_debug_text(insn_raw).c_str(), pc);
    load_store_info(pc, rdest, addr, data);

    printf("\t\tUUT      reg[%s] = %016lX\n", reg_name(rdest), data);
    printf("\t\tEmulator reg[%s] = %016lX\n", reg_name(rdest), get_reg(rdest));

    printf("\n");
    dump_regs();
    return false;
  }

  return true;
}

#define REGFILE_CHANGE_OP_COUNT 9

bool Emulator::check_next_regfile(int addr, word_t data)
{
  static const unsigned char regfile_change_ops[] = {
    0x13,   // ADDI
    0x33,   // ADDR
    0x1b,   // ADDIW
    0x3b,   // ADDRW
    0x67,   // JALR
    0x6f,   // JAL
    0x03,   // LOAD
    0x37,   // LUI
    0x17    // AUIPC
  };

  while (true) {
    word_t pc = get_pc();
    inst_t next_inst = get_inst(pc);
    run_inst();

    bool regfile_change_flag = false;

    for (int i = 0; i < REGFILE_CHANGE_OP_COUNT; i++) {
      if (INST_OPCODE(next_inst) == regfile_change_ops[i]) {
        regfile_change_flag = true;
        break;
      }
    }

    if (regfile_change_flag) {
      if (((unsigned int) addr) != INST_RD(next_inst)) {
        printf("ERROR for instruction: %s [%016lX]\n",
            inst_debug_text(next_inst).c_str(), pc);
        printf("\tUUT destination does not match EMU destination.\n");
        printf("\tuut: %s (%d), emu: %s (%d)\n",
            reg_name(addr), addr, reg_name(INST_RD(next_inst)), INST_RD(next_inst));
        return false;
      }

      if (addr != 0 && get_reg(addr) != data) {
        printf("ERROR for instruction: %s [%016lX]\n",
            inst_debug_text(next_inst).c_str(), pc);
        printf("\tUUT reg[%d] does not match EMU reg[%d]\n", addr, addr);
        printf("\tuut: %016lX\n\temu: %016lX\n", data, get_reg(addr));
        return false;
      }

      return true;
    }
  }
}

void Emulator::run_inst()
{
  last_executed_pc = checker_get_pc();
  checker_run_inst();
}

void Emulator::dump_regs()
{
  for (int i = 0; i < 32; i += 4) {
    for (int j = 0; j < 4; j++) {
      int rid = i+j;
      printf("%-4s: %016lx\t", reg_name(rid), get_reg(rid));
    }

    printf("\n");
  }
}

const char *Emulator::reg_name(int rdest)
{
  static const char *reg_names[32] = {
    "zero", "ra", "sp",   "gp",   "tp", "t0", "t1", "t2",
    "s0",   "s1", "a0",   "a1",   "a2", "a3", "a4", "a5",
    "a6",   "a7", "s2",   "s3",   "s4", "s5", "s6", "s7",
    "s8",   "s9", "s10",  "s11",  "t3", "t4", "t5", "t6"
  };

  return reg_names[rdest];
}

inst_t Emulator::get_inst(word_t addr) { return (inst_t) checker_get_insn(addr); }
word_t Emulator::get_mem64(word_t addr) { return (word_t) checker_get_u64(addr); }
word_t Emulator::get_reg(int rn) { return (word_t) checker_get_reg(rn); }
word_t Emulator::get_pc() { return (word_t) checker_get_pc(); }

word_t Emulator::calc_load_address(inst_t inst)
{
  int rs1 = INST_RS1(inst);
  word_t offset = (inst & 0xFFF00000) >> 20;
  
  if (offset & 0x800)
    offset |= 0xFFFFFFFFFFFFF000U;

  return get_reg(rs1) + offset;
}

word_t Emulator::calc_store_address(inst_t inst)
{
  int rs1 = INST_RS1(inst);
  word_t offsetl = (0x00000F80 & inst) >> 7;
  word_t offseth = (0xFC000000 & inst) >> 26;
  word_t offset = offseth << 5 | offsetl;

  if (offset & 0x800)
    offset |= 0xFFFFF000;

  return get_reg(rs1) + offset;
}
  
void Emulator::last_inst_info(word_t pc, int rdest, word_t addr, word_t data)
{
  inst_t insn_raw = get_inst(pc);

  printf("\n\n##########################: %s\n", inst_debug_text(insn_raw).c_str());
  printf("\t\t\tpc: %016lX\n", pc);
  printf("\t\t\trd: %d  addr: %016lX  data: %016lX\n", rdest, addr, data);

  load_store_info(pc, rdest, addr, data);
}

void Emulator::load_store_info(word_t pc, int rdest, word_t addr, word_t data)
{
  inst_t insn_raw = get_inst(pc);
  uint8_t opcode = insn_raw & 0x3F;

  if (opcode == RISCV_Verify::STORE_OPCODE) {
    word_t store_addr = calc_store_address(insn_raw);
    int source_rdest = (insn_raw & 0x01F00000) >> 20;

    printf("\t\t\treg[%d] = %016lX, mem[%016lX] = %016lX\n",
        source_rdest, get_reg(source_rdest),
        store_addr, get_mem64(store_addr));
  
  } else if (opcode == RISCV_Verify::LOAD_OPCODE) {
    word_t load_addr = calc_load_address(insn_raw);

    printf("\t\t\tld addr: %016lX\n", load_addr);

    printf("\n\t\t\tdata @ addr:\n");

    for (auto i = load_addr - 2; i <= load_addr + 2; i++)
      printf("\t\t\t\t[%016lX] -> %016lX\n", i, get_mem64(i));
  }
}

string RISCV_Verify::inst_text(inst_t inst)
{
  static disassembler_t dis(64);

  insn_t insn_obj(inst);
  return dis.disassemble(inst);
}

string RISCV_Verify::inst_debug_text(inst_t inst)
{
  string text = inst_text(inst);

  char buffer[12];
  sprintf(buffer, " [%08X]", inst);
  text += buffer;

  return text;
}

