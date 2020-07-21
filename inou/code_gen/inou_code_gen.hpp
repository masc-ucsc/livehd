//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lgraph.hpp"
#include "pass.hpp"

class Inou_code_gen : public Pass {
//protected:
//  enum class Cgen_type { Type_verilog, Type_prp, Type_cfg, Type_cpp };

private:
  enum class Cgen_type { Type_verilog, Type_prp, Type_cfg, Type_cpp };

  LGraph *lg;

  void to_xxx(Cgen_type cgen_type, std::shared_ptr<Lnast> lnast);

  // callback entry points
  static void to_verilog(Eprp_var &var);
  static void to_prp(Eprp_var &var);
  static void to_cfg(Eprp_var &var);
  static void to_cpp(Eprp_var &var);
public:
  Inou_code_gen(const Eprp_var &var);

  static void setup();
};
