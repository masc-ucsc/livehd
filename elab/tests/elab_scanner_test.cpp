//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "elab_scanner.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "lrand.hpp"

#define DUMP_SCANNER

class Test_scanner : public Elab_scanner {
public:
  void elaborate() {
    // printf("%s sz=%d pos=%d line=%d\n", buffer_name, buffer_sz, buffer_start_pos, buffer_start_line);

    Lrand_range<int> r_0_to_99(0,99);
    bool bug_inserted = false; //to ensure that there is only 1 bug per file
#ifdef DUMP_SCANNER
    auto last_line = 0u;
    while (!scan_is_end()) {

      auto line = scan_line();
      if (line != last_line) {
        fmt::print("\n");
      }
      last_line = line;

      auto tid = scan_token_id();
      auto ttxt = scan_text();
      if(!bug_inserted && tid == Token_id_semicolon && r_0_to_99.any()==59) {
        bug_inserted = true;
        err_tracker::err_logger("Missing Semicolon in line {}.\nSuggestion: Insert the missing semicolon.", line);
      } else if (!bug_inserted && tid == Token_id_cp && r_0_to_99.any()==40) {
        bug_inserted = true;
        err_tracker::err_logger("Missing closing parenthesis \")\" in line {}.\nSuggestion: Insert the missing closing parenthesis \")\".", line);
      } else if (!bug_inserted && tid == Token_id_op && r_0_to_99.any()==41) {
        bug_inserted = true;
        err_tracker::err_logger("Missing opening parenthesis \"(\" in line {}.\nSuggestion: Insert the missing opening parenthesis \"(\".", line);
      } else if (!bug_inserted && tid == Token_id_cbr && r_0_to_99.any()==93) {
        bug_inserted = true;
        err_tracker::err_logger("Missing closing bracket \"]\" in line {}.\nSuggestion: Insert the missing closing bracket \"]\".", line);
      } else if (!bug_inserted && tid == Token_id_obr && r_0_to_99.any()==91) {
        bug_inserted = true;
        err_tracker::err_logger("Missing opening bracket \"[\" in line {}.\nSuggestion: Insert the missing opening bracket \"(\".", line);
      } else if (!bug_inserted && tid == Token_id_colon && r_0_to_99.any()==58) {
        bug_inserted = true;
        err_tracker::err_logger("Missing colon \":\" in line {}.\nSuggestion: Insert the missing colon \":\".", line);
      } else if (!bug_inserted && tid == Token_id_comma && r_0_to_99.any()==44) {
        bug_inserted = true;
        err_tracker::err_logger("Missing comma \",\" in line {}.\nSuggestion: Insert the missing comma \",\".", line);
      } else if (!bug_inserted && tid == Token_id_plus && r_0_to_99.any()==43) {
        bug_inserted = true;
        err_tracker::err_logger("Missing plus operator \"+\" in line {}.\nSuggestion: Insert the missing plus operator \"+\".", line);
      } else if (!bug_inserted && tid == Token_id_eq && r_0_to_99.any()==61) {
        bug_inserted = true;
        err_tracker::err_logger("Missing assign sign \"=\" in line {}.\nSuggestion: Insert the missing assign sign \"=\".", line);
      } else if (!bug_inserted && tid == Token_id_alnum && ttxt == "module" && r_0_to_99.any()==98) {
        bug_inserted = true;
        err_tracker::err_logger("Missing keyword \"module\" in line {}.\nSuggestion: Insert the missing keyword \"module\".", line);
      } else if (!bug_inserted && tid == Token_id_alnum && ttxt == "endmodule" && r_0_to_99.any()==97) {
        bug_inserted = true;
        err_tracker::err_logger("Missing keyword \"endmodule\" in line {}.\nSuggestion: Insert the missing keyword \"endmodule\".", line);
      } else if (!bug_inserted && tid == Token_id_alnum && ttxt == "input" && r_0_to_99.any()==96) {
        bug_inserted = true;
        err_tracker::err_logger("Missing keyword \"input\" in line {}.\nSuggestion: Insert the missing keyword \"input\".", line);
      } else if (!bug_inserted && tid == Token_id_alnum && ttxt == "output" && r_0_to_99.any()==95) {
        bug_inserted = true;
        err_tracker::err_logger("Missing keyword \"output\" in line {}.\nSuggestion: Insert the missing keyword \"output\".", line);
      } else if (!bug_inserted && tid == Token_id_alnum && ttxt == "assign" && r_0_to_99.any()==94) {
        bug_inserted = true;
        err_tracker::err_logger("Missing keyword \"assign\" in line {}.\nSuggestion: Insert the missing keyword \"assign\".", line);
      } else {
        fmt::print("{}", ttxt);//scan_text());
      }

      if (scan_token_id() == Token_id_alnum) {
        fmt::print(" ");
      }

      scan_next();
    }
#endif
  }
};

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage:\n\t%s cfg\n", argv[0]);
    exit(-3);
  }

  Test_scanner scanner;

  scanner.parse_file(argv[1]);
}
