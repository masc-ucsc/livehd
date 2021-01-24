#pragma once

#include "lgraph.hpp"
#include "pass.hpp"

class Pass_fplan_writearea : public Pass {
public:
  Pass_fplan_writearea(const Eprp_var& var);
  static void setup();
  static void pass(Eprp_var& v);
};