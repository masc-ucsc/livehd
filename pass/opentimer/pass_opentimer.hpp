//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "ot/timer/timer.hpp"
#include "pass.hpp"

class Pass_opentimer : public Pass {
protected:
  ot::Timer timer;

  std::string opt_lib;
  std::string opt_lib_max;
  std::string opt_lib_min;
  std::string opt_sdc;
  std::string opt_spef;

  static void work(Eprp_var &var);

  void read_files();
  void build_circuit(LGraph *g);
  void read_sdc();
  void compute_timing();
  void populate_table();

public:
  Pass_opentimer(const Eprp_var &var);

  static void setup();
};
