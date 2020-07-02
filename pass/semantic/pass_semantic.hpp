//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_semantic : public Pass {
protected:
  void do_work(LGraph *g);
  void do_work(std::shared_ptr<Lnast> lnast);

public:
  static void setup();
  static void work(Eprp_var &var);

  Pass_semantic(const Eprp_var &var);
};
