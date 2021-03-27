// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Inou_cgen : public Pass {
private:
  bool        verbose;
  std::string odir;

protected:
  static void to_cgen_verilog(Eprp_var &var);

public:
  Inou_cgen(const Eprp_var &var);

  static void setup();
};
