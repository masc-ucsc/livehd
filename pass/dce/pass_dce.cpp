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

  bm::bvector<>  cell_used;
  //std::set<Node> pending;
  absl::flat_hash_set<Node> pending;

  g->each_graph_output([&pending](const Node_pin &pin) {
      pending.insert(pin.get_node());
    });

  while(!pending.empty()) {
    Node cur_node = *(pending.begin());
    pending.erase(cur_node);
    //auto node = g->get_node(current); //already in Node form
    //SH:FIXME:ASK: how to use "Node" to index a container which requires an integer?
    //cell_used.set_bit(cur_node.get_compact());
    cell_used.set_bit((bm::id_t)cur_node.get_compact());

    for(auto &inp : cur_node.inp_edges()) {
      //if(cell_used.get_bit(g->get_node(inp.get_out_pin()).get_nid()))
      if(cell_used.get_bit( (bm::id_t)inp.driver.get_node().get_compact()))
        continue;

      pending.insert(inp.driver.get_node());
    }
  }

  for(auto nid : g->fast()) {
    auto node = Node(g, 0, Node::Compact(nid));
    if(cell_used.get_bit( (bm::id_t)node.get_compact()))
      continue;
    node.del_node();
  }
  g->sync();
}
