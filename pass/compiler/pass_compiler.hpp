// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "lcompiler.hpp"
#include "pass.hpp"
#include "pass_lnast_tolg.hpp"

class Pass_compiler : public Pass {
protected:
  static void      compile(Eprp_var &var);
  bool             check_option_gviz(Eprp_var &var);
  std::string      check_option_top(Eprp_var &var);
  std::string_view check_option_firrtl(Eprp_var &var);
  static void      setup_firmap_library(Lgraph *lg);
  static void      pyrope_compilation(Eprp_var &var, Lcompiler &compiler);
  static void      firrtl_compilation(Eprp_var &var, Lcompiler &compiler);

public:
  explicit Pass_compiler(const Eprp_var &var);
  static void setup();
};
