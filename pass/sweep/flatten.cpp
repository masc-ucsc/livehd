// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <deque>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "clock_gate.hpp"
#include "inline_sub.hpp"
#include "node_util.hpp"
#include "sweep.hpp"

namespace livehd::sweep {
namespace gu = livehd::graph_util;

Flatten_stats flatten(const std::vector<std::shared_ptr<hhds::Graph>>& roots, Flat_origin* origin) {
  Flatten_stats stats;
  if (origin != nullptr && roots.size() != 1) {
    origin = nullptr;  // provenance is per root
  }
  for (const auto& root : roots) {
    if (!root) {
      continue;
    }
    gu::Host_inliner                                inliner(root.get(), "pass.sweep");
    std::deque<hhds::Node_class>                    work;
    // Provenance: the context of every Sub still in the root (by class index).
    absl::flat_hash_map<hhds::Class_index, int32_t> context_of;
    if (origin != nullptr) {
      origin->contexts.clear();
      origin->flops.clear();
      origin->memories.clear();
      origin->contexts.push_back({.parent = -1, .sub = 0, .def = root->get_gid()});
    }
    for (auto node : root->body().nodes()) {
      const auto op = gu::type_op_of(node);
      if (op == Ntype_op::Sub) {
        work.push_back(node);
        if (origin != nullptr) {
          const auto callee = node.get_subnode_graph();
          context_of.emplace(node.get_class_index(), static_cast<int32_t>(origin->contexts.size()));
          origin->contexts.push_back({.parent = 0, .sub = node.get_debug_nid(), .def = callee ? callee->get_gid() : hhds::Gid{}});
        }
      } else if (op == Ntype_op::Flop && origin != nullptr) {
        origin->flops.emplace(node.get_class_index(), std::pair{int32_t{0}, node.get_debug_nid()});
      } else if (op == Ntype_op::Memory && origin != nullptr) {
        origin->memories.emplace(node.get_class_index(), std::pair{int32_t{0}, node.get_debug_nid()});
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
      if (clock_gate::classify_leaf(*inst.get_subnode_graph()) != clock_gate::Leaf_kind::none) {
        ++stats.kept;
        continue;
      }
      std::vector<hhds::Node_class>                              children;
      std::vector<std::pair<hhds::Node_class, hhds::Node_class>> clones;
      int32_t                                                    context = -1;
      if (origin != nullptr) {
        if (auto it = context_of.find(inst.get_class_index()); it != context_of.end()) {
          context = it->second;
        }
      }
      if (!inliner.splice(inst, &children, context >= 0 ? &clones : nullptr)) {
        ++stats.kept;
        continue;
      }
      ++stats.inlined;
      if (context >= 0) {
        for (const auto& [body_node, clone] : clones) {
          const auto op = gu::type_op_of(body_node);
          if (op == Ntype_op::Flop) {
            origin->flops.emplace(clone.get_class_index(), std::pair{context, body_node.get_debug_nid()});
          } else if (op == Ntype_op::Memory) {
            origin->memories.emplace(clone.get_class_index(), std::pair{context, body_node.get_debug_nid()});
          } else if (op == Ntype_op::Sub) {
            const auto callee = body_node.get_subnode_graph();
            context_of.emplace(clone.get_class_index(), static_cast<int32_t>(origin->contexts.size()));
            origin->contexts.push_back(
                {.parent = context, .sub = body_node.get_debug_nid(), .def = callee ? callee->get_gid() : hhds::Gid{}});
          }
        }
      }
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
