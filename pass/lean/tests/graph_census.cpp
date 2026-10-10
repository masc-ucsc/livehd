// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Inspect original graph operators before the Lean adapter lowers them.
#include <iostream>
#include <string>

#include "graph_access.hpp"
#include "graph_library_singleton.hpp"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: lean_graph_census LGDB TOP\n";
    return 2;
  }
  try {
    using namespace lean_pass;
    auto&      library = livehd::Hhds_graph_library::instance(argv[1]);
    const auto io      = library.find_io(argv[2]);
    if (!io || !io->get_graph()) {
      throw std::runtime_error("graph not found");
    }
    const auto graph = io->get_graph();
    std::cout << "schema\tlean_graph_census_v1\n";
    for (const auto node : graph->body().nodes()) {
      const auto op = node_op(node);
      std::cout << "node\t" << node_id(node) << '\t' << Ntype::get_name(op) << '\t' << raw_node_width(node) << '\t'
                << node_output_is_signed(node) << '\n';
      if (op == Ntype_op::Hotmux) {
        using namespace livehd::graph_util;
        // Upstream stores runtime_check for BOTH deferred and refuted checks.
        // A refutation must be read from compile diagnostics, never inferred
        // to be deferred merely because this graph attribute is present.
        const auto status = runtime_check_of(node) == kFormalOnehot ? "runtime-check"
                            : proven_of(node) == kFormalOnehot      ? "native-proved"
                                                                    : "unreported";
        std::cout << "onehot\t" << node_id(node) << '\t' << status << '\n';
      }
      for (const auto& edge : inp_edges_ordered(node)) {
        const auto  pin  = edge.driver;
        std::string kind = "node", value = "-";
        uint32_t    intrinsic = 0;
        if (pin_is_const(pin)) {
          kind         = "constant";
          const auto c = pin_const_value(pin);
          intrinsic    = intrinsic_const_width(c);
          // Initialization images can contain millions of bits. Record their
          // width; small policy constants retain their complete value.
          value        = c.has_unknowns() ? "unknown" : intrinsic > 256 ? "wide" : c.to_decimal_string();
        } else if (pin_is_input(pin)) {
          kind = "input";
        }
        std::cout << "operand\t" << node_id(node) << '\t' << edge.sink.get_port_id() << '\t'
                  << Ntype::sink_bank(op, edge.sink.get_port_id()) << '\t' << raw_pin_width(pin) << '\t'
                  << !livehd::graph_util::is_unsign(pin) << '\t' << kind << '\t' << intrinsic << '\t' << value << '\n';
        if (op == Ntype_op::Memory) {
          std::cout << "memory\t" << node_id(node) << '\t' << sink_pin_name(edge) << '\t' << kind << '\t' << raw_pin_width(pin)
                    << '\t' << value << '\n';
        }
      }
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
