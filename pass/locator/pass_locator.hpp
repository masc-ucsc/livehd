//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

#include "pass.hpp"

class Pass_locator : public Pass {
protected:
  std::string top;

  void parse_ln(const std::shared_ptr<Lnast>& ln, Eprp_var& var);

public:
  static void begin_pass(Eprp_var& var);
  Pass_locator(const Eprp_var& var);
  static void setup();
};
