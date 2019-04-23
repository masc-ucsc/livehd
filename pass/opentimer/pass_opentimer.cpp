//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgbench.hpp"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_opentimer.hpp"

#include "ot/timer/timer.hpp"

void setup_pass_opentimer() {
  Pass_opentimer p;
  p.setup();
}

void Pass_opentimer::setup() {
  Eprp_method m1("pass.opentimer", "timing analysis on lgraph", &Pass_opentimer::work);
  register_pass(m1);
}

Pass_opentimer::Pass_opentimer()
    : Pass("opentimer") {
}

void Pass_opentimer::work(Eprp_var &var) {
  Pass_opentimer pass;

  fmt::print("OpenTimer-LGraph Action Going On...\n");

  ot::Timer timer;

  for(const auto &g : var.lgs) {
    pass.list_cells(g);
  }
}

void Pass_opentimer::list_cells(LGraph *g) {
  LGBench b("pass.opentimer.list_cells");

  for(const auto &nid : g -> forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished
    std::string name (node.get_type().get_name());
    fmt::print("Cell\t{}\n", name);
  }
}
