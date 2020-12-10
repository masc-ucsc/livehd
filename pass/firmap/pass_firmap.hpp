//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <deque>
#include <vector>

#include "node.hpp"
#include "node_pin.hpp"
#include "pass.hpp"




class Pass_firmap : public Pass {
protected:
  bool hier;
  static void trans(Eprp_var &var);

public:
  explicit Pass_firmap(const Eprp_var &var);
  static void setup();
};
