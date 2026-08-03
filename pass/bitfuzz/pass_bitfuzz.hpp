//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "bitfuzz.hpp"
#include "pass.hpp"

class Pass_bitfuzz : public Pass {
protected:
  livehd::bitfuzz::Options opts;

public:
  explicit Pass_bitfuzz(const Eprp_var& var);

  static void setup();
  static void work(Eprp_var& var);
};
