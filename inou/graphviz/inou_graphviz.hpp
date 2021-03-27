// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "graphviz.hpp"
#include "pass.hpp"

class Inou_graphviz : public Pass {
private:
  bool bits;
  bool verbose;

protected:
  static void from(Eprp_var &var);
  static void hierarchy(Eprp_var &var);

public:
  Inou_graphviz(const Eprp_var &var);

  static void setup();
};
