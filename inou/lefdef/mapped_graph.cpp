// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "mapped_graph.hpp"

#include <map>
#include <stdexcept>
#include <vector>

#include "node_util.hpp"

namespace livehd::physical {
std::shared_ptr<hhds::Graph> mapped_graph(hhds::GraphLibrary& library, const Floorplan& fp, const Technology& tech) {
  // One-bit signal pins; other LEF/DEF directions are not imported.
  const auto declare = [](const auto& io, const std::string& name, const std::string& direction) {
    if (direction == "INPUT") {
      io->add_input(name, io->get_input_pin_decls().size());
    } else if (direction == "OUTPUT") {
      io->add_output(name, io->get_output_pin_decls().size());
    } else {
      return false;
    }
    io->set_bits(name, 1);
    return true;
  };
  auto                        io = library.create_io(fp.design);
  std::map<std::string, bool> input_ports;
  for (const auto& port : fp.terminals) {
    if (declare(io, port.name, port.direction)) {
      input_ports.emplace(port.name, port.direction == "INPUT");
    }
  }
  auto                                    graph = io->create_graph();
  std::map<std::string, hhds::Node_class> nodes;
  for (const auto& cell : fp.components) {
    auto master = library.find_io(cell.macro);
    if (!master) {
      master = library.create_io(cell.macro);
      for (const auto& [name, pin] : tech.macros.at(cell.macro).pins) {
        declare(master, name, pin.direction);
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
  // DEF is also the authority for deliberately unused inputs (for example a
  // macro's write-mask bus). Record that intent separately from connectivity;
  // subsequent timing must still reject any originally connected input lost
  // during graph or timer construction.
  for (const auto& [name, node] : nodes) {
    std::vector<std::string> open;
    for (const auto& declaration : node.get_subnode_io()->get_input_pin_decls()) {
      const auto pin = node.try_get_sink_pin(declaration.port_id);
      if (pin.is_invalid() || pin.get_driver_pins().empty()) {
        open.push_back(declaration.name);
      }
    }
    if (!open.empty()) {
      node.attr(livehd::attrs::physical_open_inputs).set(std::move(open));
    }
  }
  return graph;
}
}  // namespace livehd::physical
