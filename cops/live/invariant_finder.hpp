//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef INVARIANT_FINDER_H_
#define INVARIANT_FINDER_H_

#include "bm.h"

#include "invariant.hpp"
#include "invariant_options.hpp"
#include "lgraph.hpp"

class Invariant_finder {

private:
  LGraph *             elab_graph;
  LGraph *             synth_graph;
  Invariant_boundaries boundaries;

  bool          processed;
  bm::bvector<> stack;

  typedef std::pair<Index_ID, uint32_t> Node_bit;
  std::map<Node_bit, Gate_set>          partial_cone_cells; // partial_gate_count
  std::map<Node_bit, Net_set>           partial_endpoints;  //sips

  //there is a delay between allocation of the cache and populating it
  std::set<Node_bit>                    cached;

#ifndef NDEBUG
  std::set<Node_bit>                    deleted;
#endif

  void get_topology();

  void find_invariant_boundaries();

  void propagate_until_boundary(Index_ID nid, uint32_t bit_selection);
  void clear_cache(const Node_bit &entry);

public:
  Invariant_finder(LGraph *elab, LGraph *synth, const std::string &hier_sep = ".") : boundaries(hier_sep) {
    processed   = false;
    elab_graph  = elab;
    synth_graph = synth;
  }

  Invariant_finder(const Invariant_find_options &pack) : boundaries(pack.hierarchical_separator) {
    processed   = false;
    elab_graph  = LGraph::open(pack.elab_lgdb, pack.top);
    synth_graph = LGraph::open(pack.synth_lgdb, pack.top);
  }

  const Invariant_boundaries& get_boundaries() {
    if(!processed) {
      find_invariant_boundaries();
    }
    return boundaries;
  }
};

#endif
