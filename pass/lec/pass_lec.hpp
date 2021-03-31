//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "boolector.h"
#include "pass.hpp"

class Pass_lec : public Pass {
protected:
  std::multimap<std::string, int> graphIOs;

  std::vector<std::string> graphInNames;
  // std::vector < std::vector < std::string > > allInNames;

  // std::vector <std::string > graph1, graph2;
  // std::multimap <std::string, std::multimap <std::string, int> > graphsToCheck;

  void check_lec(Lgraph *g);

  void do_work(Lgraph *g);

public:
  static void work(Eprp_var &var);

  Pass_lec(const Eprp_var &var);

  static void setup();
};
