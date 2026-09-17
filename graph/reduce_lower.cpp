// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "reduce_lower.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "node_util.hpp"

namespace livehd::graph_util {

std::shared_ptr<hhds::Graph> lower_counted_reductions_copy(const std::shared_ptr<hhds::Graph>& graph, hhds::GraphLibrary& scratch) {
  bool needed = false;
  for (auto node : graph->body().nodes()) {
    needed |= type_op_of(node) == Ntype_op::Rxor || type_op_of(node) == Ntype_op::Popcount;
  }
  if (!needed) {
    return graph;
  }
  if (!scratch.copy_from(*graph->get_io()->get_library(), graph->get_name())) {
    throw std::runtime_error("could not copy graph for reduction expansion");
  }
  auto                          result = scratch.find_graph_rw(graph->get_name());
  std::vector<hhds::Node_class> reductions;
  for (auto node : result->body().nodes()) {
    if (type_op_of(node) == Ntype_op::Rxor || type_op_of(node) == Ntype_op::Popcount) {
      reductions.push_back(node);
    }
  }
  for (auto node : reductions) {
    const int                                    count  = reduction_count(node);
    const bool                                   parity = type_op_of(node) == Ntype_op::Rxor;
    const auto                                   input  = get_driver_of_sink_name(node, "a");
    std::vector<std::pair<hhds::Pin_class, int>> level;
    for (int bit = 0; bit < count; ++bit) {
      auto slice = create_get_mask(*result, input, bit, bit + 1);
      auto value = slice.create_driver_pin(0);
      set_ubits(value, 1);
      level.emplace_back(value, 1);
    }
    while (level.size() > 1) {
      std::vector<std::pair<hhds::Pin_class, int>> next;
      for (size_t i = 0; i < level.size(); i += 2) {
        if (i + 1 == level.size()) {
          next.push_back(level[i]);
          continue;
        }
        auto combine = create_typed_node(*result, parity ? Ntype_op::Xor : Ntype_op::Sum);
        level[i].first.connect_sink(livehd::graph_util::setup_sink_pid(combine, 0));
        level[i + 1].first.connect_sink(livehd::graph_util::setup_sink_pid(combine, 0));
        const int width  = parity ? 1 : std::max(level[i].second, level[i + 1].second) + 1;
        auto      output = combine.create_driver_pin(0);
        set_ubits(output, width);
        next.emplace_back(output, width);
      }
      level = std::move(next);
    }
    const auto output = level.empty() ? create_const(*result, *Dlop::create_integer(0)) : level.front().first;
    for (const auto& edge : node.out_edges()) {
      output.connect_sink(edge.sink);
    }
    node.del_node();
  }
  return result;
}

}  // namespace livehd::graph_util
