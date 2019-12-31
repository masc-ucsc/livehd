//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "pass.hpp"

class Inou_liveparse : public Pass {
protected:
  void do_tolg();

  // eprp callbacks
  static void tolg(Eprp_var &var);

public:
  Inou_liveparse(const Eprp_var &var);

  static void setup();
};
