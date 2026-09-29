// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_verilog.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace {
namespace gu = livehd::graph_util;

// The companion CLI check proves this emitted module against a hand-written
// all-equal reference. Include every operand order and mixed signed widths:
// (a == b) == c is neither all-equal nor permutation invariant.
TEST(CgenVerilog, VariadicEqualityIncludesEveryOperand) {
  const std::string name = "eq_variadic";
  auto&             lib  = livehd::Hhds_graph_library::instance("lgdb_" + name);
  auto              io   = lib.create_io(name);
  for (int i = 0; i < 3; ++i) {
    const auto input = std::string(1, 'a' + i);
    io->add_input(input, i + 1);
    io->set_bits(input, i == 1 ? 128 : i == 2 ? 3 : 2);
    io->set_unsign(input, i == 1);
  }
  for (int i = 0; i < 9; ++i) {
    auto output = "eq" + std::to_string(i);
    io->add_output(output, i + 4);
    io->set_bits(output, 1);
    io->set_unsign(output, true);
  }
  auto graph = io->create_graph();
  gu::set_sbits(graph->get_input_pin("a"), 2);
  gu::set_ubits(graph->get_input_pin("b"), 128);
  gu::set_sbits(graph->get_input_pin("c"), 3);
  std::array<int, 3> order{0, 1, 2};
  int                output = 0;
  do {
    auto eq = gu::create_typed_node(*graph, Ntype_op::EQ);
    for (auto i : order) {
      gu::setup_sink_pid(eq, 0).connect_driver(graph->get_input_pin(std::string(1, 'a' + i)));
    }
    gu::set_ubits(eq.create_driver_pin(0), 1);
    eq.create_driver_pin(0).connect_sink(graph->get_output_pin("eq" + std::to_string(output++)));
  } while (std::next_permutation(order.begin(), order.end()));
  for (auto op : {Ntype_op::LT, Ntype_op::GT, Ntype_op::EQ}) {
    auto comparison = gu::create_typed_node(*graph, op);
    gu::setup_sink_pid(comparison, 0).connect_driver(graph->get_input_pin("a"));
    gu::setup_sink_pid(comparison, op == Ntype_op::EQ ? 0 : 1).connect_driver(graph->get_input_pin("b"));
    if (op != Ntype_op::EQ) {
      gu::setup_sink_pid(comparison, 0).connect_driver(graph->get_input_pin("c"));
    }
    gu::set_ubits(comparison.create_driver_pin(0), 1);
    comparison.create_driver_pin(0).connect_sink(graph->get_output_pin("eq" + std::to_string(output++)));
  }
  std::filesystem::create_directories(name);
  Cgen_verilog{name}.do_from_graph(graph);
  livehd::Hhds_graph_library::save("lgdb_" + name);
  EXPECT_TRUE(std::filesystem::exists(name + "/" + name + ".v"));
}

// Named nodes and identity masks both bypass ordinary temporary declarations.
// The cycle must survive as nets rather than recursively expanding expressions.
TEST(CgenVerilog, CyclicExpressionsRetainNets) {
  for (int variant : {0, 1, 2}) {
    const bool        identity_mask = variant == 1;
    const bool        shifts        = variant == 2;
    const std::string name          = identity_mask ? "cycle_mask" : shifts ? "cycle_shift" : "cycle_named";
    auto&             lib           = livehd::Hhds_graph_library::instance("lgdb_" + name);
    auto              io            = lib.create_io(name);
    io->add_output("out", 0);
    io->set_bits("out", 8);
    auto graph = io->create_graph();
    auto a     = gu::create_typed_node(*graph, shifts ? Ntype_op::SRA : Ntype_op::Not);
    auto b     = gu::create_typed_node(*graph, identity_mask ? Ntype_op::Get_mask : shifts ? Ntype_op::SRA : Ntype_op::Not);
    a.set_name("feedback_a");
    b.set_name("feedback_b");
    auto ap = a.create_driver_pin(0);
    auto bp = b.create_driver_pin(0);
    gu::set_bits(ap, 8);
    gu::set_bits(bp, 8);
    gu::set_unsign(ap);
    gu::set_unsign(bp);
    ap.connect_sink(b.create_sink_pin(0));
    bp.connect_sink(a.create_sink_pin(0));
    bp.connect_sink(graph->get_output_pin("out"));
    if (identity_mask) {
      gu::create_const(*graph, *Dlop::create_integer(255)).connect_sink(gu::setup_sink_by_name(b, "mask"));
    }
    if (shifts) {
      auto amount = gu::create_const(*graph, *Dlop::create_integer(1));
      amount.connect_sink(a.create_sink_pin(1));
      amount.connect_sink(b.create_sink_pin(1));
    }
    std::filesystem::create_directories(name);
    Cgen_verilog emitter(name);
    emitter.do_from_graph(graph);
    std::ifstream     input(name + "/" + name + ".v");
    const std::string verilog{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EXPECT_NE(verilog.find("endmodule"), std::string::npos);
    EXPECT_EQ(verilog.find("cgen-miss"), std::string::npos);
    EXPECT_NE(verilog.find("feedback_a"), std::string::npos);
    EXPECT_NE(verilog.find("feedback_b"), std::string::npos);
    EXPECT_LT(verilog.size(), 4096);
  }
}
}  // namespace
