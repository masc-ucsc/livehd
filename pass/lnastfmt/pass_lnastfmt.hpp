//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lnast.hpp"
#include "pass.hpp"

class Pass_lnastfmt : public Pass {
protected:
  void validate(const Lnast* ln);

public:
  static void fmt_begin(Eprp_var& var);
  Pass_lnastfmt(const Eprp_var& var);
  static void setup();
};
