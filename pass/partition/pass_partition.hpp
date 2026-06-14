// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

// pass.partition — build a new graph_library from the colors produced by
// pass.color (task 2p-partition). One module per colored region, instantiated
// once per region in a fresh top that is LEC-equivalent to the original.
class Pass_partition : public Pass {
public:
  explicit Pass_partition(const Eprp_var& var);

  static void setup();
  static void partition(Eprp_var& var);
};
