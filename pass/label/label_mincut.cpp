// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_mincut.hpp"
#include "pass.hpp"
#include "cell.hpp"

Label_mincut::Label_mincut(bool _verbose, bool _hier) : verbose(_verbose), hier(_hier) {
}

void Label_mincut::label(LGraph *g) {

  g->each_graph_input([&](const Node_pin &pin) {
  });

  g->each_graph_output([&](const Node_pin &pin) {
  });

  for(auto node:g->fast(hier)) {
    // FIXME: do pass here
  }
}

