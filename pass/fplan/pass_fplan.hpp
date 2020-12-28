#pragma once

#include <string_view>

#include "lg_flat_loader.hpp"
#include "lg_hier_loader.hpp"
#include "lgraph.hpp"
#include "pass.hpp"

class Pass_fplan : public Pass {
public:
  Pass_fplan(const Eprp_var& var);

  void analyze_floorplan(const std::string_view filename);

  static void setup();

  static void pass(Eprp_var& v);

private:
  LGraph* root_lg;
};
