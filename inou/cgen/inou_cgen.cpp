//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_cgen.hpp"
#include "eprp_utils.hpp"
#include "cgen_verilog.hpp"

static Pass_plugin sample("inou_cgen", Inou_cgen::setup);

Inou_cgen::Inou_cgen(const Eprp_var &var) : Pass("inou.cgen", var) {
  auto v  = var.get("verbose");
  verbose = v != "false" && v != "0";
}

void Inou_cgen::setup() {
  Eprp_method m1("inou.cgen.verilog", "export verilog from an Lgraph", &Inou_cgen::to_cgen_verilog);

  m1.add_label_optional("verbose", "dump bits and wirename (true/false)", "false");
  register_inou("cgen", m1);
}

void Inou_cgen::to_cgen_verilog(Eprp_var &var) {
  Inou_cgen pp(var);

  Cgen_verilog p(pp.verbose, pp.get_odir(var));

  for (const auto &l : var.lgs) {
    p.do_from_lgraph(l);
  }
}

