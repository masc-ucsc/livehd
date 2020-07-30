// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "inou_lnast_dfg.hpp"


void setup_inou_lnast_dfg() { Inou_lnast_dfg::setup(); }

void Inou_lnast_dfg::setup() {

  Eprp_method m1("inou.lnast_dfg.tolg", " front-end language lnast -> lgraph", &Inou_lnast_dfg::tolg);
  m1.add_label_optional("path", "path to output the lgraph[s] to", "lgdb");
  register_pass(m1);

  Eprp_method m2("inou.lnast_dfg.dbg_lnast_ssa", " perform the LNAST SSA transformation, only for debug purpose", &Inou_lnast_dfg::dbg_lnast_ssa);
  register_pass(m2);
}


void Inou_lnast_dfg::dbg_lnast_ssa(Eprp_var &var) {
  for (const auto &lnast : var.lnasts) {
    lnast->ssa_trans();
  }
}


void Inou_lnast_dfg::tolg(Eprp_var &var) {
  Lbench b1("inou.lnast_dfg.ssa");
  for (const auto &lnast : var.lnasts) {
    lnast->ssa_trans();
  }

  Lbench b2("inou.lnast_dfg.tolg");
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



