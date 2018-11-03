//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef PASS_DCE_H
#define PASS_DCE_H

#include "options.hpp"
#include "pass.hpp"

#include <string>

class Pass_dce : public Pass {
private:
protected:
  std::string dce_type;

public:
  Pass_dce();

  void trans(LGraph *orig) final;
};

#endif

