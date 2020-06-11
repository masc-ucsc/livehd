//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <stdio.h>

#include "lbench.hpp"
#include "prp.hpp"

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s file\n", argv[0]);
    exit(1);
  }

  Lbench bench("prp_test");

  Prp  scanner;

  scanner.parse_file(argv[1]);

  return 0;
}
