// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "loop_cleanup.hpp"

#include <functional>

#include "absl/container/flat_hash_set.h"
#include "attr_carry.hpp"
#include "bitwidth.hpp"
#include "color_reduce.hpp"
#include "cprop.hpp"
#include "diag.hpp"
#include "inline_sub.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"

namespace livehd::abc {
namespace {
// Lift the carry-independent data computation before expanding a recurrence.
// The resulting ordinary instance is replicated with each ordinal, while ABC
// maps its body once. Index-only expressions stay beside the carry so ordinal
// specialization can simplify slice masks and reduction routing.
std::shared_ptr<hhds::Graph> extract_parallel_data(const std::shared_ptr<hhds::Graph>&       body,
                                                   const absl::flat_hash_set<hhds::Port_id>& carry_ports,
                                                   const absl::flat_hash_set<hhds::Port_id>& index_ports) {
  namespace gu         = livehd::graph_util;
  const auto propagate = [&](std::vector<hhds::Pin_class> work) {
    absl::flat_hash_set<hhds::Node_class> reached;
    absl::flat_hash_set<hhds::Pin_class>  visited;
    while (!work.empty()) {
      auto pin = work.back();
      work.pop_back();
      if (!visited.insert(pin).second) {
        continue;
      }
      for (const auto& edge : pin.out_edges()) {
        auto node = edge.sink.get_master_node();
        if (gu::is_builtin_node(node) || gu::is_type_flop(node) || gu::type_op_of(node) == Ntype_op::Latch
            || gu::type_op_of(node) == Ntype_op::Memory || !reached.insert(node).second) {
          continue;
        }
        for (const auto& output : node.out_edges()) {
          work.push_back(output.driver);
        }
      }
    }
    return reached;
  };
  std::vector<hhds::Pin_class> carry_inputs, data_inputs;
  for (const auto& port : body->get_io()->get_input_pin_decls()) {
    auto pin = body->get_input_pin(port.name);
    if (carry_ports.contains(port.port_id)) {
      carry_inputs.push_back(pin);
    } else if (!index_ports.contains(port.port_id)) {
      data_inputs.push_back(pin);
    }
  }
  const auto                            dependent = propagate(std::move(carry_inputs));
  const auto                            data      = propagate(std::move(data_inputs));
  absl::flat_hash_set<hhds::Node_class> selected;
  std::vector<hhds::Node_class>         nodes;
  for (auto node : body->body().nodes()) {
    if (!gu::is_builtin_node(node) && data.contains(node) && !dependent.contains(node) && gu::type_op_of(node) != Ntype_op::Sub) {
      selected.insert(node);
      nodes.push_back(node);
    }
  }
  if (nodes.empty()) {
    return {};
  }

  std::vector<hhds::Pin_class>                 inputs, outputs;
  absl::flat_hash_map<hhds::Pin_class, size_t> input_ids, output_ids;
  for (auto node : nodes) {
    for (const auto& edge : node.inp_edges()) {
      if (!edge.driver.is_const() && !selected.contains(edge.driver.get_master_node())
          && input_ids.emplace(edge.driver, inputs.size()).second) {
        inputs.push_back(edge.driver);
      }
    }
    for (const auto& edge : node.out_edges()) {
      if (!selected.contains(edge.sink.get_master_node()) && output_ids.emplace(edge.driver, outputs.size()).second) {
        outputs.push_back(edge.driver);
      }
    }
  }
  if (outputs.empty()) {
    return {};
  }
  auto*       lib  = body->get_io()->get_library();
  std::string name = std::string(body->get_name()) + ".__parallel";
  for (size_t suffix = 1; lib->find_io(name); ++suffix) {
    name = std::string(body->get_name()) + ".__parallel" + std::to_string(suffix);
  }
  auto io = lib->create_io(name);
  for (size_t i = 0; i < inputs.size(); ++i) {
    const auto port = "i" + std::to_string(i);
    io->add_input(port, static_cast<hhds::Port_id>(i + 1));
    io->set_bits(port, std::max(1, gu::bits_of(inputs[i])));
    io->set_unsign(port, gu::is_unsign(inputs[i]));
  }
  for (size_t i = 0; i < outputs.size(); ++i) {
    const auto port = "o" + std::to_string(i);
    io->add_output(port, static_cast<hhds::Port_id>(inputs.size() + i + 1));
    io->set_bits(port, std::max(1, gu::bits_of(outputs[i])));
    io->set_unsign(port, gu::is_unsign(outputs[i]));
  }
  auto                                                    shared = io->create_graph();
  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> copies;
  absl::flat_hash_map<hhds::Pin_class, hhds::Pin_class>   pins;
  for (size_t i = 0; i < inputs.size(); ++i) {
    auto pin = shared->get_input_pin("i" + std::to_string(i));
    gu::carry_pin_attrs(inputs[i], pin);
    pins.emplace(inputs[i], pin);
  }
  for (auto node : nodes) {
    auto copy = gu::create_typed_node(*shared, gu::type_op_of(node));
    gu::carry_node_attrs(node, copy);
    gu::carry_srcid(node, copy, body.get(), lib);
    gu::set_color(copy, 1);
    copies.emplace(node, copy);
  }
  const auto driver = [&](const hhds::Pin_class& original) {
    if (auto it = pins.find(original); it != pins.end()) {
      return it->second;
    }
    auto copy = original.is_const() ? gu::create_const(*shared, gu::const_of(original))
                                    : copies.at(original.get_master_node()).create_driver_pin(original.get_port_id());
    gu::carry_pin_attrs(original, copy);
    pins.emplace(original, copy);
    return copy;
  };
  for (auto node : nodes) {
    for (const auto& edge : node.inp_edges()) {
      auto sink = copies.at(node).create_sink_pin(edge.sink.get_port_id());
      gu::carry_pin_attrs(edge.sink, sink);
      driver(edge.driver).connect_sink(sink);
    }
  }
  auto instance = gu::create_typed_node(*body, Ntype_op::Sub);
  instance.set_subnode(io);
  instance.set_name("__parallel_data");
  for (size_t i = 0; i < inputs.size(); ++i) {
    inputs[i].connect_sink(instance.create_sink_pin(static_cast<hhds::Port_id>(i + 1)));
  }
  for (size_t i = 0; i < outputs.size(); ++i) {
    driver(outputs[i]).connect_sink(shared->get_output_pin("o" + std::to_string(i)));
    auto result = instance.create_driver_pin(static_cast<hhds::Port_id>(inputs.size() + i + 1));
    gu::carry_pin_attrs(outputs[i], result);
    std::vector<hhds::Pin_class> readers;
    for (const auto& edge : outputs[i].out_edges()) {
      if (!selected.contains(edge.sink.get_master_node())) {
        readers.push_back(edge.sink);
      }
    }
    for (auto sink : readers) {
      result.connect_sink(sink);
    }
  }
  for (auto node : nodes) {
    node.del_node();
  }
  return shared;
}

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
  if (unroll_carry) {
    absl::flat_hash_map<hhds::Gid, absl::flat_hash_set<hhds::Port_id>> carry_ports, index_ports;
    absl::flat_hash_map<hhds::Gid, std::shared_ptr<hhds::Graph>>       bodies;
    for (const auto& graph : graphs) {
      for (auto node : graph->body().nodes()) {
        if (!node.is_loop_subnode() || !node.get_subnode_graph()) {
          continue;
        }
        auto gid    = node.get_subnode_gid();
        bodies[gid] = node.get_subnode_graph();
        auto group  = node.subnode_group();
        for (const auto& carry : group.carries()) {
          carry_ports[gid].insert(carry.input_port());
        }
        auto loop = group.loop();
        if (loop->index_input) {
          index_ports[gid].insert(*loop->index_input);
        }
        if (loop->activation_input) {
          // A break recurrence is carry logic too. An invariant activation
          // alone also should not make index-only routing look like data.
          index_ports[gid].insert(*loop->activation_input);
          if (loop->next_active_output) {
            carry_ports[gid].insert(*loop->activation_input);
          }
        }
      }
    }
    for (const auto& [gid, ports] : carry_ports) {
      if (auto shared = extract_parallel_data(bodies.at(gid), ports, index_ports[gid])) {
        result.preserved_defs.insert(shared->get_gid());
        result.shared_bodies.push_back(std::move(shared));
      }
    }
  }
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
