// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_lnast_dfg.hpp"


void setup_pass_lnast_dfg() { Pass_lnast_dfg::setup(); }

void Pass_lnast_dfg::setup() {

  Eprp_method m1("pass.lnast_dfg", " front-end language lnast -> lgraph", &Pass_lnast_dfg::tolg);
  m1.add_label_optional("path", "path to output the lgraph[s] to", "lgdb");
  register_pass(m1);

  Eprp_method m2("pass.lnast_dfg.dbg_lnast_ssa", " perform the LNAST SSA transformation, only for debug purpose", &Pass_lnast_dfg::dbg_lnast_ssa);
  register_pass(m2);
}


void Pass_lnast_dfg::dbg_lnast_ssa(Eprp_var &var) {
  for (const auto &lnast : var.lnasts) {
    lnast->ssa_trans();
  }
}


void Pass_lnast_dfg::tolg(Eprp_var &var) {
  Lbench b1("pass.lnast_dfg.ssa");
  for (const auto &lnast : var.lnasts) {
    lnast->ssa_trans();
  }

  Lbench b2("pass.lnast_dfg.tolg");
  std::vector<LGraph *> lgs;
  for (const auto &ln : var.lnasts) {
    auto module_name = ln->get_top_module_name();
    const auto top = ln->get_root();
    const auto top_stmts = ln->get_first_child(top);
    Lnast_dfg p(var, module_name);
    lgs = p.do_tolg(ln, top_stmts);
  }

  if (lgs.empty()) {
    error("failed to generate any lgraph from lnast");
  } else {
    var.add(lgs);
  }
}



