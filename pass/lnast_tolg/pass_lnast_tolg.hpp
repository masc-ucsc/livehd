// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "lnast_tolg.hpp"

class Pass_lnast_tolg : public Pass {
protected:
  // eprp callbacks
  static void tolg(Eprp_var &var);
  static void dbg_lnast_ssa(Eprp_var &var);

public:
  explicit Pass_lnast_tolg(const Eprp_var &var);
  static void setup();
};
