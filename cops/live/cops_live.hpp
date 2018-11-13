//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef COPS_LIVE_HPP_
#define COPS_LIVE_HPP_

#include "pass.hpp"

class Cops_live : public Pass {
  public:
  Cops_live() : Pass("cops") {
  }

  void setup() final;

  static void invariant_finder(Eprp_var &var);
  static void diff_finder(Eprp_var &var);
  static void netlist_merge(Eprp_var &var);
};

#endif
