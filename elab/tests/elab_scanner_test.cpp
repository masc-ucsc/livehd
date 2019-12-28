//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "elab_scanner.hpp"

#define DUMP_SCANNER

class Test_scanner : public Elab_scanner {
public:
  void elaborate() {

    //printf("%s sz=%d pos=%d line=%d\n", buffer_name, buffer_sz, buffer_start_pos, buffer_start_line);

#ifdef DUMP_SCANNER
    while(!scan_is_end()) {
      fmt::print("tok={}\n", scan_text());

      scan_next();
    }
#endif
  }
};

int main(int argc, char **argv) {

  if(argc != 2) {
    fprintf(stderr, "Usage:\n\t%s cfg\n", argv[0]);
    exit(-3);
  }

  Test_scanner scanner;

  scanner.parse_file(argv[1]);
}
