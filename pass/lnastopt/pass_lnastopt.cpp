//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnastopt.hpp"

#include <string>

#include "opt_lnast.hpp"

static Pass_plugin lnastopt("pass_lnastopt", Pass_lnastopt::setup);

void Pass_lnastopt::setup() {
  Eprp_method m1("pass.lnastopt", mmap_lib::str("LNAST optimization"), &Pass_lnastopt::work);

  register_pass(m1);
}

Pass_lnastopt::Pass_lnastopt(const Eprp_var &var) : Pass("pass.lnastopt", var) {}

void Pass_lnastopt::work(Eprp_var &var) {
  Opt_lnast p(var);

  for (const auto &ln : var.lnasts) {
    p.opt(ln);
    Lnast_create ln2;
    ln2.new_lnast("ln2");
    p.reconstruct(ln, ln2);
    ln2.lnast->dump();
  }
}
