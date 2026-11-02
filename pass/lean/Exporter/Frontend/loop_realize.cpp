// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "loop_realize.hpp"

#include <string>

#include "absl/container/flat_hash_set.h"
#include "graph_access.hpp"
#include "inline_sub.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"

namespace lean_export {

namespace {

constexpr std::string_view kPass = "pass.lean";

[[noreturn]] void refuse(const std::string& msg) { throw lean_pass::Emit_error("[ERROR] pass.lean: " + msg); }

std::vector<hhds::Node_class> loop_subs_of(const hhds::Graph& graph) {
  std::vector<hhds::Node_class> loops;
  for (auto node : graph.body().nodes()) {
    if (node.is_loop_subnode()) {
      loops.push_back(node);
    }
  }
  return loops;
}

}  // namespace

std::shared_ptr<hhds::Graph> realize_rolled_loops_copy(const std::shared_ptr<hhds::Graph>& graph, hhds::GraphLibrary& scratch) {
  if (!graph || loop_subs_of(*graph).empty()) {
    return graph;
  }
  const auto  io  = graph->get_io();
  const auto* lib = io ? io->get_library() : nullptr;
  if (lib == nullptr) {
    refuse("graph '" + std::string(graph->get_name()) + "' holds a rolled loop but has no graph library to copy it from");
  }
  // Copy the whole definition closure: the replicas instantiate the loop body
  // by gid, and the splice reads that body (and any loop nested in it) from the
  // library the copied top resolves through. copy_from keeps the name-hash gid,
  // so a copied parent's Sub still names the copied child.
  for (const auto& def : graph->definitions().graphs()) {
    if (!scratch.copy_from(*lib, def->get_name())) {
      refuse("could not copy definition '" + std::string(def->get_name()) + "' to realize the rolled loops of '"
             + std::string(graph->get_name()) + "'");
    }
  }
  auto top = scratch.find_graph_rw(graph->get_name());
  if (!top) {
    refuse("could not copy '" + std::string(graph->get_name()) + "' to realize its rolled loops");
  }
  // One round per nesting level: splicing a replica brings the body's own loop
  // Subs into the top, and the next round realizes them. The definition DAG is
  // finite, so the rounds end.
  for (auto loops = loop_subs_of(*top); !loops.empty(); loops = loop_subs_of(*top)) {
    // Reverse storage order, as materialize_occurrences does: the occurrence
    // namer counts the loop sites stored before this one, so they must still be
    // compact when it runs.
    for (auto it = loops.rbegin(); it != loops.rend(); ++it) {
      const auto                    site = livehd::graph_util::debug_name(*it);
      std::vector<hhds::Node_class> replicas;
      if (!livehd::graph_util::materialize_occurrence(top.get(), *it, kPass, &replicas)) {
        refuse("could not expand the rolled loop " + site + " into one instance per iteration (see the error above)");
      }
      for (const auto& replica : replicas) {
        if (!livehd::graph_util::inline_sub_instance(top.get(), replica, kPass)) {
          refuse("could not inline iteration " + livehd::graph_util::debug_name(replica) + " of the rolled loop " + site
                 + " (see the error above)");
        }
      }
    }
  }
  return top;
}

std::vector<bool> loop_body_only(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  absl::flat_hash_set<hhds::Gid> loop_bodies;
  absl::flat_hash_set<hhds::Gid> plain_subs;
  for (const auto& graph : graphs) {
    if (!graph) {
      continue;
    }
    for (auto node : graph->body().nodes()) {
      if (livehd::graph_util::type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      (node.is_loop_subnode() ? loop_bodies : plain_subs).insert(node.get_subnode_gid());
    }
  }
  std::vector<bool> result;
  result.reserve(graphs.size());
  for (const auto& graph : graphs) {
    result.push_back(graph && loop_bodies.contains(graph->get_gid()) && !plain_subs.contains(graph->get_gid()));
  }
  return result;
}

}  // namespace lean_export
