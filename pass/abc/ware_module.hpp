// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::abc {
struct Ware_policy {
  bool arith = true;
  bool cmp   = true;
  bool shift = true;
};
// Persisted pass.color options, independent of the stop_* boundary controls.
Ware_policy                               ware_policy(const hhds::Graph& graph, Ware_policy fallback = {});
// Specialize arithmetic definitions by all operand/result widths and signs.
// Constants and source section options belong to the specialization as well.
std::vector<std::shared_ptr<hhds::Graph>> build_ware_modules(const std::vector<std::shared_ptr<hhds::Graph>>& graphs,
                                                             Ware_policy                                      fallback = {});
}  // namespace livehd::abc
