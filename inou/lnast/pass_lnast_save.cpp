//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnast_save.hpp"

#include <fstream>
#include <iostream>
#include <ostream>

#include "lnast_hif_writer.hpp"

static Pass_plugin sample("lnast.save", Pass_lnast_save::setup);

Pass_lnast_save::Pass_lnast_save(const Eprp_var& var) : Pass("lnast.save", var) {}

void Pass_lnast_save::setup() {
  Eprp_method m1("lnast.save", "Serialize LNAST to HIF format", &Pass_lnast_save::do_work);
  m1.add_label_optional("odir", "Output Directory");
  register_pass(m1);
}

void Pass_lnast_save::do_work(const Eprp_var& var) {
  Pass_lnast_save pass(var);
  auto            odir = pass.get_odir(var);
  odir                 = (odir == "/INVALID") ? "." : odir;
  for (const auto& lnast : var.lnasts) {
    Lnast_hif_writer writer(absl::StrCat(odir, "/", lnast->get_top_module_name()), lnast);
    writer.write_all();
  }
}
