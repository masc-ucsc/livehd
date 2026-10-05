// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "xag.hpp"

namespace livehd::usyn {
struct Npn_candidates {
  Status               status = Status::invalid;
  std::vector<Xsignal> signals;
};

// Complete four-input NPN class coverage, with two bounded native templates per
// class (area/depth heuristics). Input i is bit i of the truth-table index. This
// is not an optimal synthesis claim. Generated data and exhaustive transforms
// use no ABC/solver. Existing nodes remain immutable; refused trial nodes are
// append-only scratch, not accepted replacements. Both variants share a budget.
Npn_candidates npn4_candidates(Xag& graph, uint16_t truth, const std::array<Xsignal, 4>& inputs, Budget& work,
                               uint32_t max_nodes = 2000000);
}  // namespace livehd::usyn
