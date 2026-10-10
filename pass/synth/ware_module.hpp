// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>
#include <set>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::synth {
struct Ware_policy {
  bool arith = true;
  bool cmp   = true;
  bool shift = true;
};
// Persisted pass.color options, independent of the stop_* boundary controls.
Ware_policy                               ware_policy(const hhds::Graph& graph, Ware_policy fallback = {});
// Specialize arithmetic definitions by all operand/result widths and signs.
// Constants and source section options belong to the specialization as well.
// The section COLOR is part of the identity only for a color with its own
// options (an embedded coloring_info region_opts entry, or one listed in
// `option_colors` -- the CLI region_opts keys): color numbers shift when an
// edit adds or removes a region, and naming every width specialization after
// one would rename the callee of every unrelated instance.
std::vector<std::shared_ptr<hhds::Graph>> build_ware_modules(const std::vector<std::shared_ptr<hhds::Graph>>& graphs,
                                                             Ware_policy                                      fallback      = {},
                                                             const std::set<int>&                             option_colors = {});
}  // namespace livehd::synth
