#pragma once

#include "pass.hpp"
#include "lgraph.hpp"
#include "graph_info.hpp"

// not sure if we're an inou or a pass.  Assuming a pass for now.

class Livehd_parser : public Pass {
public:
  // creates the parser and registers it with LGraph...?
  Livehd_parser(const Eprp_var &var);

  static void setup();

  static void tofp(Eprp_var &v);
};