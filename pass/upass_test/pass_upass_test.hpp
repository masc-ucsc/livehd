//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"
#include "upass_runner.hpp"

class Pass_upass_test : public Pass {
protected:
public:
  static void work(Eprp_var &var);

  Pass_upass_test(const Eprp_var &var);

  static void setup();
};
