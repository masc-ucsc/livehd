
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "eprp_scanner.hpp"

class Verilog_scanner : public Eprp_scanner {
public:
  void elaborate() {

    //printf("%s sz=%d pos=%d line=%d\n", buffer_name, buffer_sz, buffer_start_pos, buffer_start_line);

    bool in_module=false;
    bool last_input = false;
    bool last_output = false;

    std::string module;
    while(!scan_is_end()) {
      if (scan_is_token(TOK_ALNUM)) {
        std::string token;
        scan_append(token);
        if (strcasecmp(token.c_str(),"module")==0) {
          if (in_module) {
            scan_error(fmt::format("unexpected nested modules"));
          }
          scan_next();
          scan_append(module);
          in_module = true;
        }else if (strcasecmp(token.c_str(),"input")==0) {
          last_input = true;
        }else if (strcasecmp(token.c_str(),"output")==0) {
          last_output = true;
        }else if (strcasecmp(token.c_str(),"endmodule")==0) {
          if (in_module) {
            in_module = false;
            fmt::print("{}={}\n", token, module);
            module.clear();
          }else{
            scan_error(fmt::format("found endmodule without corresponding module"));
          }
        }
        // IdentityModule IdentityModule (
      }else if (scan_is_token(TOK_COMMA) ||scan_is_token(TOK_SEMICOLON)) {
        if (last_input || last_output) {
          if (scan_is_prev_token(TOK_ALNUM)) {
            std::string label;
            scan_prev_append(label);

            if (last_input)
              fmt::print("  inp {}\n",label);
            else
              fmt::print("  out {}\n",label);
          }
          last_input = false;
          last_output = false;
        }
      }

      scan_next();
    }
  }
};

int main(int argc, char **argv) {

  if(argc != 2) {
    fprintf(stderr, "Usage:\n\t%s file\n", argv[0]);
    exit(-3);
  }

  int fd = open(argv[1], O_RDONLY);
  if(fd < 0) {
    fprintf(stderr, "error, could not open %s\n", argv[1]);
    exit(-3);
  }

  struct stat sb;
  fstat(fd, &sb);

  char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
  if(memblock == MAP_FAILED) {
    fprintf(stderr, "error, mmap failed\n");
    exit(-3);
  }

  Verilog_scanner scanner;

  scanner.parse(argv[1], memblock, sb.st_size);
}
