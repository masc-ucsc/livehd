//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_punch : public Pass {
protected:
  std::string_view src;
  std::string_view dst;

  static void work(Eprp_var &var);

public:
  Pass_punch();
  Pass_punch(std::string_view src, std::string_view dst);

  void setup() final;

  void punch(LGraph *top, std::string_view src, std::string_view dst);
};

