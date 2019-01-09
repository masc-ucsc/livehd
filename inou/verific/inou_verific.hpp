//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "pass.hpp"

class Inou_verific : public Pass {
private:
protected:

  static void tolg(Eprp_var &var);

  bool verific_parse(LGraph *lg, std::string_view filename);

public:
  Inou_verific();

  void setup() final;
};

