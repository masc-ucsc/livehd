//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <string_view>

#include "lgraph.hpp"
#include "pass.hpp"
#include "floorplanner.hpp"

class Pass_fplan_makefp : public Pass {
public:
  Pass_fplan_makefp(const Eprp_var& var);
  static void setup();
  static void pass(Eprp_var& v);

private:
  LGraph* root_lg;

  void makefp_int(Lhd_floorplanner& fp, const std::string_view dest, const float ar);
};

