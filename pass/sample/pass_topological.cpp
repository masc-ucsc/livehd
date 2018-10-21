//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_topological.hpp"

Pass_topo::Pass_topo() {
}

void Pass_topo::trans(LGraph *orig) {
  int cells = 0;
  for(const auto& idx : orig->forward()) {
    (void)idx;
    cells++;
  }
  fmt::print("Pass: cells {}\n",cells);
}
