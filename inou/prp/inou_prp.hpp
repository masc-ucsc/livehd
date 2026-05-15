//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Inou_prp : public Pass {
protected:
  // eprp callbacks
  static void parse_to_lnast(Eprp_var& var);

public:
  Inou_prp(const Eprp_var& var);

  static void setup();
};
