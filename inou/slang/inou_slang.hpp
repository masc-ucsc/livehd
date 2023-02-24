//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <atomic>

#include "pass.hpp"

class Inou_slang : public Pass {
private:
  static inline std::atomic<int> trace_module_cnt = 0;

protected:
  void check_lec(Lgraph *g);

  void do_work(Lgraph *g);

public:
  static void work(Eprp_var &var);

  Inou_slang(const Eprp_var &var);

  static void setup();
};
