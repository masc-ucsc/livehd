// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_synth.hpp"
#include "pass.hpp"
#include "cell.hpp"

Label_synth::Label_synth(bool _verbose, bool _hier) : verbose(_verbose), hier(_hier) {
  (void)verbose;
}

void Label_synth::label(LGraph *g) {

  g->each_graph_input([&](const Node_pin &pin) {
    (void)pin; // to avoid warning
  });

  g->each_graph_output([&](const Node_pin &pin) {
    (void)pin; // to avoid warning
  });

  for(auto node:g->fast(hier)) {
    (void)node; // to avoid warning
    // FIXME: do pass here
  }
}

