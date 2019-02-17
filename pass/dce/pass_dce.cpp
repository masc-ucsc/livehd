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

  bm::bvector<>      output_used;
  std::set<Index_ID> pending;

  g->each_output([&pending](const Node_pin &pin) {
      pending.insert(pin.get_idx());
    });

  while(!pending.empty()) {
    Index_ID current = *(pending.begin());
    pending.erase(current);
    output_used.set_bit(current);

    for(auto &c : g->inp_edges(current)) {
      if(output_used.get_bit(c.get_out_pin().get_idx()))
        continue;

      pending.insert(c.get_out_pin().get_idx());
    }
  }

  for(auto idx : g->fast()) {
    if(output_used.get_bit(idx))
      continue;

    g->del_node(idx);
  }

  g->sync();
}
