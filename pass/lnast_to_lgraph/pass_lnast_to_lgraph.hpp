//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lnast_to_lgraph.hpp"
#include "pass.hpp"

class Pass_lnast_to_lgraph : public Pass {
public:
  static void setup();
  static void work(Eprp_var& var);
  Pass_lnast_to_lgraph(const Eprp_var& var);
};
