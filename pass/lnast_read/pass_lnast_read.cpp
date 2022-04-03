//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnast_read.hpp"

#include "lnast_parser.hpp"
#include "absl/strings/str_split.h"

#include <fstream>

static Pass_plugin sample("pass_lnast_read", Pass_lnast_read::setup);

Pass_lnast_read::Pass_lnast_read(Eprp_var& var) : Pass("pass.lnast_read", var) {}

void Pass_lnast_read::setup() {
  Eprp_method m1("pass.lnast_read", "Read from LNAST textual IR files and convert them to LNAST", &Pass_lnast_read::do_work);
  m1.add_label_optional("files", "LNAST textual IR files");
  register_pass(m1);
}

void Pass_lnast_read::do_work(Eprp_var& var) {
  Pass_lnast_read pass(var);
  for (const auto &file : absl::StrSplit(var.get("files"), ',')) {
    std::ifstream fs;
    fs.open(std::string(file));
    Lnast_parser parser(fs);
    var.add(parser.parse_all());
  }
}
