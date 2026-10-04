// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Prints the memory wrapper `Cgen_verilog` GENERATES for a (R,W,clock) shape
// ware/rtl does not ship, so a test can simulate the real generated module
// rather than a copy of it.
//
//   gen_mem_wrapper_dump <module_name> <n_rd> <n_wr> <single_clock 0|1>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "cgen_verilog.hpp"

struct Cgen_verilog_test_peer {
  static std::string gen(const std::string& name, int n_rd, int n_wr, bool single_clock) {
    return Cgen_verilog::gen_mem_wrapper(name, n_rd, n_wr, single_clock);
  }
};

int main(int argc, char** argv) {
  if (argc != 5) {
    std::fprintf(stderr, "usage: %s <module_name> <n_rd> <n_wr> <single_clock 0|1>\n", argv[0]);
    return 2;
  }
  const std::string name = argv[1];
  const int         n_rd = std::atoi(argv[2]);
  const int         n_wr = std::atoi(argv[3]);
  const bool        sc   = std::atoi(argv[4]) != 0;
  if (n_rd <= 0 || n_wr <= 0) {
    std::fprintf(stderr, "n_rd and n_wr must be positive\n");
    return 2;
  }
  std::fputs(Cgen_verilog_test_peer::gen(name, n_rd, n_wr, sc).c_str(), stdout);
  return 0;
}
