//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Opt_lnast {
protected:

public:
  Opt_lnast(const Eprp_var &var);

  void opt(std::shared_ptr<Lnast> ln);
};
