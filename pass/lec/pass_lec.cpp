//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lec.hpp"

#include "annotate.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void setup_pass_lec() { Pass_lec::setup(); }

void Pass_lec::setup() {
  Eprp_method m1("pass.lec", "Checks if all the LGraph outputs are satisfiable", &Pass_lec::work);

  register_pass(m1);
}

Pass_lec::Pass_lec(const Eprp_var &var) : Pass("pass.lec", var) {}

void Pass_lec::do_work(LGraph *g) {
  check_lec(g);
}

void Pass_lec::work(Eprp_var &var) {
  Pass_lec p(var);

  for (const auto &g : var.lgs) {
    p.do_work(g);
  }
}

void Pass_lec::check_lec(LGraph *g) {
  fmt::print("TODO: implement LEC\n");

  for (const auto node : g->forward()) {
    fmt::print("node type:{}\n", node.get_type().get_name());
  }
}

