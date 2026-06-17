// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

// pass.semdiff — structural diff/match (task 2f-semdiff). Stamps the `match`
// attribute on the two designs in var.graphs (graph[0] = ref, graph[1] = impl)
// so corresponding nodes share an id and the diff is greppable. Mirrors
// pass.lec's shape; the `lhd semdiff` command calls
// semdiff::structural_match directly (as lec_command calls lec::prove_equal).
// Knobs ride --set semdiff.* (matching_names | id_granularity | alg).
class Pass_semdiff : public Pass {
public:
  explicit Pass_semdiff(const Eprp_var& var);

  static void setup();
  static void semdiff(Eprp_var& var);
};
