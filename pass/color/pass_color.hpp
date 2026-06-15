// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

// pass.color — hierarchical node coloring (the revived pass/label, task
// 2c-color). One method takes the algorithm as the `alg` label
// (acyclic|synth|path|mincut|clear); every other knob rides --set pass.color.*.
class Pass_color : public Pass {
public:
  explicit Pass_color(const Eprp_var& var);

  static void setup();
  static void color(Eprp_var& var);
};
