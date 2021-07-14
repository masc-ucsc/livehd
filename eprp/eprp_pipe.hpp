//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "eprp_method.hpp"
#include "eprp_var.hpp"

struct Pipe_step {
  Pipe_step(const Eprp_method &fun, const Eprp_var &var) : m(fun), var_fields(var), next_step(nullptr) {}

  void run(Eprp_var &last_cmd_var);

  Eprp_method m;
  Eprp_var    var_fields;
  Pipe_step  *next_step;
};

class Eprp_pipe {
protected:

  std::vector<Pipe_step> steps;
public:

  void clear() {
    steps.clear();
  }

  void add_command(const Eprp_method &m, const Eprp_var &field_var);

  void run();
};

