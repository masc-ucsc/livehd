// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "width_validation.hpp"

#include "diag.hpp"
#include "node_util.hpp"

namespace livehd::graph_util {
void require_sized_design(hhds::Graph& graph, std::string_view consumer) {
  for (const auto& definition : graph.definitions().graphs()) {
    const auto check = [&](const hhds::Pin_class& driver) {
      if (driver.is_invalid() || driver.is_const()) {
        return;
      }
      auto width = bits_of(driver);
      if (is_graph_input_pin(driver) && width == 0) {
        width = definition->get_io()->get_bits(driver.get_pin_name());
      }
      if (width > 0) {
        return;
      }
      const auto producer = is_graph_input_pin(driver) ? std::string{"input "} + std::string{driver.get_pin_name()}
                                                       : debug_name(driver.get_master_node());
      livehd::diag::err(consumer, "graph-width-unresolved", "internal")
          .msg("{} cannot consume definition '{}': {} driver pin {} has unresolved width {}",
               consumer,
               definition->get_name(),
               producer,
               driver.get_port_id(),
               width)
          .hint("the producing pass must install width/sign or run bitwidth before handing the graph to a consumer")
          .fatal();
    };
    for (auto node : definition->body().nodes()) {
      // Check actual outputs, including each Sub/Memory output (never invent
      // pin 0 on a multi-output cell), and primary inputs used by the body.
      for (const auto driver : node.out_sorted_pins()) {
        check(driver);
      }
      for (const auto sink : node.inp_sorted_pins()) {
        for (const auto& driver : sink.get_driver_pins()) {
          check(driver);
        }
      }
    }
    for (const auto& port : definition->get_io()->get_output_pin_decls()) {
      for (const auto& driver : definition->get_output_pin(port.name).get_driver_pins()) {
        check(driver);
      }
    }
  }
}
}  // namespace livehd::graph_util
