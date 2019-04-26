//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lghierarchy.hpp"
#include "options.hpp"
#include "pass.hpp"

class Pass_opentimer : public Pass {
protected:

  static void work(Eprp_var &var);

  void list_cells(LGraph *g);             // To be removed later
  void file_reader();
  void circuit_builder();

public:
  Pass_opentimer();

  void setup() final;
};
