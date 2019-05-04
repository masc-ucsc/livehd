//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lghierarchy.hpp"
#include "options.hpp"
#include "pass.hpp"
#include "ot/timer/timer.hpp"

class Pass_opentimer : public Pass {
protected:
  ot::Timer timer;

  static void work(Eprp_var &var);

  void read_file(LGraph *g);
  void build_circuit(LGraph *g);
  void compute_timing();
  void populate_table();

public:
  Pass_opentimer();

  void setup() final;
};
