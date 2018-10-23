
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "eprp_scanner.hpp"

#define DUMP_SCANNER

class Test_scanner : public Eprp_scanner {
public:
  void elaborate() {

    //printf("%s sz=%d pos=%d line=%d\n", buffer_name, buffer_sz, buffer_start_pos, buffer_start_line);

#ifdef DUMP_SCANNER
    while(!scan_is_end()) {
      std::string token;
      scan_append(token);
      fmt::print("tok={}\n", token);

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

  int fd = open(argv[1], O_RDONLY);
  if(fd < 0) {
    fprintf(stderr, "error, could not open %s\n", argv[1]);
    exit(-3);
  }

  struct stat sb;
  fstat(fd, &sb);
  printf("Size: %lu\n", (uint64_t)sb.st_size);

  char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
  if(memblock == MAP_FAILED) {
    fprintf(stderr, "error, mmap failed\n");
    exit(-3);
  }

  Test_scanner scanner;

  scanner.parse(argv[1], memblock, sb.st_size);
}
