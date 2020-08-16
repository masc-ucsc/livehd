//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lnast.hpp"

class Pass_lnastfmt :public Pass {
public:
  Pass_lnastfmt(const Eprp_var& var);
  static void setup();
};
