//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgbench.hpp"
#include "lgraph.hpp"

#include "pass_sample.hpp"

void setup_pass_sample() {
  Pass_sample p;
  p.setup();
}

void Pass_sample::setup() {
  Eprp_method m1("pass.sample", "counts number of nodes in an lgraph", &Pass_sample::work);

  register_pass(m1);
}

Pass_sample::Pass_sample()
 : Pass("sample") {
}

void Pass_sample::work(Eprp_var &var) {
  Pass_sample pass;

  for(const auto &g:var.lgs) {
    pass.do_work(g);
  }

}
void Pass_sample::do_work(const LGraph *g) {
  LGBench b("pass.sample");

  int cells = 0;
  for(const auto& idx : g->forward()) {
    (void)idx;
    cells++;
  }

  fmt::print("Pass: cells {}\n",cells);
}

