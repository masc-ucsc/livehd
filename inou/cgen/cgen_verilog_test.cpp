// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_verilog.hpp"

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
