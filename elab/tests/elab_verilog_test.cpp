//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <set>
#include <string>

#include "elab_scanner.hpp"

class Verilog_scanner : public Elab_scanner {
  std::set<std::string> verilog_keyword;
public:
  Verilog_scanner() {
    // Verilog preproc
    verilog_keyword.insert("ifdef");
    verilog_keyword.insert("ifdef");
    verilog_keyword.insert("endif");
    // Verilog
    verilog_keyword.insert("always");
    verilog_keyword.insert("and");
    verilog_keyword.insert("assign");
    verilog_keyword.insert("begin");
    verilog_keyword.insert("buf");
    verilog_keyword.insert("bufif0");
    verilog_keyword.insert("bufif1");
    verilog_keyword.insert("case");
    verilog_keyword.insert("casex");
    verilog_keyword.insert("casez");
    verilog_keyword.insert("cmos");
    verilog_keyword.insert("deassign");
    verilog_keyword.insert("default");
    verilog_keyword.insert("defparam");
    verilog_keyword.insert("disable");
    verilog_keyword.insert("edge");
    verilog_keyword.insert("else");
    verilog_keyword.insert("end");
    verilog_keyword.insert("endcase");
    verilog_keyword.insert("endfunction");
    verilog_keyword.insert("endmodule");
    verilog_keyword.insert("endprimitive");
    verilog_keyword.insert("endspecify");
    verilog_keyword.insert("endtable");
    verilog_keyword.insert("endtask");
    verilog_keyword.insert("event");
    verilog_keyword.insert("for");
    verilog_keyword.insert("force");
    verilog_keyword.insert("forever");
    verilog_keyword.insert("fork");
    verilog_keyword.insert("function");
    verilog_keyword.insert("highz0");
    verilog_keyword.insert("highz1");
    verilog_keyword.insert("if");
    verilog_keyword.insert("initial");
    verilog_keyword.insert("initivar");
    verilog_keyword.insert("inout");
    verilog_keyword.insert("input");
    verilog_keyword.insert("integer");
    verilog_keyword.insert("join");
    verilog_keyword.insert("large");
    verilog_keyword.insert("macromodule");
    verilog_keyword.insert("medium");
    verilog_keyword.insert("module");
    verilog_keyword.insert("nand");
    verilog_keyword.insert("negedge");
    verilog_keyword.insert("nmos");
    verilog_keyword.insert("nor");
    verilog_keyword.insert("not");
    verilog_keyword.insert("notif0");
    verilog_keyword.insert("notif1");
    verilog_keyword.insert("or");
    verilog_keyword.insert("output");
    verilog_keyword.insert("pmos");
    verilog_keyword.insert("posedge");
    verilog_keyword.insert("primitive");
    verilog_keyword.insert("pull0");
    verilog_keyword.insert("pull1");
    verilog_keyword.insert("pulldown");
    verilog_keyword.insert("pullup");
    verilog_keyword.insert("rcmos");
    verilog_keyword.insert("reg");
    verilog_keyword.insert("release");
    verilog_keyword.insert("repeat");
    verilog_keyword.insert("rnmos");
    verilog_keyword.insert("rpmos");
    verilog_keyword.insert("rtran");
    verilog_keyword.insert("rtranif0");
    verilog_keyword.insert("rtranif1");
    verilog_keyword.insert("scalared");
    verilog_keyword.insert("small");
    verilog_keyword.insert("specify");
    verilog_keyword.insert("specparam");
    verilog_keyword.insert("strong0");
    verilog_keyword.insert("strong1");
    verilog_keyword.insert("supply0");
    verilog_keyword.insert("supply1");
    verilog_keyword.insert("table");
    verilog_keyword.insert("task");
    verilog_keyword.insert("time");
    verilog_keyword.insert("tran");
    verilog_keyword.insert("tranif0");
    verilog_keyword.insert("tranif1");
    verilog_keyword.insert("tri");
    verilog_keyword.insert("tri0");
    verilog_keyword.insert("tri1");
    verilog_keyword.insert("triand");
    verilog_keyword.insert("trior");
    verilog_keyword.insert("vectored");
    verilog_keyword.insert("wait");
    verilog_keyword.insert("wand");
    verilog_keyword.insert("weak0");
    verilog_keyword.insert("weak1");
    verilog_keyword.insert("while");
    verilog_keyword.insert("wire");
    verilog_keyword.insert("wor");
    verilog_keyword.insert("xnor");
    verilog_keyword.insert("xor");
  }
  void elaborate() {

    //printf("%s sz=%d pos=%d line=%d\n", buffer_name, buffer_sz, buffer_start_pos, buffer_start_line);

    bool last_input = false;
    bool last_output = false;

    std::string_view module;
    while(!scan_is_end()) {
      if (scan_is_token(Token_id_alnum)) {
        std::string token{scan_text()};

        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c){ return std::tolower(c); }); // verilog is can insensitive

        if (token == "module") {
          if (!module.empty()) {
            scan_error(fmt::format("unexpected nested modules"));
          }
          scan_next();
          module = scan_text();
        }else if (token == "input") {
          last_input = true;
        }else if (token == "output") {
          last_output = true;
        }else if (token == "endmodule") {
          if (module.size()) {
            fmt::print("{}={}\n", token, module);
            module = std::string_view("");
          }else{
            scan_error(fmt::format("found endmodule without corresponding module"));
          }
        }
      }else if (scan_is_token(Token_id_comma) ||scan_is_token(Token_id_semicolon) || scan_is_token(Token_id_cp)) {
        if (last_input || last_output) {
          if (scan_is_prev_token(Token_id_alnum)) {
            auto label = scan_prev_text();

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

  Verilog_scanner scanner;

  scanner.parse_file(argv[1]);
}
