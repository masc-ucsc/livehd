//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "mustache.hpp"
using namespace kainjow;

#include "eprp.hpp"

class Inou_yosys_api {
protected:
  static void set_script_liblg(Eprp_var &var, std::string &script_file, std::string &liblg, bool do_read);
  static int  create_lib(std::string_view lib_file, std::string_view lgdb);
  static int  do_work(std::string_view yosys, std::string_view liblg, std::string_view script_file, mustache::data &vars);
  static void tolg(Eprp_var &var);
  static void fromlg(Eprp_var &var);

public:
  static void setup(Eprp &eprp);
};
