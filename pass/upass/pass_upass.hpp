//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <string>

#include "pass.hpp"
#include "upass_runner.hpp"

class Pass_upass : public Pass {
protected:
  std::vector<std::string> upass_order;
  std::size_t              max_iters{1};
  std::string              ir_mode{"lnast"};
  bool                     dry_run{false};
  bool                     inherit_labels{true};

public:
  static void work(Eprp_var &var);

  Pass_upass(const Eprp_var &var);

  static void setup();
};
