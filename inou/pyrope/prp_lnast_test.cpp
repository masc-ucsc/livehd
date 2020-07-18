//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prp_lnast.hpp"

#include "lbench.hpp"

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s file\n", argv[0]);
    exit(1);
  }
  Prp_lnast converter;

  Lbench b("prp_lnast_test.parse");
  converter.parse_file(argv[1]);
  b.end();

  Lbench b2("prp_last_test.convert");
  auto   lnast = converter.prp_ast_to_lnast("test");
  b2.end();

  fmt::print("AST to LNAST output:\n\n");
  lnast->dump();

  return 0;
}
