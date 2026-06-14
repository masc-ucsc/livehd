// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

// pass.abc — technology-map each colored region to a standard-cell netlist
// (task 2a-abc). Reuses pass.partition's decomposition seam: one module per
// color region, each body replaced by an ABC-mapped netlist of 1-bit blackbox
// Sub cells named after the Liberty cells. The module structure mirrors
// pass.partition exactly, so each netlist module LEC-checks against its twin.
class Pass_abc : public Pass {
public:
  explicit Pass_abc(const Eprp_var& var);

  static void setup();
  static void work(Eprp_var& var);
};
