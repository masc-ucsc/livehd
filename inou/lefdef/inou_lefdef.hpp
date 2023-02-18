// See LICENSE for details

#pragma once

#include "pass.hpp"

class Inou_lefdef : public Pass {
protected:
  std::string lgdb;

  void do_tolg(Eprp_var &var);

  // eprp callbacks
  static void to_lg(Eprp_var &var);

  std::vector<Lgraph *> generate();
  void                  generate(std::vector<const Lgraph *> &out);

  std::vector<Lgraph *> parse_def(std::string_view def_file);

public:
  Inou_lefdef(const Eprp_var &var);

  static void setup();
};

