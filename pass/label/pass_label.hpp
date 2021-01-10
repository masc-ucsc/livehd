// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_label : public Pass {
private:
protected:

  bool verbose;
  bool hier;

  static void label_synth(Eprp_var &var);
  static void label_mincut(Eprp_var &var);
  static void label_acyclic(Eprp_var &var);

public:
  Pass_label(const Eprp_var &var);

  static void setup();
};
