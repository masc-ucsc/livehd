//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "cfg_lnast.hpp"
#include "pass.hpp"

class Inou_cfg : public Pass {
protected:
  // eprp callbacks
  static void parse_to_lnast(Eprp_var &var);

public:
  Inou_cfg(const Eprp_var &var);

  static void setup();
};
