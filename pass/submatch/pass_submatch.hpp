//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class pass_submatch : public Pass {
protected:
  void find_subs(LGraph *g);

  void do_work(LGraph *g);

public:
  static void work(Eprp_var &var);

  pass_submatch(const Eprp_var &var);

  static void setup();
};
