//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lgraph.hpp"
#include "pass.hpp"

#include "mustache.hpp"
using namespace kainjow;

#include "eprp.hpp"

class Inou_yosys_api : public Pass {
protected:
  std::string yosys; // yosys configuration script option
  std::string script_file;
  std::string liblg;

  void set_script_liblg(const Eprp_var &var, bool do_read);

  int create_lib(const std::string &lib_file, const std::string &lgdb);
  void do_tolg(Eprp_var &var);

  int call_yosys(mustache::data &vars);

  // eprp callback
  static void tolg(Eprp_var &var);
  static void fromlg(Eprp_var &var);
public:
  Inou_yosys_api(Eprp_var &var, bool do_read);

  static void setup(Eprp &eprp);
};
