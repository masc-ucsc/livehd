//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_topological.hpp"

void Pass_topo::transform(LGraph *orig) {
  int cells = 0;
  for(const auto& idx : orig->forward()) {
    (void)idx;
    cells++;
  }
  fmt::print("cells {}\n",cells);
}
