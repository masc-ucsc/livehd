// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_sim.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "sim_color_plan.hpp"

namespace {
namespace gu = livehd::graph_util;

std::string emit(const std::shared_ptr<hhds::Graph>& graph, const std::string& name) {
  std::filesystem::create_directories(name);
  const auto plan = livehd::sim::Color_plan::discover(graph.get(), false);
  EXPECT_TRUE(plan.complete()) << plan.report();
  Cgen_sim emitter(name, "", name, "false", &plan);
  emitter.do_from_graph(graph);
  std::string code;
  for (const auto& entry : std::filesystem::directory_iterator(name)) {
    if (entry.path().extension() == ".cpp") {
      std::ifstream input(entry.path());
      EXPECT_TRUE(input.good());
      code.append(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
  }
  return code;
}

TEST(CgenSim, FusesAndReductionAfterTemporaryBindingsExpire) {
  for (int width : {1, 3, 8, 65}) {
    const auto name = "reduce_" + std::to_string(width);
    auto&      lib  = livehd::Hhds_graph_library::instance("lgdb_" + name);
    auto       io   = lib.create_io(name);
    io->add_input("a", 0);
    io->set_bits("a", width);
    io->set_unsign("a", true);
    io->add_output("y", 1);
    io->set_bits("y", 1);
    io->set_unsign("y", true);
    auto graph = io->create_graph();
    auto sx    = gu::create_typed_node(*graph, Ntype_op::Sext);
    graph->get_input_pin("a").connect_sink(gu::setup_sink_by_name(sx, "a"));
    gu::create_const(*graph, *Dlop::create_integer(width)).connect_sink(gu::setup_sink_by_name(sx, "b"));
    auto signed_value = sx.create_driver_pin(0);
    gu::set_sbits(signed_value, width);
    auto eq = gu::create_typed_node(*graph, Ntype_op::EQ);
    signed_value.connect_sink(gu::setup_sink_by_name(eq, "as"));
    gu::create_const(*graph, *Dlop::create_integer(-1)).connect_sink(gu::setup_sink_by_name(eq, "as"));
    auto out = eq.create_driver_pin(0);
    gu::set_ubits(out, 1);
    out.connect_sink(graph->get_output_pin("y"));
    const auto code = emit(graph, name);
    EXPECT_NE(code.find("::rand_op("), std::string::npos);
    EXPECT_EQ(code.find(".sext_op("), std::string::npos);
    EXPECT_EQ(code.find("::eq_op("), std::string::npos);
    EXPECT_EQ(code.find("create_integer(-1)"), std::string::npos);
    EXPECT_EQ(code.find("UNRESOLVED-CYCLE"), std::string::npos);
  }
}

TEST(CgenSim, MaskWritesUseRangesIncludingWholeAndOutsideCarrier) {
  const std::string name = "mask_windows";
  auto&             lib  = livehd::Hhds_graph_library::instance("lgdb_" + name);
  auto              io   = lib.create_io(name);
  io->add_input("a", 0);
  io->add_input("v", 1);
  for (auto field : {"a", "v"}) {
    io->set_bits(field, 8);
    io->set_unsign(field, true);
  }
  for (int i = 0; i < 4; ++i) {
    const auto field = "y" + std::to_string(i);
    io->add_output(field, i + 2);
    io->set_bits(field, 8);
    io->set_unsign(field, true);
  }
  auto graph = io->create_graph();
  int  index = 0;
  for (const auto& mask :
       {gu::mask_whole_const(), gu::mask_window_const(3, 6), gu::mask_window_const(6, 70), gu::mask_window_const(64, 70)}) {
    auto node = gu::create_set_mask(*graph, graph->get_input_pin("a"), gu::create_const(*graph, mask), graph->get_input_pin("v"));
    auto out  = node.create_driver_pin(0);
    gu::set_ubits(out, 8);
    out.connect_sink(graph->get_output_pin("y" + std::to_string(index++)));
  }
  const auto code = emit(graph, name);
  EXPECT_NE(code.find(".set_mask_op_opt("), std::string::npos);
  EXPECT_EQ(code.find(".set_mask_op("), std::string::npos);
  EXPECT_EQ(code.find("UNRESOLVED-CYCLE"), std::string::npos);
}
TEST(CgenSim, EmitsNativeCountedReductions) {
  for (int width : {1, 3, 8, 65, 129}) {
    const auto         name = "native_reduce_" + std::to_string(width);
    hhds::GraphLibrary lib;
    auto               io = lib.create_io(name);
    io->add_input("a", 0);
    io->set_bits("a", width);
    io->set_unsign("a", true);
    auto graph = io->create_graph();
    for (auto op : {Ntype_op::Rxor, Ntype_op::Popcount}) {
      const auto port        = std::string(Ntype::get_name(op));
      const int  result_bits = op == Ntype_op::Rxor ? 1 : std::bit_width(static_cast<unsigned>(width));
      io->add_output(port, op == Ntype_op::Rxor ? 1 : 2);
      io->set_bits(port, result_bits);
      io->set_unsign(port, true);
      auto node = gu::create_typed_node(*graph, op);
      graph->get_input_pin("a").connect_sink(node.create_sink_pin(0));
      gu::create_const(*graph, *Dlop::create_integer(width)).connect_sink(node.create_sink_pin(1));
      auto output = node.create_driver_pin(0);
      gu::set_ubits(output, result_bits);
      output.connect_sink(graph->get_output_pin(port));
    }
    const auto code = emit(graph, name);
    EXPECT_NE(code.find("::rxor_op("), std::string::npos);
    EXPECT_NE(code.find("::popcount_op("), std::string::npos);
    EXPECT_EQ(code.find("UNRESOLVED-CYCLE"), std::string::npos);
  }
}

// clang keeps a constructor's initializer count in an 18-bit field, so a class
// with 2^18+ members gets an implicit constructor that silently skips every
// initializer past the wrapped count (xs_rob: `__in.clock__tick` stayed false
// and no flop ever captured). The flop members of a big module therefore live
// in bounded base structs, each with its own constructor.
TEST(CgenSim, HugeFlopSetSplitsIntoBoundedStateBases) {
  constexpr int     kFlops = 16400;  // Q + _din per flop: just past one 2^15-member chunk
  const std::string name   = "huge_flop_set";
  auto&             lib    = livehd::Hhds_graph_library::instance("lgdb_" + name);
  auto              io     = lib.create_io(name);
  io->add_input("clk", 0);
  io->add_input("d", 1);
  io->set_bits("d", 1);
  io->set_unsign("d", true);
  io->add_output("q", 2);
  io->set_bits("q", 1);
  io->set_unsign("q", true);
  auto graph = io->create_graph();
  auto chain = graph->get_input_pin("d");
  for (int i = 0; i < kFlops; ++i) {
    auto flop = gu::create_typed_node(*graph, Ntype_op::Flop);
    graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
    chain.connect_sink(gu::setup_sink_by_name(flop, "din"));
    chain = flop.create_driver_pin(0);
    gu::set_ubits(chain, 1);
  }
  chain.connect_sink(graph->get_output_pin("q"));
  emit(graph, name);

  std::string header;
  for (const auto& entry : std::filesystem::directory_iterator(name)) {
    if (entry.path().extension() == ".hpp" && entry.path().stem() == name) {
      std::ifstream input(entry.path());
      header.append(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
  }
  ASSERT_FALSE(header.empty());
  EXPECT_NE(header.find("struct huge_flop_set : huge_flop_set__state0, huge_flop_set__state1 {"), std::string::npos);
  EXPECT_NE(header.find("struct huge_flop_set__state1 {"), std::string::npos);
  EXPECT_EQ(header.find("huge_flop_set__state2"), std::string::npos);
}
}  // namespace
