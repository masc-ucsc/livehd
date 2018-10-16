//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>
#include <time.h>

#include "bm.h"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_dce.hpp"

Pass_dce::Pass_dce()
    : Pass("dce") {
}

void Pass_dce::trans(LGraph *g) {

  bm::bvector<>      output_used;
  std::set<Index_ID> pending;

  for(auto idx : g->fast()) {
    if(g->is_graph_output(idx)) {
      pending.insert(idx);
    }
  }

  while(!pending.empty()) {
    Index_ID current = *(pending.begin());
    pending.erase(current);
    output_used.set_bit(current);

    for(auto &c : g->inp_edges(current)) {
      if(output_used.get_bit(c.get_out_pin().get_nid()))
        continue;

      pending.insert(c.get_out_pin().get_nid());
    }
  }

  /*for(auto idx:g->backward()) {
    output_used.set_bit(idx);
    for(const auto &c:g->inp_edges(idx)) {
      output_used.set_bit(c.get_idx());
    }
    fmt::print("dce using {}\n",idx);
  }*/

  for(auto idx : g->fast()) {
    if(output_used.get_bit(idx))
      continue;

    g->del_node(idx);
  }

  g->sync();
}
