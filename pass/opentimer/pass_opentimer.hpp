//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"
#include "ot/timer/timer.hpp"

class Pass_opentimer : public Pass {
protected:
  ot::Timer timer;

  std::string_view lib;
  std::string_view lib_max;
  std::string_view lib_min;
  std::string_view spef;

  static void work(Eprp_var &var);

  void read_file(LGraph *g,std::string_view lib,std::string_view lib_max,std::string_view lib_min,std::string_view spef);
  void build_circuit(LGraph *g);
  void read_sdc(LGraph *g);
  void compute_timing();
  void populate_table();

public:
  Pass_opentimer();

  void setup() final;
};
