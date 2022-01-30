// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_acyclic.hpp"

#include "annotate.hpp"
#include "cell.hpp"
#include "pass.hpp"

Label_acyclic::Label_acyclic(bool _verbose, bool _hier, uint8_t _cutoff) : verbose(_verbose), hier(_hier), cutoff(_cutoff) {}


void Label_acyclic::dump() const {
  fmt::print("Label_acyclic dump\n");
}


void Label_acyclic::label(Lgraph *g) {
  if (cutoff) fmt::print("small partition cutoff: {}\n", cutoff);
  
  g->each_graph_input([&](const Node_pin &pin) {
    //(void)pin;  // to avoid warning
    for (const auto &e : pin.out_edges()) {
      auto sink_node = e.sink.get_node();
      auto driver_node = e.driver.get_node();
      fmt::print("sink_node: {}, driver_node: {}\n", sink_node.debug_name(), driver_node.debug_name());
    }
  });

  g->each_graph_output([&](const Node_pin &pin) {
    (void)pin;  // to avoid warning
  });

  for (auto node : g->fast(hier)) {
    (void)node;  // to avoid warning
    // FIXME: do pass here
  }

  if (verbose) {
    dump();
  } 
}
