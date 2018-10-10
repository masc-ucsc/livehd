#ifndef RISCV_VERIFY_H_
#define RISCV_VERIFY_H_

#include <cstdint>
#include <string>

#define MSG_BUFFER_SIZE     256
#define EMULATOR_RAM_SIZE   8192

#define INST_OPCODE(inst) (inst & 0x0000007F)
#define INST_RD(inst)     ((inst & 0x00000F80) >> 7)
#define INST_RS1(inst)    ((inst & 0x000F8000) >> 15)

namespace RISCV_Verify
{
  typedef uint32_t inst_t;
  typedef uint64_t word_t;

  const uint8_t STORE_OPCODE  = 0x23;
  const uint8_t LOAD_OPCODE   = 0x03;

  class Emulator
  {
  private:
    word_t last_executed_pc;

  public:
    Emulator(const char *bin_filename);
    ~Emulator();

    inst_t get_inst(word_t addr);
    word_t get_mem64(word_t addr);
    word_t get_reg(int rn);
    word_t get_pc();
    bool check_next(word_t pc, int rdest, word_t addr, word_t data);
    bool check_next_regfile(int addr, word_t data);
    void run_inst();

    word_t calc_load_address(inst_t inst);
    word_t calc_store_address(inst_t inst);

    void dump_regs();
    void last_inst_info(word_t pc, int rdest, word_t addr, word_t data);
    void load_store_info(word_t pc, int rdest, word_t addr, word_t data);
    static const char *reg_name(int rdest);
  };

  std::string inst_text(inst_t inst);
  std::string inst_debug_text(inst_t inst);
}

#endif

