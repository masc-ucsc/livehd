//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <string>

#include "pass.hpp"
#include "upass_core.hpp"
#include "upass_runner.hpp"

class Pass_upass : public Pass {
protected:
  std::vector<std::string> upass_order;
  bool                     inherit_labels{true};
  bool                     verifier_include_funcs{false};
  bool                     run_ssa{false};
  bool                     run_tolg{false};
  bool                     run_toln{true};
  upass::Options_map       pass_options;

public:
  static void work(Eprp_var& var);

  Pass_upass(const Eprp_var& var);

  static void setup();
};
