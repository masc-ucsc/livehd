//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Inou_pyrope : public Pass {
private:
  static inline int trace_module_cnt = 0;

protected:
  void to_lgraph(std::string_view file);

  void do_work(const Lgraph *g);

  // eprp callbacks
  static void parse_to_lnast(Eprp_var &var);

public:
  Inou_pyrope(const Eprp_var &var);

  static void setup();
};
