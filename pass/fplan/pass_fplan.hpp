#pragma once

#include "pass.hpp"

//#include "i_resolve_header.hpp"

// not sure if we're an inou or a pass.  Assuming a pass for now.

class Pass_fplan : public Pass {
public:
  // creates the parser and registers it with LGraph...?
  Pass_fplan(const Eprp_var &var) : Pass("pass.fplan", var) {}

  static void setup();

  static void pass(Eprp_var &v);

private:
  // TODO: if a Graph_info class is used here, things break beacause of "i_resolve_header" not being included last.
  //void makefp(Eprp_var &var, Graph_info &gi);
};