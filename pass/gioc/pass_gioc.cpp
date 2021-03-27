//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_gioc.hpp"

#include "gioc.hpp"

static Pass_plugin sample("pass_gioc", Pass_gioc::setup);

void Pass_gioc::setup() {
  Eprp_method m1("pass.gioc", "global IO connection between caller-callee", &Pass_gioc::connect);

  register_pass(m1);
}

Pass_gioc::Pass_gioc(const Eprp_var &var) : Pass("pass.gioc", var) {}

void Pass_gioc::connect(Eprp_var &var) {
  Pass_gioc p(var);
  auto      path = p.get_path(var);
  Gioc      gioc(path);

  for (auto &lg : var.lgs) {
    gioc.do_trans(lg);
  }
}
