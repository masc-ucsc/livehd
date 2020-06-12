//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lconst.hpp"
#include "node.hpp"
#include "pass.hpp"

class Pass_cprop : public Pass {
private:
protected:
  static void optimize(Eprp_var &var);

  void trans(LGraph *orig);
  void replace_node(Node &old_node, const Lconst &result);
  void try_collapse_forward(Node &node);

public:
  Pass_cprop(const Eprp_var &var);

  static void setup();
};

