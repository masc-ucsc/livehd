//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "eprp_method.hpp"
#include "eprp_var.hpp"

class Eprp_pipe {
protected:
  struct Step {
    Step(const Eprp_method &fun, const Eprp_var &var) : m(fun), var_fields(var) {}

    Eprp_method m;
    Eprp_var    var_fields;
  };

  std::vector<Step> steps;
public:

  void clear() {
    steps.clear();
  }

  void add_command(const Eprp_method &m, const Eprp_var &field_var);

  void run();
};

