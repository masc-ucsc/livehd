//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Inou_slang : public Pass {
public:
  static void work(Eprp_var& var);

  Inou_slang(const Eprp_var& var);

  static void setup();
};
