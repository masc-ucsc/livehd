#ifndef INVARIANT_FINDER_H_
#define INVARIANT_FINDER_H_

#include "lgraph.hpp"
#include "invariant.hpp"

#include "bitscan/bitscan.h"

class Invariant_finder {

private:
  LGraph*              elab_graph;
  LGraph*              synth_graph;
  Invariant_boundaries boundaries;
  std::string          hier_separator;

  bool                 processed;
  bitarray             stack;

  typedef std::pair<Index_ID, uint32_t> Node_bit;
  std::map<Node_bit, Gate_set>          partial_cone_cells;  // partial_gate_count
  std::map<Node_bit, Net_set>           partial_endpoints;   //sips

  void get_topology();

  void find_invariant_boundaries();

  void propagate_until_boundary(Index_ID nid, uint32_t bit_selection);

public:
  Invariant_finder(LGraph* elab, LGraph* synth, std::string hier_sep = ".") :
    hier_separator(hier_sep), stack(synth->max_size()) {
    processed   = false;
    elab_graph  = elab;
    synth_graph = synth;
  }


  Invariant_boundaries get_boundaries() {
    if(!processed) {
      find_invariant_boundaries();
    }
    return boundaries;
  }
  ~Invariant_finder() {
    fmt::print("IF destructor\n");

  }
};

#endif

