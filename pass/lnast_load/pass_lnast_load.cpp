//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnast_load.hpp"

#include <fstream>
#include <iostream>
#include <ostream>

#include "lnast_hif_reader.hpp"
#include "lnast_ntype.hpp"

static Pass_plugin sample("pass_lnast_load", Pass_lnast_load::setup);

Pass_lnast_load::Pass_lnast_load(const Eprp_var& var) : Pass("pass.lnast_load", var) {}

void Pass_lnast_load::setup() {
  Eprp_method m1("pass.lnast_load", "Load from HIF to LNAST", &Pass_lnast_load::do_work);
  m1.add_label_required("files", "HIF directory");
  register_pass(m1);
}

void Pass_lnast_load::do_work(Eprp_var& var) {
  Pass_lnast_load pass(var);
  auto            files = var.get("files");
  for (const auto& file : absl::StrSplit(files, ",")) {
    auto             lnast = std::make_unique<Lnast>(file);
    Lnast_hif_reader reader(file);
    var.add(reader.read_all());
  }
}
