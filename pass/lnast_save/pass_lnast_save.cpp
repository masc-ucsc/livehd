//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnast_save.hpp"

#include "lnast_hif_writer.hpp"

#include <fstream>
#include <ostream>
#include <iostream>

static Pass_plugin sample("pass_lnast_save", Pass_lnast_save::setup);

Pass_lnast_save::Pass_lnast_save(const Eprp_var& var) : Pass("pass.lnast_save", var) {}

void Pass_lnast_save::setup() {
  Eprp_method m1("pass.lnast_save", "Serialize LNAST to HIF format", &Pass_lnast_save::do_work);
  register_pass(m1);
}

void Pass_lnast_save::do_work(const Eprp_var& var) {
  Pass_lnast_save pass(var);
  for (const auto &lnast : var.lnasts) {
    Lnast_hif_writer writer(lnast->get_top_module_name(), lnast);
    writer.write_all();
  }
}
