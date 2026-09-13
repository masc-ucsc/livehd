// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "loop_cleanup.hpp"

#include <functional>

#include "absl/container/flat_hash_set.h"
#include "bitwidth.hpp"
#include "color_reduce.hpp"
#include "cprop.hpp"
#include "diag.hpp"
#include "inline_sub.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"

namespace livehd::abc {
namespace {
bool cleanup_loop_bodies(const std::vector<std::shared_ptr<hhds::Graph>>& graphs,
                         const absl::flat_hash_set<hhds::Node_class>&     expanded_instances) {
  namespace gu = livehd::graph_util;
  std::unordered_set<hhds::Gid>                            visited;
  std::function<bool(const std::shared_ptr<hhds::Graph>&)> visit = [&](const auto& graph) {
    if (!visited.insert(graph->get_gid()).second) {
      return true;
    }
    std::vector<hhds::Node_class> instances;
    for (const auto node : graph->body().nodes()) {
      if (gu::type_op_of(node) == Ntype_op::Sub && node.get_subnode_graph()) {
        instances.push_back(node);
      }
    }
    bool changed = false;
    for (const auto& inst : instances) {
      auto child = inst.get_subnode_graph();
      if (!visit(child)) {
        return false;
      }
      if (!expanded_instances.contains(inst)) {
        continue;
      }
      // Colors are local to each definition. A source block with its own ABC
      // settings remains a boundary so those settings retain their identity.
      auto info = child->get_input_node().attr(livehd::attrs::coloring_info);
      if (info.has() && std::string_view(info.get()).find("region_opts") != std::string_view::npos) {
        continue;
      }
      if (!gu::inline_sub_instance(graph.get(), inst, "pass.abc", nullptr, false, true, true)) {
        return false;
      }
      changed = true;
    }
    if (changed) {
      // Reduction may have hidden a loop's control expression in a shared
      // pattern. Once iteration inputs become constants, specialize just those
      // sites; patterns whose inputs remain dynamic still map once for reuse.
      bool specialized;
      do {
        Cprop{}.do_trans(graph, false);
        Bitwidth{3}.do_trans(graph);
        Cprop{}.do_trans(graph, false);
        Bitwidth{3}.do_trans(graph);
        std::vector<hhds::Node_class> constant_patterns;
        for (const auto node : graph->body().nodes()) {
          auto child = gu::type_op_of(node) == Ntype_op::Sub ? node.get_subnode_graph() : nullptr;
          if (!child || !livehd::color::is_pattern_def_name(child->get_name())) {
            continue;
          }
          for (const auto& edge : node.inp_edges()) {
            if (edge.driver.is_const()) {
              constant_patterns.push_back(node);
              break;
            }
          }
        }
        specialized = !constant_patterns.empty();
        for (const auto& inst : constant_patterns) {
          if (!gu::inline_sub_instance(graph.get(), inst, "pass.abc", nullptr, false, true, true)) {
            return false;
          }
        }
      } while (specialized);
    }
    return true;
  };
  for (const auto& graph : graphs) {
    if (!visit(graph)) {
      return false;
    }
  }
  return true;
}
}  // namespace

bool prepare_loop_bodies(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, bool unroll_carry, Loop_preparation& result) {
  result = {};
  absl::flat_hash_set<hhds::Node_class> expanded_instances;
  for (const auto& graph : graphs) {
    std::vector<hhds::Node_class> loops;
    for (const auto node : graph->body().nodes()) {
      if (node.is_loop_subnode()) {
        loops.push_back(node);
      }
    }
    // Preserve the source module's ordinal naming while expanding selected sites.
    for (auto it = loops.rbegin(); it != loops.rend(); ++it) {
      const auto group = it->subnode_group();
      try {
        group.validate();
      } catch (const std::exception& e) {
        livehd::diag::err("pass.abc", "replica-expand", "internal")
            .msg("invalid compact loop '{}': {}", livehd::graph_util::default_instance_name(*it), e.what())
            .emit();
        return false;
      }
      const auto desc    = group.loop();
      const bool carried = !group.carries().empty() || (desc->activation_input && desc->next_active_output);
      carried ? ++result.carried : ++result.independent;
      if (!carried || !unroll_carry) {
        result.preserved_defs.insert(it->get_subnode_gid());
        continue;
      }
      std::vector<hhds::Node_class> replicas;
      if (!livehd::graph_util::materialize_occurrence(graph.get(), *it, "pass.abc", &replicas)) {
        return false;
      }
      expanded_instances.insert(replicas.begin(), replicas.end());
      ++result.expanded;
    }
  }
  return cleanup_loop_bodies(graphs, expanded_instances);
}
}  // namespace livehd::abc
