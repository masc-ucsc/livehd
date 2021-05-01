//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>

#include "pass_lnastopt.hpp"
#include "opt_lnast.hpp"

static Pass_plugin lnastopt("pass_lnastopt", Pass_lnastopt::setup);

void Pass_lnastopt::setup() {
  Eprp_method m1("pass.lnastopt", "LNAST optimization", &Pass_lnastopt::work);

  register_pass(m1);
}

Pass_lnastopt::Pass_lnastopt(const Eprp_var &var) : Pass("pass.lnastopt", var) {}

void Pass_lnastopt::work(Eprp_var &var) {
  Opt_lnast p(var);

  for (const auto &ln : var.lnasts) {
    p.opt(ln);
  }
}

