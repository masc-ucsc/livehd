// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

#include "hhds/graph.hpp"

namespace livehd::graph_util {
// All-build consumer precondition. Constants carry their type in the literal;
// every other materialized value needs a positive width. Unsigned is the
// absence of pin_signed, so no separate "sign present" attribute is required.
// Includes the reachable definition closure, including instance output pins.
// Emits an internal error and throws before the consumer can use a bad graph.
void require_sized_design(hhds::Graph& graph, std::string_view consumer);
}  // namespace livehd::graph_util
