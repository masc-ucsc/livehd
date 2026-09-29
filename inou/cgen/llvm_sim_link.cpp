// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "cgen_llvm.hpp"

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: llvm_sim_link OUTPUT HOST_BITCODE KERNEL_BITCODE...\n");
    return 2;
  }
  std::vector<std::string> kernels;
  kernels.reserve(static_cast<size_t>(argc - 3));
  for (int i = 3; i < argc; ++i) {
    // Ninja lists the helper itself as an implicit input so changing its
    // optimization/lowering code invalidates warm native objects. `$in`
    // includes that dependency; it is not color bitcode.
    if (std::string_view(argv[i]) == argv[0]) {
      continue;
    }
    kernels.emplace_back(argv[i]);
  }
  std::string error;
  if (!Cgen_llvm::link_bitcode_object(argv[2], kernels, argv[1], error)) {
    std::fprintf(stderr, "llvm_sim_link: %s\n", error.c_str());
    return 1;
  }
  return 0;
}
