//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lgraph_to_lnast.hpp"

#include "lbench.hpp"
#include "lgedgeiter.hpp"

void setup_pass_lgraph_to_lnast() { Pass_lgraph_to_lnast::setup(); }

void Pass_lgraph_to_lnast::setup() {
  Eprp_method m1("pass.lgraph_to_lnast", "translates LGraph to LNAST", &Pass_lgraph_to_lnast::trans);
  register_pass(m1);
}

Pass_lgraph_to_lnast::Pass_lgraph_to_lnast(const Eprp_var &var) : Pass("pass.lgraph_to_lnast", var) { }

void Pass_lgraph_to_lnast::trans(Eprp_var &var) {
  Pass_lgraph_to_lnast p(var);

  std::vector<const LGraph *> lgs;
  for (const auto &l : var.lgs) {
    p.do_trans(l);
  }
}

void Pass_lgraph_to_lnast::do_trans(LGraph *lg) {
  Lbench b("pass.lgraph_to_lnast");

  //pass_setup(lg);
  bool successful = true;
  //bool successful = iterate_over_lg();
  if (!successful) {
    error("Could not properly map LGraph to LNAST\n");//FIXME: Maybe later print out why
  }
}
