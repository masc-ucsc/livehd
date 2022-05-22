//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "ot/timer/timer.hpp"
#include "pass.hpp"

class Pass_opentimer : public Pass {
protected:
  ot::Timer timer;

  static void liberty_open(Eprp_var &var);
  static void work(Eprp_var &var);

  void read_files();
  void build_circuit(Lgraph *g);
  void read_sdc(std::string_view sdc_file);
  void compute_timing();
  void populate_table();

public:
  Pass_opentimer(const Eprp_var &var);

  static void setup();
};
