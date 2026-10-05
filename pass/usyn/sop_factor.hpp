// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "xag.hpp"

namespace livehd::usyn {
struct Sop_result {
  Status               status = Status::invalid;
  Xag                  graph;
  std::vector<Xsignal> outputs;
  uint64_t             roots = 0, cofactors = 0;
  bool                 limited = false;
};

// Experimental complete alternative for CMOS selection. Recognize bounded
// OR-of-products and apply symbolic Shannon cofactoring on repeated literals.
// Computed atoms remain independent symbolic leaves; the identity holds for
// every assignment to them, so wider data supports need no sampled care table.
// Keep the owner's incumbent immutable. This candidate can have higher native
// AND/XOR cost and must be judged by mapped evidence, never presumed cheaper.
Sop_result factor_sop(const Xag& source, std::span<const Xsignal> outputs, Budget& work, uint32_t max_nodes = 2000000);
}  // namespace livehd::usyn
