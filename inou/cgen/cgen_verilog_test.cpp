// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_verilog.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace {
namespace gu = livehd::graph_util;

TEST(CgenVerilog, StructuralWiringUsesPlainVerilogAndExplicitWidths) {
  const std::string name = "structural_wiring";
  auto&             lib  = livehd::Hhds_graph_library::instance("lgdb_" + name);
  auto              io   = lib.create_io(name);
  io->add_input("s", 1);
  io->add_input("u", 2);
  io->set_bits("s", 3);
  io->set_bits("u", 3);
  io->set_unsign("u", true);
  int port = 3;
  for (const char* output : {"sx", "ux", "slice", "joined", "negative"}) {
    io->add_output(output, port++);
    io->set_bits(output, 8);
    io->set_unsign(output, true);
  }
  auto graph = io->create_graph();
  gu::set_sbits(graph->get_input_pin("s"), 3);
  gu::set_ubits(graph->get_input_pin("u"), 3);
  auto sx = gu::create_typed_node(*graph, Ntype_op::Sext, 8);
  graph->get_input_pin("s").connect_sink(gu::setup_sink_pid(sx, 0));
  gu::create_const(*graph, *Dlop::create_integer(3)).connect_sink(gu::setup_sink_pid(sx, 1));
  sx.create_driver_pin(0).connect_sink(graph->get_output_pin("sx"));
  for (const auto& [source, output, low] : {
           std::tuple{"u",    "ux", 0},
           std::tuple{"s", "slice", 2}
  }) {
    auto mask = gu::create_typed_node(*graph, Ntype_op::Get_mask, 8);
    graph->get_input_pin(source).connect_sink(gu::setup_sink_pid(mask, 0));
    gu::connect_bit_range(mask, low, low + 8);
    gu::set_ubits(mask.create_driver_pin(0), 8);
    mask.create_driver_pin(0).connect_sink(graph->get_output_pin(output));
  }
  auto concat = gu::create_typed_node(*graph, Ntype_op::Concat, 8);
  graph->get_input_pin("s").connect_sink(gu::setup_sink_pid(concat, 0));
  gu::create_const(*graph, *Dlop::create_integer(4)).connect_sink(gu::setup_sink_pid(concat, 1));
  graph->get_input_pin("u").connect_sink(gu::setup_sink_pid(concat, 2));
  gu::create_const(*graph, *Dlop::create_integer(4)).connect_sink(gu::setup_sink_pid(concat, 3));
  gu::set_ubits(concat.create_driver_pin(0), 8);
  concat.create_driver_pin(0).connect_sink(graph->get_output_pin("joined"));
  gu::create_const(*graph, *Dlop::create_integer(-6)).connect_sink(graph->get_output_pin("negative"));
  std::filesystem::create_directories(name);
  Cgen_verilog{name}.do_from_graph(graph);
  livehd::Hhds_graph_library::save("lgdb_" + name);
  std::ifstream     input(name + "/" + name + ".v");
  const std::string verilog{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  for (const char* syntax : {"always", " reg ", "signed", "$signed", "$unsigned", "'sb", "'sh"}) {
    EXPECT_EQ(verilog.find(syntax), std::string::npos) << syntax << "\n" << verilog;
  }
  EXPECT_NE(verilog.find("assign "), std::string::npos);
}

// STA does not apply Verilog's implicit extension at Liberty instance ports.
TEST(CgenVerilog, StructuralInstanceInputsExtendToDeclaredWidth) {
  const std::string name = "structural_port_width";
  auto&             lib  = livehd::Hhds_graph_library::instance("lgdb_" + name);
  auto              cell = lib.create_io("wide_cell");
  cell->add_input("A", 1);
  cell->set_bits("A", 8);
  cell->set_unsign("A", true);
  cell->add_output("Y", 2);
  cell->set_bits("Y", 1);
  auto io = lib.create_io(name);
  io->add_input("u", 1);
  io->set_bits("u", 3);
  io->set_unsign("u", true);
  io->add_output("y", 2);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto input = graph->get_input_pin("u");
  gu::set_ubits(input, 3);
  auto instance = gu::create_typed_node(*graph, Ntype_op::Sub);
  instance.set_subnode(cell);
  input.connect_sink(instance.create_sink_pin("A"));
  auto output = instance.create_driver_pin("Y");
  gu::set_ubits(output, 1);
  output.connect_sink(graph->get_output_pin("y"));
  std::filesystem::create_directories(name);
  Cgen_verilog{name}.do_from_graph(graph);
  std::ifstream     file(name + "/" + name + ".v");
  const std::string verilog{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  EXPECT_NE(verilog.find(".A({5'b0,u})"), std::string::npos) << verilog;
  EXPECT_EQ(verilog.find(".A(u)"), std::string::npos) << verilog;
}

// A wiring node named after the output port it drives must not alias the port
// when its own width differs, and a graph-IO pin without a `bits` attr is sliced
// with its GraphIO width.
TEST(CgenVerilog, StructuralWiringNamedDriverAndAttrlessIoWidth) {
  const std::string name = "structural_named";
  auto&             lib  = livehd::Hhds_graph_library::instance("lgdb_" + name);
  auto              io   = lib.create_io(name);
  io->add_input("a", 1);
  io->set_bits("a", 4);
  io->set_unsign("a", true);
  io->add_output("o", 2);
  io->set_bits("o", 8);
  io->set_unsign("o", true);
  auto graph = io->create_graph();
  auto mask  = gu::create_typed_node(*graph, Ntype_op::Get_mask, 3);
  graph->get_input_pin("a").connect_sink(gu::setup_sink_pid(mask, 0));
  gu::connect_bit_range(mask, 1, 4);
  gu::set_ubits(mask.create_driver_pin(0), 3);
  gu::set_pin_name(mask.create_driver_pin(0), "o");
  mask.create_driver_pin(0).connect_sink(graph->get_output_pin("o"));
  std::filesystem::create_directories(name);
  Cgen_verilog{name}.do_from_graph(graph);
  livehd::Hhds_graph_library::save("lgdb_" + name);
  std::ifstream     input(name + "/" + name + ".v");
  const std::string verilog{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  size_t            assigns = 0;
  for (auto pos = verilog.find("assign o "); pos != std::string::npos; pos = verilog.find("assign o ", pos + 1)) {
    ++assigns;
  }
  EXPECT_EQ(assigns, 1u) << verilog;
  EXPECT_NE(verilog.find("wire [2:0] o_"), std::string::npos) << verilog;
  // Bits 1..3 of the 4-bit input, not a 1-bit view of it.
  EXPECT_NE(verilog.find("a[3]"), std::string::npos) << verilog;
}

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
      livehd::graph_util::connect_bit_range(b, 0, 8);
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

// A chain of two-reader bitwise cells: or_k = (or_{k-1} & y) | x also drives
// output o_k, the shape a rolled loop's runtime-position bit writes produce
// (each write's Or feeds the next write's shift and its low-part And). Every Or
// is inlined at both readers, so without a bound on duplicated text each o_k
// re-spelled the whole chain below it and the module grew quadratically.
TEST(CgenVerilog, DuplicatedInlineChainStaysLinear) {
  const auto emit = [](int k) {
    const std::string name = "dup_chain_" + std::to_string(k);
    auto&             lib  = livehd::Hhds_graph_library::instance("lgdb_" + name);
    auto              io   = lib.create_io(name);
    for (const char* input : {"x", "y"}) {
      io->add_input(input, input[0] == 'x' ? 1 : 2);
      io->set_bits(input, 8);
      io->set_unsign(input, true);
    }
    for (int i = 0; i < k; ++i) {
      const auto output = "o" + std::to_string(i);
      io->add_output(output, 3 + i);
      io->set_bits(output, 8);
      io->set_unsign(output, true);
    }
    auto graph = io->create_graph();
    gu::set_ubits(graph->get_input_pin("x"), 8);
    gu::set_ubits(graph->get_input_pin("y"), 8);
    auto prev = graph->get_input_pin("x");
    for (int i = 0; i < k; ++i) {
      auto low = gu::create_typed_node(*graph, Ntype_op::And);
      gu::setup_sink_pid(low, 0).connect_driver(prev);
      gu::setup_sink_pid(low, 0).connect_driver(graph->get_input_pin("y"));
      gu::set_ubits(low.create_driver_pin(0), 8);
      auto merged = gu::create_typed_node(*graph, Ntype_op::Or);
      gu::setup_sink_pid(merged, 0).connect_driver(low.create_driver_pin(0));
      gu::setup_sink_pid(merged, 0).connect_driver(graph->get_input_pin("x"));
      gu::set_ubits(merged.create_driver_pin(0), 8);
      merged.create_driver_pin(0).connect_sink(graph->get_output_pin("o" + std::to_string(i)));
      prev = merged.create_driver_pin(0);
    }
    std::filesystem::create_directories(name);
    Cgen_verilog{name}.do_from_graph(graph);
    std::ifstream input(name + "/" + name + ".v");
    return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  };
  const auto small = emit(100);
  const auto large = emit(400);
  EXPECT_NE(large.find("endmodule"), std::string::npos);
  EXPECT_EQ(large.find("cgen-miss"), std::string::npos);
  // 4x the cells: linear text grows ~4x, the quadratic re-spelling ~16x.
  EXPECT_LT(large.size(), small.size() * 6) << "small=" << small.size() << " large=" << large.size();
}

// A memory whose read ports DISAGREE on timing has no scalar `type`, and this
// emitter picks its memory model from exactly that field. Before the guard, the
// mixed sentinel matched neither admitted type and fell through to the ARRAY
// path: a stateless combinational lookup, with the synchronous read registers
// gone and the write folded into an always_comb. Wrong Verilog, emitted
// silently.
//
// There is no RTL route to this shape -- the reader's script runs
// `memory -nomap -nordff`, so a synchronous read port is never formed -- so the
// graph is built by hand. That is the only way the guard is ever executed.
TEST(CgenVerilog, MixedReadTimingMemoryIsRefused) {
  const std::string name = "mixed_read_timing_mem";
  auto&             lib  = livehd::Hhds_graph_library::instance("lgdb_" + name);
  auto              io   = lib.create_io(name);
  int               port = 0;
  for (const char* n : {"clk", "we"}) {
    io->add_input(n, port++);
    io->set_bits(n, 1);
    io->set_unsign(n, true);
  }
  for (const char* n : {"wa", "ra0", "ra1"}) {
    io->add_input(n, port++);
    io->set_bits(n, 2);
    io->set_unsign(n, true);
  }
  io->add_input("din", port++);
  io->set_bits("din", 8);
  io->set_unsign("din", true);
  for (const char* n : {"dout0", "dout1"}) {
    io->add_output(n, port++);
    io->set_bits(n, 8);
    io->set_unsign(n, true);
  }
  auto      graph  = io->create_graph();
  auto      mem    = gu::create_typed_node(*graph, Ntype_op::Memory);
  const int stride = static_cast<int>(Ntype::Memory_port_stride);
  auto      konst  = [&](int64_t v) { return gu::create_const(*graph, *Dlop::create_integer(v)); };
  auto      pin    = [&](const char* n, int idx) {
    return mem.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, n) + idx * stride);
  };
  konst(8).connect_sink(pin("bits", 0));
  konst(4).connect_sink(pin("size", 0));
  konst(0).connect_sink(pin("fwd", 0));
  konst(1).connect_sink(pin("wensize", 0));
  konst(1).connect_sink(pin("posclk", 0));
  konst(Ntype::Memory_type_mixed).connect_sink(pin("type", 0));
  // write port 0
  graph->get_input_pin("wa").connect_sink(pin("addr", 0));
  graph->get_input_pin("din").connect_sink(pin("din", 0));
  graph->get_input_pin("we").connect_sink(pin("enable", 0));
  graph->get_input_pin("clk").connect_sink(pin("clock_pin", 0));
  konst(Ntype::Memory_rdport_write).connect_sink(pin("rdport", 0));
  // read port 1 SYNCHRONOUS, read port 2 ASYNCHRONOUS -- the disagreement
  const int64_t timing[] = {Ntype::Memory_rdport_sync, Ntype::Memory_rdport_async};
  for (int r = 0; r < 2; ++r) {
    graph->get_input_pin(r == 0 ? "ra0" : "ra1").connect_sink(pin("addr", 1 + r));
    konst(1).connect_sink(pin("enable", 1 + r));
    graph->get_input_pin("clk").connect_sink(pin("clock_pin", 1 + r));
    konst(timing[r]).connect_sink(pin("rdport", 1 + r));
    auto dout = mem.create_driver_pin(1 + r);
    gu::set_ubits(dout, 8);
    dout.connect_sink(graph->get_output_pin(r == 0 ? "dout0" : "dout1"));
  }
  std::filesystem::create_directories(name);
  EXPECT_THROW(Cgen_verilog{name}.do_from_graph(graph), std::runtime_error)
      << "a mixed-timing memory must be refused, not emitted as a combinational array";
}

}  // namespace
