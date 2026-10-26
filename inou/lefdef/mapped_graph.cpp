// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "mapped_graph.hpp"

#include <map>
#include <stdexcept>
#include <vector>

#include "node_util.hpp"

namespace livehd::physical {
std::shared_ptr<hhds::Graph> mapped_graph(hhds::GraphLibrary& library, const Floorplan& fp, const Technology& tech) {
  auto                        io = library.create_io(fp.design);
  std::map<std::string, bool> input_ports;
  for (const auto& port : fp.terminals) {
    if (port.direction == "INPUT") {
      io->add_input(port.name, io->get_input_pin_decls().size());
      io->set_bits(port.name, 1);
      input_ports.emplace(port.name, true);
    } else if (port.direction == "OUTPUT") {
      io->add_output(port.name, io->get_output_pin_decls().size());
      io->set_bits(port.name, 1);
      input_ports.emplace(port.name, false);
    }
  }
  auto                                    graph = io->create_graph();
  std::map<std::string, hhds::Node_class> nodes;
  for (const auto& cell : fp.components) {
    auto master = library.find_io(cell.macro);
    if (!master) {
      master = library.create_io(cell.macro);
      for (const auto& [name, pin] : tech.macros.at(cell.macro).pins) {
        if (pin.direction == "INPUT") {
          master->add_input(name, master->get_input_pin_decls().size());
          master->set_bits(name, 1);
        } else if (pin.direction == "OUTPUT") {
          master->add_output(name, master->get_output_pin_decls().size());
          master->set_bits(name, 1);
        }
      }
    }
    auto node = livehd::graph_util::create_typed_node(*graph, Ntype_op::Sub);
    node.set_name(cell.name);
    node.set_subnode(master);
    nodes.emplace(cell.name, node);
  }
  for (const auto& net : fp.nets) {
    hhds::Pin_class              driver;
    std::vector<hhds::Pin_class> sinks;
    for (const auto& pin : net.connections) {
      bool            output = false;
      hhds::Pin_class physical;
      if (pin.instance == "PIN") {
        if (!input_ports.contains(pin.pin)) {
          throw std::runtime_error("unsupported DEF signal port: " + pin.pin);
        }
        if (input_ports.at(pin.pin)) {
          physical = graph->get_input_pin(pin.pin);
          output   = true;
        } else {
          physical = graph->get_output_pin(pin.pin);
        }
      } else {
        auto        node        = nodes.at(pin.instance);
        const auto& declaration = tech.macros.at(std::string(node.get_subnode_io()->get_name())).pins.at(pin.pin);
        output                  = declaration.direction == "OUTPUT";
        if (!output && declaration.direction != "INPUT") {
          throw std::runtime_error("unsupported DEF signal direction: " + pin.instance + ":" + pin.pin);
        }
        physical = output ? node.create_driver_pin(pin.pin) : node.create_sink_pin(pin.pin);
      }
      if (output) {
        livehd::graph_util::set_bits(physical, 1);
        if (!driver.is_invalid()) {
          throw std::runtime_error("multiple DEF signal drivers: " + net.name);
        }
        driver = physical;
      } else {
        sinks.push_back(physical);
      }
    }
    if (driver.is_invalid() && !sinks.empty()) {
      throw std::runtime_error("undriven DEF signal net cannot be imported exactly: " + net.name);
    }
    for (auto sink : sinks) {
      sink.connect_driver(driver);
    }
  }
  return graph;
}
}  // namespace livehd::physical
