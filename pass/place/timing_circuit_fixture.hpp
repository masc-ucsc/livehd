// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "pass/liberty/timing_model.hpp"

namespace livehd::placement::test_fixture {
// Flat mapped netlist for native timing tests. Each master's graph interface
// is created once from its Liberty pins, in Liberty pin order.
struct Timing_circuit {
  hhds::GraphLibrary                              owner;
  std::shared_ptr<const liberty::timing::Library> library;
  std::shared_ptr<hhds::Graph>                    graph;
  Timing_circuit(std::string_view liberty, const std::string& top, std::initializer_list<std::string> inputs,
                 std::initializer_list<std::string> outputs)
      : library(std::make_shared<liberty::timing::Library>(liberty::timing::parse(liberty))) {
    auto io = owner.create_io(top);
    for (const auto& name : inputs) {
      io->add_input(name, io->get_input_pin_decls().size());
      io->set_bits(name, 1);
    }
    for (const auto& name : outputs) {
      io->add_output(name, io->get_output_pin_decls().size());
      io->set_bits(name, 1);
    }
    graph = io->create_graph();
  }
  hhds::Node_class cell(const std::string& instance, const std::string& master) {
    auto io = owner.find_io(master);
    if (!io) {
      io = owner.create_io(master);
      for (const auto& [name, pin] : library->cells.at(master).pins) {
        if (pin.direction == "input") {
          io->add_input(name, io->get_input_pin_decls().size());
        } else {
          io->add_output(name, io->get_output_pin_decls().size());
        }
        io->set_bits(name, 1);
      }
    }
    auto node = livehd::graph_util::create_typed_node(*graph, Ntype_op::Sub);
    node.set_name(instance);
    node.set_subnode(io);
    return node;
  }
};
}  // namespace livehd::placement::test_fixture
