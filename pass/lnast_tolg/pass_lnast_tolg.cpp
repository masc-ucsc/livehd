// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_lnast_tolg.hpp"

/* void setup_pass_lnast_tolg() { Pass_lnast_tolg::setup(); } */
static Pass_plugin sample("pass.lnast_tolg", Pass_lnast_tolg::setup);

void Pass_lnast_tolg::setup() {
  Eprp_method m1("pass.lnast_tolg", " front-end language lnast -> lgraph", &Pass_lnast_tolg::tolg);
  m1.add_label_optional("path", "path to output the lgraph[s] to", "lgdb");
  register_pass(m1);

  Eprp_method m2("pass.lnast_tolg.dbg_lnast_ssa",
                 " perform the LNAST SSA transformation, only for debug purpose",
                 &Pass_lnast_tolg::dbg_lnast_ssa);
  register_pass(m2);
}

Pass_lnast_tolg::Pass_lnast_tolg(const Eprp_var &var) : Pass("pass.lnast_tolg", var) {}

void Pass_lnast_tolg::dbg_lnast_ssa(Eprp_var &var) {
  for (const auto &lnast : var.lnasts) lnast->ssa_trans();
}

void Pass_lnast_tolg::tolg(Eprp_var &var) {
  /* Lbench b1("pass.lnast_tolg.ssa"); */
  Pass_lnast_tolg p(var);
  auto            path = p.get_path(var);

  for (const auto &lnast : var.lnasts) {
    lnast->ssa_trans();
  }

  /* Lbench b2("pass.lnast_tolg.tolg"); */
  std::vector<Lgraph *> lgs;
  for (const auto &ln : var.lnasts) {
    auto       module_name = ln->get_top_module_name();
    const auto top         = ln->get_root();
    const auto top_stmts   = ln->get_first_child(top);

    Lnast_tolg pp(module_name, path);
    lgs = pp.do_tolg(ln, top_stmts);
    var.add(lgs);
  }

  if (lgs.empty()) {
    error("failed to generate any lgraph from lnast");
  }
}
