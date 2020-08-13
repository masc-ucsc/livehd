#pragma once

#include "pass.hpp"

// not sure if we're an inou or a pass.  Assuming a pass for now.

class Pass_fplan : public Pass {
public:
  // creates the parser and registers it with LGraph...?
  Pass_fplan(const Eprp_var &var) : Pass("pass.fplan", var) {}

  static void setup();

  static void pass(Eprp_var &v);

private:
  void makefp(LGraph *l);
};