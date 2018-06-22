//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef LIVE_STRUCTURAL_H_
#define LIVE_STRUCTURAL_H_

#include "invariant.hpp"
#include <set>

class queue_element {
public:
  Index_ID id;
  uint32_t priority;
  queue_element(Index_ID id, uint32_t pri) : id(id), priority(pri) {}

  bool operator<(const queue_element &b) {
    return priority < b.priority;
  }
};

struct Compare {
  bool operator()(queue_element lhs, queue_element rhs) {
    return lhs.priority > rhs.priority;
  }
};

class Live_structural {

private:
  LGraph *original;
  //Invariant_boundaries* boundaries;

  Index_ID get_candidate(Index_ID newid, LGraph *nsynth) {
    if(nsynth->get_wid(newid) == 0)
      return 0;
    std::string name = nsynth->get_node_wirename(newid);
    if(!original->has_name(name))
      return 0;

    return original->get_node_id(name);
  }

  Node_Pin get_inp_edge(LGraph *current, Index_ID nid, Port_ID pid);

public:
  Live_structural(LGraph *original, Invariant_boundaries *boundaries) : //original(original), boundaries(boundaries) {
                                                                        original(original) {
    assert(boundaries);
  }

  //void replace(LGraph* nsynth, std::set<Net_ID>& diffs);
  void replace(LGraph *nsynth);
};

#endif
