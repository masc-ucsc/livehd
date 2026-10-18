//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

// pass.aegraph: the e-graph alternative to pass.cprop. The kernel selects it
// with `--set compile.cprop.engine=aegraph|aegraph+classic` and swaps the
// method in the compile schedule; there is no user-facing way to run it alone.
class Pass_aegraph : public Pass {
protected:
  static void optimize(Eprp_var& var);

public:
  Pass_aegraph(const Eprp_var& var);
  static void setup();
};
