
#include <stdlib.h>

#include <iostream>

#include "disasm.h"

int main(int argc, const char **argv) {

  if (argc!=2) {
    std::cerr << "Usage:\n";
    std::cerr << argv[0] << " instruction_hexa\n";
    exit(0);
  }

  disassembler_t dis(64);

  insn_t insn(strtol(argv[1],0,16));

  std::cout << "insn: " << std::hex << insn.bits() << " : " << dis.disassemble(insn) << "\n";

  return 0;
}

