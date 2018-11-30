//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// YAML to YAML graph conversion

#include "import_verilog.hpp"

#include <fstream>
#include <iostream>

#include <regex>

// FIXME: consider replacing with a simple lex/yacc parser?
// This only parses
void Import_verilog::update() {

  assert(false); // not implemented

  fmt::print("update\n");
  std::ifstream input_file(opack.file_path);

  if(!input_file.good()) {
    Pass::error("Import_verilog: unable to open technology file {}", opack.file_path);
  }

  input_file.close();
}
