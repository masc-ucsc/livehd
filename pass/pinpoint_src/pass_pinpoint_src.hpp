//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"
#include <string_view>

class Pass_pinpoint_src : public Pass {
protected:
  std::string  top;

  void parse_ln(const std::shared_ptr<Lnast>& ln, Eprp_var& var);
public:
  static void begin_pass(Eprp_var &var);
  Pass_pinpoint_src(const Eprp_var &var);
  static void setup();
};
