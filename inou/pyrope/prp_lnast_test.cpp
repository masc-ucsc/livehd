#include "prp_lnast.hpp"

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s file\n", argv[0]);
    exit(1);
  }
  Prp_lnast converter;
  
  converter.parse_file(argv[1]);
  converter.prp_ast_to_lnast();
  
  return 0;
}
