// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "pass.hpp"

class Pass_usyn : public Pass {
public:
  explicit Pass_usyn(const Eprp_var& var) : Pass("pass.usyn", var) {}
  static void setup();
  // Synthesizes the design. An allocation failure anywhere below
  // (std::bad_alloc, e.g. at the RLIMIT_AS memory backstop or the shared job
  // manager's lease) fails with a `memory-exhausted` TIMEOUT-memory diagnostic.
  static void work(Eprp_var& var);

private:
  static void synthesize(Eprp_var& var);
};
