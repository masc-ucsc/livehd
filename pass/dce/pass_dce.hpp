//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef PASS_DCE_H
#define PASS_DCE_H

#include <string>

#include "pass.hpp"

class Pass_dce : public Pass {
private:
protected:
  static void optimize(Eprp_var &var);

  void trans(LGraph *orig);

public:
  Pass_dce(const Eprp_var &var);

  static void setup();
};

#endif
