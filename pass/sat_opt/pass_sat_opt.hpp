//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_sat_opt : public Pass {
protected:
  void check_sat_opt(LGraph *g);

  void do_work(LGraph *g);

public:
  static void work(Eprp_var &var);

  Pass_sat_opt(const Eprp_var &var);

  static void setup();
};
