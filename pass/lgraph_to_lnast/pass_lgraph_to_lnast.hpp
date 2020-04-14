//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"
#include "lnast.hpp"
#include "lgraph.hpp"

class Pass_lgraph_to_lnast : public Pass {
protected:
  static void trans(Eprp_var &var);
  void        do_trans(LGraph *g);

public:
  Pass_lgraph_to_lnast(const Eprp_var &var);

  static void setup();
};
