
#include <time.h>
#include <string>

#include <bm.h>

#include "pass_dce.hpp"
#include "lgraph.hpp"
#include "lgedgeiter.hpp"

Pass_dce::Pass_dce()
  : Pass("dce") {
}

void Pass_dce::transform(LGraph *g) {

  bm::bvector<> output_used;

  for(auto idx:g->backward()) {
    output_used.set_bit(idx);
    for(const auto &c:g->inp_edges(idx)) {
      output_used.set_bit(c.get_idx());
    }
  }

  for(auto idx:g->fast()) {
    if (output_used.get_bit(idx))
      continue;

    bool deleted;
    do {
      deleted = false;
      for(const auto &c:g->out_edges(idx)) {
        g->del_edge(c);
        deleted = true;
      }
    }while(deleted);

    do {
      deleted = false;
      for(const auto &c:g->inp_edges(idx)) {
        g->del_edge(c);
        deleted = true;
      }
    }while(deleted); // Delete can mess the iterator, try again
  }

  g->sync();

}

