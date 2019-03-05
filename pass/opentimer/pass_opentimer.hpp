//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lghierarchy.hpp"
#include "options.hpp"
#include "pass.hpp"

//class Pass_opentimer_options : public Options_base {
//public:
//  Pass_opentimer_options() {
//    verbose = false;
//  };
//
//  std::string liberty_file;
//  std::string spef_file;
//  std::string sdc_file;
//  bool verbose;
//
//  void set(const std::string &key, const std::string &value);
//};

class Pass_opentimer : public Pass {
private:
protected:
  LGraph_Hierarchy src_hierarchy;
  LGraph_Hierarchy dst_hierarchy;

  static void work(Eprp_var &var);

public:
  Pass_opentimer();
  Pass_opentimer(std::string_view src, std::string_view dst);

  void setup() final;

  void punch(LGraph *top, std::string_view src, std::string_view dst);
};
