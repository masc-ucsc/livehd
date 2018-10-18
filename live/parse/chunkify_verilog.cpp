
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <set>
#include <string>

#include "chunkify_verilog.hpp"

Chunkify_verilog::Chunkify_verilog(const std::string &outd) {

}

void Chunkify_verilog::elaborate() {

  //printf("%s sz=%d pos=%d line=%d\n", buffer_name, buffer_sz, buffer_start_pos, buffer_start_line);

  bool in_module=false;
  bool last_input = false;
  bool last_output = false;

  std::string module;

  std::string not_in_module_text; // This has to be cut&pasted to each file
  std::string in_module_text;

  while(!scan_is_end()) {
    bool endmodule_found=false;
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
          endmodule_found = true;
        }else{
          scan_error(fmt::format("found endmodule without corresponding module"));
        }
      }
    }else if (scan_is_token(TOK_COMMA) ||scan_is_token(TOK_SEMICOLON) || scan_is_token(TOK_CP)) {
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
    }else if (scan_is_token(TOK_COMMENT)) {
      // Drop comment, to avoid unneded recompilations
      scan_next();
      continue;
    }
    if (!in_module && !endmodule_found) {
      scan_format_append(not_in_module_text);
    }else{
      scan_format_append(in_module_text);
      if (endmodule_found) {
        fmt::print("module: {}\n{}\n{}\n", module, not_in_module_text, in_module_text);
        module.clear();
        in_module_text.clear();
      }
    }

    scan_next();
  }
}

