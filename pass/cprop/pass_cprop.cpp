//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "pass_cprop.hpp"
#include "cprop.hpp"


static Pass_plugin sample("pass_cprop", Pass_cprop::setup);

void Pass_cprop::setup() {
  Eprp_method m1("pass.cprop", "in-place copy propagation", &Pass_cprop::optimize);
  m1.add_label_optional("hier", "hierarchical bitwidth", "false");

  register_pass(m1);
}

Pass_cprop::Pass_cprop(const Eprp_var &var) : Pass("pass.cprop", var) {
  auto hier_txt = var.get("hier");

  if (hier_txt != "false" && hier_txt != "0")
    hier = true;
  else
    hier = false;
}

void Pass_cprop::optimize(Eprp_var &var) {
  Pass_cprop pcp(var);
  Cprop cp(pcp.hier);

  for (auto &lg : var.lgs) {
    cp.do_trans(lg);
  }
}


