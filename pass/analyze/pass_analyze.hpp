// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

namespace livehd {

// `lhd pass analyze lg:DIR [--top m]` — read-only structural diagnosis over a
// WHOLE library: combinational loops (classified), clock endpoints, and the
// validity of Color_acyclic's partitioning. See pass/analyze/analyze.hpp for why
// this is not a call into the passes that ask the same questions.
class Pass_analyze : public Pass {
public:
  explicit Pass_analyze(const Eprp_var& var);
  static void setup();
  static void analyze(Eprp_var& var);
};

}  // namespace livehd
