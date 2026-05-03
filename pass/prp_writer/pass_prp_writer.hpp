//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_prp_writer : public Pass {
public:
  static void work(Eprp_var& var);
  Pass_prp_writer(const Eprp_var& var);
  static void setup();
};
