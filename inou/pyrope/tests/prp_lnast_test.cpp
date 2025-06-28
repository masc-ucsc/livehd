//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prp_lnast.hpp"
#include "perf_tracing.hpp"

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s file\n", argv[0]);
    exit(1);
  }
  Prp_lnast converter;

  TRACE_EVENT("inou", "PYROPE_prp_lnast_parse");
  converter.parse_file(argv[1]);

  TRACE_EVENT("inou", "PYROPE_prp_lnast_convert");
  auto   lnast = converter.prp_ast_to_lnast("test");

  std::cout << "AST to LNAST output:\n\n";

  lnast->dump();

  return 0;
}
