//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Inou_liberty : public Pass {
protected:
  static void liberty_open(Eprp_var &var);

public:
  Inou_liberty(const Eprp_var &var);

  static void setup();
};
