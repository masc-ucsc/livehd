// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Inou_cgen : public Pass {
private:
  bool        verbose;
  bool        srcmap = false;  // ECMA-426 .map sidecar
  std::string odir;

protected:
  static void to_cgen_verilog(Eprp_var& var);
  static void to_cgen_sim(Eprp_var& var);  // inou.cgen.sim: executable Slop C++ (TODO 3d)

public:
  Inou_cgen(const Eprp_var& var);

  static void setup();
};
