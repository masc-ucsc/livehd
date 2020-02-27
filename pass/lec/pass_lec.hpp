//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_lec : public Pass {
protected:
  void check_lec(LGraph *g);

  void do_work(LGraph *g);

public:
  static void work(Eprp_var &var);

  Pass_lec(const Eprp_var &var);

  static void setup();
};
