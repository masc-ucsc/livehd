// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <deque>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "inline_sub.hpp"
#include "node_util.hpp"
#include "sweep.hpp"

namespace livehd::sweep {
namespace gu = livehd::graph_util;

Flatten_stats flatten(const std::vector<std::shared_ptr<hhds::Graph>>& roots) {
  Flatten_stats stats;
  for (const auto& root : roots) {
    if (!root) {
      continue;
    }
    gu::Host_inliner inliner(root.get(), "pass.sweep");
    std::deque<hhds::Node_class> work;
    for (auto node : root->body().nodes()) {
      if (gu::type_op_of(node) == Ntype_op::Sub) {
        work.push_back(node);
      }
    }
    while (!work.empty()) {
      auto inst = work.front();
      work.pop_front();
      if (inst.is_invalid() || gu::type_op_of(inst) != Ntype_op::Sub) {
        continue;
      }
      // A black box (a Liberty cell or macro, external IP) has no body to
      // splice, and a rolled loop keeps its compact form.
      if (!inst.get_subnode_graph() || inst.subnode_loop()) {
        ++stats.kept;
        continue;
      }
      std::vector<hhds::Node_class> children;
      if (!inliner.splice(inst, &children)) {
        ++stats.kept;
        continue;
      }
      ++stats.inlined;
      for (const auto& child : children) {
        work.push_back(child);
      }
    }
    inliner.flush();
  }
  return stats;
}

std::vector<std::shared_ptr<hhds::Graph>> reachable_closure(const std::vector<std::shared_ptr<hhds::Graph>>& roots) {
  std::vector<std::shared_ptr<hhds::Graph>> out;
  absl::flat_hash_set<const hhds::Graph*>   seen;
  std::deque<std::shared_ptr<hhds::Graph>>  pending(roots.begin(), roots.end());
  while (!pending.empty()) {
    auto graph = pending.front();
    pending.pop_front();
    if (!graph || !seen.insert(graph.get()).second) {
      continue;
    }
    out.push_back(graph);
    for (auto node : graph->body().nodes()) {
      if (gu::type_op_of(node) == Ntype_op::Sub) {
        if (auto callee = node.get_subnode_graph()) {
          pending.push_back(std::move(callee));
        }
      }
    }
  }
  return out;
}

}  // namespace livehd::sweep
