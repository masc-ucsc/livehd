//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pass.hpp"

class Lnast;

// Lower each lgraph in var.graphs to one ordinary front-end LNAST pushed on
// var.lnasts (see README.md). Prototype: combinational cells + default flops.
class Pass_lg2lnast : public Pass {
public:
  static void work(Eprp_var& var);
  // The same lowering without an Eprp_var: one LNAST per graph that is inside the supported subset.
  // A graph outside it is reported in `skipped` (name, reason) and, when `warn`, as a `graph-skipped`
  // diagnostic. A Sub instance cycle is resolved by splicing the stuck instances into the graph IN
  // MEMORY, so hand this a private copy of a library that must stay as it is. Returns false when
  // `passes` names an unknown stream pass. `sim_markers` (the `lhd sim` flow): keep each `lgundef`
  // marker as a call pass/lnast_llvm reports (sim.warn_undefined); tolg does not read that call.
  static bool lower(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view passes,
                    std::vector<std::shared_ptr<Lnast>>&              lnasts,
                    std::vector<std::pair<std::string, std::string>>* skipped = nullptr, bool warn = true,
                    bool sim_markers = false);
  explicit Pass_lg2lnast(const Eprp_var& var);
  static void setup();
};
