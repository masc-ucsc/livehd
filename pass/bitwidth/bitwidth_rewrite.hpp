// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "cprop.hpp"

// Post-convergence rewrites over semantic ranges, invoked once per bitwidth
// pass. Structural helpers are shared with cprop; inference is never queried
// by walking a producer cone.
class Bitwidth_rewrite : private Cprop {
  bool scalar_concat_range(hhds::Node_class& node);
  bool scalar_get_mask_range(hhds::Node_class& node);

public:
  void run(hhds::Graph& graph, const std::function<int(const hhds::Pin_class&)>& query,
           const std::function<void(const hhds::Pin_class&)>&      erase,
           const std::function<void(const hhds::Pin_class&, int)>& publish);
};
