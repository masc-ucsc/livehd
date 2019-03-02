//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>
#include <time.h>

#include "bm.h"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_dce.hpp"

void setup_pass_dce() {
  Pass_dce p;
  p.setup();
}

void Pass_dce::setup() {
  Eprp_method m1("pass.dce", "optimize an lgraph with a dce, gen _mapped", &Pass_dce::optimize);

  register_pass(m1);
}

Pass_dce::Pass_dce()
    : Pass("dce") {
}

void Pass_dce::optimize(Eprp_var &var) {
  Pass_dce pass;

  for(auto &l : var.lgs) {
    pass.trans(l);
  }
}

void Pass_dce::trans(LGraph *g) {

  bm::bvector<>      cell_used;
  std::set<Index_ID> pending;

  g->each_graph_output([&pending](const Node_pin &pin) {
      pending.insert(pin.get_idx());
    });

  while(!pending.empty()) {
    Index_ID current = *(pending.begin());
    pending.erase(current);
    auto node = g->get_node(current);
    cell_used.set_bit(node.get_nid());

    for(auto &c : g->inp_edges(node.get_nid())) {
      if(cell_used.get_bit(g->get_node(c.get_out_pin()).get_nid()))
        continue;

      pending.insert(c.get_out_pin().get_idx());
    }
  }

  for(auto nid : g->fast()) {
    if(cell_used.get_bit(nid))
      continue;

    g->del_node(nid);
  }

  g->sync();
}
