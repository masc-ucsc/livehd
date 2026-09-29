//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_enableopt : public Pass {
protected:
  static void optimize(Eprp_var& var);

public:
  Pass_enableopt(const Eprp_var& var);
  static void setup();
};
