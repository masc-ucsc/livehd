//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_cprop : public Pass {
private:
  bool hier;
protected:
  static void optimize(Eprp_var &var);

public:
  Pass_cprop(const Eprp_var &var);
  static void setup();
};

