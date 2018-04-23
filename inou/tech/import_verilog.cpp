
// YAML to YAML graph conversion

#include "import_verilog.hpp"

#include <iostream>
#include <fstream>

#include <regex>


//FIXME: consider replacing with a simple lex/yacc parser?
//This only parses
void Import_verilog::update() {

  assert(false); //not implemented

  fmt::print("update\n");
  std::ifstream input_file(opack.file_path);

  if(!input_file.good()) {
    console->error("Unable to open technology file {}\n",opack.file_path);
  }


  input_file.close();
}

