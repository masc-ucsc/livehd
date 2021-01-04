#pragma once

#include <string_view>

#include "lg_flat_floorp.hpp"
#include "lg_hier_floorp.hpp"
#include "lgraph.hpp"
#include "pass.hpp"
#include "iassert.hpp"

class Pass_fplan_makefp : public Pass {
public:
  Pass_fplan_makefp(const Eprp_var& var);

  static void setup();

  static void pass(Eprp_var& v);

private:
  LGraph* root_lg;
};