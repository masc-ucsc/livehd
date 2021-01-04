//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <deque>
#include <vector>

#include "bitwidth_range.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "pass.hpp"

using BWMap_flat = absl::flat_hash_map<Node_pin::Compact_flat, Bitwidth_range>;
using BWMap_hier = absl::flat_hash_map<Node_pin::Compact,      Bitwidth_range>;

class Pass_bitwidth : public Pass {
protected:
  int  max_iterations;
  bool must_perform_backward;
  bool hier;
  static void trans(Eprp_var &var);
  BWMap_flat flat_bwmap;
  BWMap_hier hier_bwmap;

public:
  explicit Pass_bitwidth(const Eprp_var &var);
  static void setup();
};
