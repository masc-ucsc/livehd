//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lnast_create.hpp"
#include "pass.hpp"

class Pass_lnastopt : public Pass {
protected:
public:
  static void work(Eprp_var &var);

  Pass_lnastopt(const Eprp_var &var);

  static void setup();
};
