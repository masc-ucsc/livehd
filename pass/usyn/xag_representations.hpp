// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "xag.hpp"

namespace livehd::usyn {
struct Representation_candidate {
  Xsignal     signal;
  std::string representation;
};
struct Representation_result {
  Status                                status = Status::invalid;
  std::vector<Representation_candidate> candidates;
  uint64_t                              bdd_nodes = 0, sop_cubes = 0, dsd_blocks = 0;
  uint64_t                              rejected = 0;
  bool                                  limited  = false;
};
// Total-function alternatives on the identical ordered independent basis:
// bounded disjoint-support decomposition, signed SOPs of f and ~f, and a
// reduced ordered BDD with a fixed per-cone order. DSD tests at most 64 partitions
// for two distinct complete cofactors, with BDDs at prime leaves; it is not a
// canonical or exhaustive decomposition algorithm.
// <=8 inputs, <=64 SOP literals, <=128 BDD nodes. No wide SOP expansion or care
// assumptions. Every completed candidate is exhaustively checked before return.
Representation_result represent_function(Xag& graph, const Truth_table& function, std::span<const Xsignal> inputs, Budget& work,
                                         uint32_t max_nodes = 2000000);
}  // namespace livehd::usyn
