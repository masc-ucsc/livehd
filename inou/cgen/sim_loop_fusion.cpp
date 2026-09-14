// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sim_loop_fusion.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <numeric>
#include <queue>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "attr_carry.hpp"
#include "node_util.hpp"

namespace livehd::sim {
namespace {
namespace gu = livehd::graph_util;

std::shared_ptr<hhds::Graph> fuse_group(hhds::Graph* graph, const std::vector<hhds::Node_class>& group) {
  auto*       library  = graph->get_io()->get_library();
  size_t      sequence = 0;
  std::string name;
  while (true) {
    name = "__sim_fused_loop_" + std::to_string(graph->get_gid()) + "_" + std::to_string(sequence);
    if (!library->find_io(name)) {
      break;  // `sequence` now NAMES this body, so the instance can reuse it
    }
    ++sequence;
  }
  auto io         = library->create_io(name);
  auto descriptor = *group.front().subnode_loop();
  descriptor.index_input.reset();
  hhds::Port_id next_port      = 0;
  uint32_t      index_bits     = 1;
  bool          index_unsigned = true;
  // The shared index port must represent EVERY member's ordinal domain. Taking
  // the max width and the AND of the signs is not enough on its own: a signed
  // carrier reads an unsigned member's top bit as the sign, so an N-bit
  // unsigned index needs N+1 bits once any member forces the port signed.
  uint32_t index_unsigned_bits = 1;
  uint32_t index_signed_bits   = 0;
  for (const auto& loop : group) {
    const auto index = loop.subnode_loop()->index_input;
    if (!index) {
      continue;
    }
    for (const auto& port : loop.get_subnode_io()->get_input_pin_decls()) {
      if (port.port_id == *index) {
        if (port.unsign) {
          index_unsigned_bits = std::max(index_unsigned_bits, port.bits);
        } else {
          index_signed_bits = std::max(index_signed_bits, port.bits);
          index_unsigned    = false;
        }
      }
    }
    descriptor.index_input = 0;
  }
  index_bits = index_unsigned ? index_unsigned_bits : std::max(index_signed_bits, index_unsigned_bits + 1);
  if (descriptor.index_input) {
    io->add_input("index", next_port++);
    io->set_bits("index", index_bits);
    io->set_unsign("index", index_unsigned);
  }
  struct Ports {
    std::map<hhds::Port_id, hhds::Port_id> inputs, outputs;
    std::map<hhds::Port_id, std::string>   input_names, output_names;
  };
  std::vector<Ports> ports(group.size());
  for (size_t i = 0; i < group.size(); ++i) {
    const auto old_io = group[i].get_subnode_io();
    for (const auto& port : old_io->get_input_pin_decls()) {
      if (group[i].subnode_loop()->index_input == port.port_id) {
        ports[i].inputs.emplace(port.port_id, *descriptor.index_input);
        ports[i].input_names.emplace(port.port_id, "index");
        continue;
      }
      const auto pname = "g" + std::to_string(i) + "_i" + std::to_string(port.port_id);
      io->add_input(pname, next_port);
      io->set_bits(pname, std::max<uint32_t>(1, port.bits));
      io->set_unsign(pname, port.unsign);
      ports[i].inputs.emplace(port.port_id, next_port++);
      ports[i].input_names.emplace(port.port_id, pname);
    }
    for (const auto& port : old_io->get_output_pin_decls()) {
      const auto pname = "g" + std::to_string(i) + "_o" + std::to_string(port.port_id);
      io->add_output(pname, next_port);
      io->set_bits(pname, std::max<uint32_t>(1, port.bits));
      io->set_unsign(pname, port.unsign);
      ports[i].outputs.emplace(port.port_id, next_port++);
      ports[i].output_names.emplace(port.port_id, pname);
    }
  }
  auto body = io->create_graph();
  // GraphIO declarations and driver realization hints are separate. This
  // rewrite runs after bitwidth, so publish the hints for the new ABI inputs
  // explicitly; otherwise LLVM sees the default one-bit graph input width.
  for (const auto& port : io->get_input_pin_decls()) {
    auto pin = body->get_input_pin(port.name);
    gu::set_bits(pin, port.bits);
    if (port.unsign) {
      gu::set_unsign(pin);
    } else {
      gu::set_sign(pin);
    }
  }
  auto fused = gu::create_typed_node(*graph, Ntype_op::Sub);
  fused.set_subnode(io, descriptor);
  fused.set_name("u_fused_" + std::to_string(sequence));
  for (size_t i = 0; i < group.size(); ++i) {
    const auto& original = group[i];
    auto        child    = gu::create_typed_node(*body, Ntype_op::Sub);
    child.set_subnode(original.get_subnode_io());
    child.set_name("part_" + std::to_string(i));
    for (const auto& [old_port, new_port] : ports[i].inputs) {
      body->get_input_pin(ports[i].input_names.at(old_port)).connect_sink(child.create_sink_pin(old_port));
      (void)new_port;
    }
    for (const auto& [old_port, new_port] : ports[i].outputs) {
      auto output = child.create_driver_pin(old_port);
      output.connect_sink(body->get_output_pin(ports[i].output_names.at(old_port)));
      auto       fused_output = fused.create_driver_pin(new_port);
      const auto old_output   = original.get_driver_pin(old_port);
      if (!old_output.is_invalid()) {
        gu::carry_pin_attrs(old_output, output);
        gu::carry_pin_attrs(old_output, fused_output);
      }
    }
    for (const auto& edge : original.inp_edges()) {
      if (edge.driver.get_master_node() == original || original.subnode_loop()->index_input == edge.sink.get_port_id()) {
        continue;
      }
      edge.driver.connect_sink(fused.create_sink_pin(ports[i].inputs.at(edge.sink.get_port_id())));
    }
    for (const auto& carry : original.subnode_group().carries()) {
      fused.create_driver_pin(ports[i].outputs.at(carry.output_port()))
          .connect_sink(fused.create_sink_pin(ports[i].inputs.at(carry.input_port())));
    }
    // Snapshot external readers before creating edges to their existing pins.
    std::vector<std::pair<hhds::Port_id, hhds::Pin_class>> readers;
    for (const auto& edge : original.out_edges()) {
      if (edge.sink.get_master_node() != original) {
        readers.emplace_back(edge.driver.get_port_id(), edge.sink);
      }
    }
    for (const auto& [port, sink] : readers) {
      fused.create_driver_pin(ports[i].outputs.at(port)).connect_sink(sink);
    }
  }
  for (const auto& original : group) {
    original.del_node();
  }
  fused.subnode_group().validate();
  return body;
}
}  // namespace

std::vector<std::shared_ptr<hhds::Graph>> fuse_parallel_loops(hhds::Graph* graph) {
  std::vector<std::shared_ptr<hhds::Graph>> created;
  if (graph == nullptr || graph->get_io() == nullptr) {
    return created;  // a detached/placeholder definition owns no library to mint into
  }
  std::vector<hhds::Node_class>                 nodes;
  absl::flat_hash_map<hhds::Node_class, size_t> ids;
  size_t                                        loop_count = 0;
  for (auto node : graph->body().nodes()) {
    if (!gu::is_builtin_node(node)) {
      ids.emplace(node, nodes.size());
      nodes.push_back(node);
      loop_count += node.is_loop_subnode() ? 1 : 0;
    }
  }
  if (loop_count < 2) {
    return created;
  }
  const size_t                     words = (loop_count + 63) / 64;
  std::vector<uint64_t>            ancestors(nodes.size() * words);
  std::vector<size_t>              indegree(nodes.size());
  std::vector<std::vector<size_t>> successors(nodes.size());
  for (size_t sink = 0; sink < nodes.size(); ++sink) {
    for (const auto& edge : nodes[sink].inp_edges()) {
      const auto producer = edge.driver.get_master_node();
      if (producer == nodes[sink] && nodes[sink].is_loop_subnode()) {
        continue;  // ordinal carry, not a dependency between loops
      }
      if (auto it = ids.find(producer); it != ids.end()) {
        ++indegree[sink];
        successors[it->second].push_back(sink);
      }
    }
  }
  std::priority_queue<size_t, std::vector<size_t>, std::greater<>> ready;
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (indegree[i] == 0) {
      ready.push(i);
    }
  }
  std::vector<std::vector<hhds::Node_class>> groups;
  std::vector<hhds::Node_class>              group;
  std::vector<size_t>                        group_bits;
  size_t                                     loop_bit    = 0;
  size_t                                     visited     = 0;
  const auto                                 close_group = [&] {
    if (group.size() > 1) {
      groups.push_back(group);
    }
    group.clear();
    group_bits.clear();
  };
  while (!ready.empty()) {
    const size_t id = ready.top();
    ready.pop();
    ++visited;
    if (auto loop = nodes[id].subnode_loop()) {
      const bool eligible
          = loop->count != 0 && !loop->activation_input && !loop->next_active_output && nodes[id].get_subnode_graph() != nullptr;
      bool compatible = eligible && group.size() < 8;
      if (!group.empty()) {
        const auto previous  = *group.front().subnode_loop();
        compatible          &= loop->first == previous.first && loop->step == previous.step && loop->count == previous.count;
        for (const size_t bit : group_bits) {
          compatible &= (ancestors[id * words + bit / 64] & (uint64_t{1} << (bit % 64))) == 0;
        }
      }
      if (!compatible) {
        close_group();
      }
      if (eligible) {
        group.push_back(nodes[id]);
        group_bits.push_back(loop_bit);
      }
      ancestors[id * words + loop_bit / 64] |= uint64_t{1} << (loop_bit % 64);
      ++loop_bit;
    }
    for (const auto sink : successors[id]) {
      for (size_t word = 0; word < words; ++word) {
        ancestors[sink * words + word] |= ancestors[id * words + word];
      }
      if (--indegree[sink] == 0) {
        ready.push(sink);
      }
    }
  }
  if (visited != nodes.size()) {
    return created;  // retain boundaries when opaque ports prevent a word DAG
  }
  close_group();
  // Groups occupy disjoint intervals of the loop order and are antichains.
  // Contracting them cannot create a new cycle between fused calls.
  for (const auto& members : groups) {
    created.push_back(fuse_group(graph, members));
  }
  return created;
}
}  // namespace livehd::sim
