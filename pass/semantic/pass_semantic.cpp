//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_semantic.hpp"

#include "lbench.hpp"
#include "lgraph.hpp"
#include "semantic_check.hpp"

static Pass_plugin semantic("pass_semantic", Pass_semantic::setup);

void Pass_semantic::setup() {
  Eprp_method m1("pass.semantic", "semantic check for LANST", &Pass_semantic::work);

  register_pass(m1);
}

Pass_semantic::Pass_semantic(const Eprp_var &var) : Pass("pass.semantic", var) {}

void Pass_semantic::do_work(LGraph *g) { fmt::print("future lgraph semantic check\n"); }

void Pass_semantic::do_work(std::shared_ptr<Lnast> lnast) {
  Semantic_check p;

  p.do_check(lnast.get());
}

void Pass_semantic::work(Eprp_var &var) {
  Lbench        b("pass.semantic");
  Pass_semantic p(var);

  for (const auto &g : var.lgs) {
    p.do_work(g);
  }

  for (const auto &l : var.lnasts) {
    p.do_work(l);
  }
}
