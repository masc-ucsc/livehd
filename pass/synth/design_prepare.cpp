// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_prepare.hpp"

#include <unordered_set>

#include "diag.hpp"
#include "node_util.hpp"
#include "pass_partition.hpp"

namespace livehd::synth {

std::unique_ptr<Prepared_design> prepare_design(std::span<const std::shared_ptr<hhds::Graph>> sources, bool unroll_carry,
                                                std::string_view from_pass, Preparation_budget* budget) {
  if (!admit_preparation(budget, "begin", 0)) {
    return {};
  }
  auto result = std::make_unique<Prepared_design>();
  for (const auto& source : sources) {
    if (!source) {
      continue;
    }
    auto  io      = source->get_io();
    auto* library = io ? io->get_library() : nullptr;
    if (!library) {
      diag::err(from_pass, "scratch-copy", "internal")
          .msg("could not copy '{}' into the private synthesis library", source->get_name())
          .emit();
      return {};
    }
    if (!admit_preparation(budget, "definitions", 0)) {
      return {};
    }
    // copy_from is definition-local; a copied instance resolves only through
    // the destination library. Include callees absent from the input list.
    for (const auto& graph : source->definitions().graphs()) {
      if (result->library.find_io(graph->get_name())) {
        continue;
      }
      if (budget) {
        // Admit the complete source shape before copying its body. Count ports,
        // pins and edges as work too: a small node count need not mean a small
        // graph, especially for opaque interfaces and wide multi-input nodes.
        const auto graph_io = graph->get_io();
        for (const auto& port : graph_io->get_input_pin_decls()) {
          (void)port;
          if (!budget->check("source-port")) {
            return {};
          }
        }
        for (const auto& port : graph_io->get_output_pin_decls()) {
          (void)port;
          if (!budget->check("source-port")) {
            return {};
          }
        }
        const auto admit_pins = [&](const hhds::Node_class& node) {
          for (const auto& pin : node.inp_sorted_pins()) {
            if (!budget->check("source-pin")) {
              return false;
            }
            for (const auto& driver : pin.get_driver_pins()) {
              (void)driver;
              if (!budget->check("source-edge")) {
                return false;
              }
            }
          }
          for (const auto& pin : node.out_sorted_pins()) {
            (void)pin;
            if (!budget->check("source-pin")) {
              return false;
            }
          }
          return true;
        };
        // body().nodes() excludes the primary IO nodes. Include their pins
        // explicitly so output drivers and direct feedthrough edges are charged.
        if (!admit_pins(graph->get_input_node()) || !admit_pins(graph->get_output_node())) {
          return {};
        }
        for (const auto node : graph->body().nodes()) {
          if (!budget->source_node() || !admit_pins(node)) {
            return {};
          }
        }
      }
      if (!admit_preparation(budget, "copy", 0)) {
        return {};
      }
      if (!result->library.copy_from(*library, graph->get_name())) {
        diag::err(from_pass, "scratch-copy", "internal")
            .msg("could not copy '{}' into the private synthesis library", graph->get_name())
            .emit();
        return {};
      }
      if (!admit_preparation(budget, "copied", 0)) {
        return {};
      }
      for (const auto node : graph->body().nodes()) {
        if (!admit_preparation(budget, "opaque-scan")) {
          return {};
        }
        if (graph_util::type_op_of(node) == Ntype_op::Sub && node.get_subnode_io() && !node.get_subnode_graph()
            && !result->library.find_io(node.get_subnode_io()->get_name())) {
          if (budget) {
            for (const auto& port : node.get_subnode_io()->get_input_pin_decls()) {
              (void)port;
              if (!budget->check("opaque-port")) {
                return {};
              }
            }
            for (const auto& port : node.get_subnode_io()->get_output_pin_decls()) {
              (void)port;
              if (!budget->check("opaque-port")) {
                return {};
              }
            }
          }
          if (!admit_preparation(budget, "opaque-copy", 0)) {
            return {};
          }
          partition::resolve_or_clone_subdef(&result->library, node);
          if (!admit_preparation(budget, "opaque-copied", 0)) {
            return {};
          }
        }
      }
    }
  }
  for (const auto& source : sources) {
    if (!admit_preparation(budget, "roots")) {
      return {};
    }
    auto io = source ? result->library.find_io(source->get_name()) : std::shared_ptr<hhds::GraphIO>{};
    result->roots.push_back(io ? io->get_graph() : std::shared_ptr<hhds::Graph>{});
  }
  for (const auto gid : result->library.all_gids()) {
    if (!admit_preparation(budget, "copied-definitions")) {
      return {};
    }
    if (auto graph = result->library.get_graph(gid)) {
      result->definitions.push_back(std::move(graph));
    }
  }
  if (!prepare_loop_bodies(result->definitions, unroll_carry, result->loops, from_pass, budget)) {
    return {};
  }
  result->definitions.insert(result->definitions.end(), result->loops.shared_bodies.begin(), result->loops.shared_bodies.end());
  result->resolve_graphs = result->roots;
  std::unordered_set<hhds::Gid> listed;
  for (const auto& graph : result->roots) {
    if (graph) {
      listed.insert(graph->get_gid());
    }
  }
  for (const auto& graph : result->definitions) {
    if (!admit_preparation(budget, "resolve")) {
      return {};
    }
    if (listed.insert(graph->get_gid()).second) {
      result->resolve_graphs.push_back(graph);
    }
  }
  if (!admit_preparation(budget, "complete", 0)) {
    return {};
  }
  return result;
}

}  // namespace livehd::synth
