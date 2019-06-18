//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>
#include <time.h>

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

  absl::flat_hash_set<Node::Compact> cell_used;
  absl::flat_hash_set<Node> pending;

  g->each_graph_output([&pending](const Node_pin &pin) {
      pending.insert(pin.get_node());
    });

  while(!pending.empty()) {
    auto it = pending.begin();
    Node cur_node = *it;
    pending.erase(it);
    cell_used.insert(cur_node.get_compact());

    for(auto &inp : cur_node.inp_edges()) {
      if(cell_used.count(inp.driver.get_node().get_compact()))
        continue;

      pending.insert(inp.driver.get_node());
    }
  }

  for(auto node : g->fast()) {
    if(cell_used.count(node.get_compact()))
      continue;
    node.del_node();
  }
  g->sync();
}
