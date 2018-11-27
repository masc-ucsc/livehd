
#pragma once

#include "pass.hpp"

class Inou_liveparse : public Pass {
protected:
  static void tolg(Eprp_var &var);

public:
  Inou_liveparse();

  void setup() final;
};
