// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

// pass.lec — relational equivalence (L3 entry). Proves the two designs in
// var.graphs equal (graph[0] = ref, graph[1] = impl) under assume_equal of
// matched primary inputs. Engine/solver knobs ride --set lec.* (the
// add_label_optional registry below IS the lec.* option set; `lhd lec` feeds
// them in via merge_sets, and --set lec.solver=lgyosys selects the yosys/lgcheck
// backend instead of this in-process engine).
class Pass_lec : public Pass {
public:
  explicit Pass_lec(const Eprp_var& var);

  static void setup();
  static void lec(Eprp_var& var);
};
