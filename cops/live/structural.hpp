//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef LIVE_STRUCTURAL_H_
#define LIVE_STRUCTURAL_H_

#include <set>

#include "invariant.hpp"
#include "stitch_options.hpp"

class queue_element {
public:
  Index_id id;
  uint32_t priority;
  queue_element(Index_id id, uint32_t pri) : id(id), priority(pri) {}

  bool operator<(const queue_element &b) { return priority < b.priority; }
};

struct Compare {
  bool operator()(queue_element lhs, queue_element rhs) { return lhs.priority > rhs.priority; }
};

class Live_structural {
private:
  const Lgraph *              original;
  const Invariant_boundaries *boundaries;

  Index_id get_candidate(Index_id newid, Lgraph *nsynth) {
    if (nsynth->get_wid(newid) == 0)
      return 0;
    auto name = nsynth->get_node_wirename(newid);
    if (!original->has_wirename(name))
      return 0;

    return original->get_node_id(name);
  }

  Node_pin get_inp_edge(Lgraph *current, Index_id nid, Port_ID pid);

public:
  Live_structural(Lgraph *original, Invariant_boundaries *boundaries)
      :  // original(original), boundaries(boundaries) {
      original(original) {
    I(boundaries);
  }

  Live_structural(Stitch_pass_options &pack);

  // void replace(Lgraph* nsynth, std::set<Net_ID>& diffs);
  void replace(Lgraph *nsynth);

  void replace(const std::string &nsynth) {
    Lgraph *synth = Lgraph::open(nsynth, boundaries->top);
    replace(synth);
  }
};

#endif
