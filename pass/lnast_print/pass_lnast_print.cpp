//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnast_print.hpp"

#include "lnast_writer.hpp"

#include <fstream>
#include <ostream>
#include <iostream>

static Pass_plugin sample("pass_lnast_print", Pass_lnast_print::setup);

Pass_lnast_print::Pass_lnast_print(const Eprp_var& var) : Pass("pass.lnast_print", var) {}

void Pass_lnast_print::setup() {
  Eprp_method m1("pass.lnast_print", "Print LNAST in textual form to the terminal or a specified file", &Pass_lnast_print::do_work);
  m1.add_label_optional("odir", "output directory");
  register_pass(m1);
}

void Pass_lnast_print::do_work(const Eprp_var& var) {
  Pass_lnast_print pass(var);
  auto odir = pass.get_odir(var);
  bool has_file_output = (odir != "/INVALID");
  for (const auto &lnast : var.lnasts) {
    std::fstream fs;
    if (has_file_output) {
      fs.open(absl::StrCat(odir, "/", lnast->get_top_module_name(), ".ln"), std::fstream::out);
    }
    std::ostream &os = (has_file_output) ? fs : std::cout;
    Lnast_writer writer(os, lnast);
    writer.write_all();
  }
}
