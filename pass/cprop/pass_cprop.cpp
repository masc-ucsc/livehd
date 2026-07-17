//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_cprop.hpp"

#include "cprop.hpp"

static Pass_plugin sample("pass_cprop", Pass_cprop::setup);

void Pass_cprop::setup() {
  Eprp_method m1("pass.cprop", "in-place copy propagation", &Pass_cprop::optimize);

  register_pass(m1);
}

Pass_cprop::Pass_cprop(const Eprp_var& var) : Pass("pass.cprop", var) {}

void Pass_cprop::optimize(Eprp_var& var) {
  Pass_cprop pcp(var);
  Cprop      cp;

  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    cp.do_trans(g);
  }
}
