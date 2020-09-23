// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>

#include "lgraph.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"
#include "pass.hpp"
#include "graphviz.hpp"


class Inou_graphviz : public Pass {
private:
  bool bits;
  bool verbose;
protected:

  // eprp callback methods
  static void from(Eprp_var &var);
  static void hierarchy(Eprp_var &var);

public:
  Inou_graphviz(const Eprp_var &var);

  static void setup();
};
