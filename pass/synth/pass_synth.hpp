// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "pass.hpp"

class Pass_synth : public Pass {
public:
  explicit Pass_synth(const Eprp_var& var) : Pass("pass.synth", var) {}
  static void setup();
  static void work(Eprp_var& var);
};
