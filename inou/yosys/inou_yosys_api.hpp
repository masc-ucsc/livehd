//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// External package includes
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

#include "mustache.hpp"
using namespace kainjow;

#pragma GCC diagnostic pop

#include "eprp.hpp"
#include "lgraph.hpp"
#include "pass.hpp"

class Inou_yosys_api : public Pass {
protected:
  std::string script_file;

  void set_script_yosys(const Eprp_var &var, bool do_read);

  void do_tolg(Eprp_var &var);

  void call_yosys(mustache::data &vars);

  // eprp callback
  static void tolg(Eprp_var &var);
  static void fromlg(Eprp_var &var);

public:
  Inou_yosys_api(Eprp_var &var, bool do_read);

  static void setup();
};
