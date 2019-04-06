//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_mockturtle : public Pass {
protected:
  static void work(Eprp_var &var);

public:
  Pass_mockturtle();

  void setup() final;

  void do_work(LGraph *g);
};

