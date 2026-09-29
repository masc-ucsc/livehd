// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>

#include "pass.hpp"

namespace livehd::abc {
struct Map_options;
}

// pass.abc — technology-map each colored region to a standard-cell netlist.
// Reuses pass.partition's decomposition seam: one module per
// color region, each body replaced by an ABC-mapped netlist of 1-bit blackbox
// Sub cells named after the Liberty cells. The module structure mirrors
// pass.partition exactly, so each netlist module LEC-checks against its twin.
// A prior pass.color is optional — an uncolored design maps as a single
// color-0 region (with a one-line warning), so `pass abc` runs standalone.
class Pass_abc : public Pass {
public:
  explicit Pass_abc(const Eprp_var& var);

  static void setup();
  static void work(Eprp_var& var);
  static void add_mapping_labels(Eprp_method& method);
  static void work_with(Eprp_var& var, const std::function<void(livehd::abc::Map_options&)>& configure);
};
