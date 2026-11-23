// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <vector>

#include "absl/container/flat_hash_set.h"
#include "node_util.hpp"

namespace livehd::graph_util {

// Visit each reachable supplied definition once, children before parents. A
// missing definition is an external boundary. The caller supplies the body
// iteration order and can account for work while enumerating nodes.
template <class Roots, class Definitions, class Visit_nodes>
auto callee_first_order(const Roots& roots, const Definitions& definitions, Visit_nodes visit_nodes) {
  using Handle = typename Definitions::mapped_type;
  std::vector<Handle>            order;
  absl::flat_hash_set<hhds::Gid> seen;
  const auto                     visit = [&](auto& self, const Handle& graph) -> void {
    if (!graph || !graph->get_io() || !seen.insert(graph->get_gid()).second) {
      return;
    }
    visit_nodes(*graph, [&](const hhds::Node_class& node) {
      if (!node.is_invalid() && is_type_sub(node)) {
        if (auto child = definitions.find(node.get_subnode_gid()); child != definitions.end()) {
          self(self, child->second);
        }
      }
    });
    order.push_back(graph);
  };
  for (const auto& root : roots) {
    visit(visit, root);
  }
  return order;
}

}  // namespace livehd::graph_util
