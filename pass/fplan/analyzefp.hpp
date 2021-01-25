//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lgraph.hpp"
#include "pass.hpp"

class Pass_fplan_analyzefp : public Pass {
public:
  Pass_fplan_analyzefp(const Eprp_var& var);
  static void setup();
  static void pass(Eprp_var& v);
};