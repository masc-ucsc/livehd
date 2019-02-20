//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "pass_mockturtle.hpp"

void setup_pass_mockturtle() {
  Pass_mockturtle p;
  p.setup();
}

void Pass_mockturtle::setup() {
  Eprp_method m1("pass.mockturtle", "pass a lgraph using mockturtle", &Pass_mockturtle::work);

  register_pass(m1);
}

Pass_mockturtle::Pass_mockturtle()
    : Pass("mockturtle") {
}

void Pass_mockturtle::work(Eprp_var &var) {
  Pass_mockturtle pass;

  for(const auto &g : var.lgs) {
    pass.do_work(g);
  }
}

void Pass_mockturtle::do_work(const LGraph *g) {
  LGBench b("pass.mockturtle");

  std::map<std::string, int> histogram;

  int cells = 0;
  for(const auto &idx : g->forward()) {
    cells++;
    const auto &nt = g->node_type_get(idx);
    std::string name = nt.get_name();
    for(const auto &in_edge : g->inp_edges(idx)) {
      name.append("_i");
      name.append(std::to_string(in_edge.get_bits()));
    }
    for(const auto &out_edge : g->out_edges(idx)) {
      name.append("_o");
      name.append(std::to_string(out_edge.get_bits()));
    }

    histogram[name]++;
  }

  for(auto it=histogram.begin(); it != histogram.end(); it++) {
    fmt::print("{} {}\n",it->first,it->second);
  }

  fmt::print("Pass: cells {}\n", cells);
}
