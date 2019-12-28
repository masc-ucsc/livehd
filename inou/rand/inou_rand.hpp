//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "pass.hpp"

class Inou_rand : public Pass {
private:
  int       rand_seed;
  int       rand_size;
  int       rand_crate;
  double    rand_eratio;
protected:
  LGraph   *do_tolg();

  // eprp callback
  static void tolg(Eprp_var &var);
public:
  Inou_rand(const Eprp_var &var);

  static void setup();
};

