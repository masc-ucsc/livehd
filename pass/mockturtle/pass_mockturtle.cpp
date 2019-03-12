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

  std::unordered_map<int,std::pair<std::string,std::pair<int,int>>> cell_type;

  for(const auto &idx : g->forward()) {
    const auto &nt = g->node_type_get(idx);
    std::string name = nt.get_name();
    int in_edges_num = 0;
    for(const auto &in_edge : g->inp_edges(idx)) {
      //name.append(std::to_string(in_edge.get_bits()));
      in_edges_num++;
    }
    int out_edges_num = 0;
    for(const auto &out_edge : g->out_edges(idx)) {
      //name.append(std::to_string(out_edge.get_bits()));
      out_edges_num++;
    }
    cell_type[idx]=std::make_pair(name,std::make_pair(in_edges_num,out_edges_num));
  }

  fmt::print("Pass: number of cells {}\n", cell_type.size());

  for(auto const it:cell_type) {
    fmt::print("node_id:{} node_type:{} in_edges:{} out_edges:{}\n", it.first, it.second.first, it.second.second.first, it.second.second.second);
  }
}
