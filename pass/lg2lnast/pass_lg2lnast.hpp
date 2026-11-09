//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

// Lower each lgraph in var.graphs to one ordinary front-end LNAST pushed on
// var.lnasts (see README.md). Prototype: combinational cells + default flops.
class Pass_lg2lnast : public Pass {
public:
  static void work(Eprp_var& var);
  explicit Pass_lg2lnast(const Eprp_var& var);
  static void setup();
};
