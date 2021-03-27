//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_gioc : public Pass {
private:
protected:
  static void connect(Eprp_var &var);

public:
  Pass_gioc(const Eprp_var &var);
  static void setup();
};
