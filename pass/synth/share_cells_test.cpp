// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// share_cells: duplicate combinational cells share one, on graphs built by hand.
#include "share_cells.hpp"

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace {
namespace gu = livehd::graph_util;
using livehd::synth::share_duplicate_cells;

std::shared_ptr<hhds::Graph> make_graph(const char* lib_name) {
  auto& lib = livehd::Hhds_graph_library::instance(lib_name);
  auto  io  = lib.create_io("top");
  int   id  = 1;
  for (auto name : {"a", "b", "c"}) {
    io->add_input(name, id++);
    io->set_bits(name, 8);
  }
  io->add_output("o", id++);
  io->add_output("p", id++);
  auto graph = io->create_graph();
  for (auto name : {"a", "b", "c"}) {
    gu::set_ubits(graph->get_input_pin(name), 8);
  }
  return graph;
}

hhds::Pin_class cell(hhds::Graph& g, Ntype_op op, int bits, std::initializer_list<hhds::Pin_class> inputs, int color = 0) {
  auto node = gu::create_typed_node(g, op);
  for (const auto& in : inputs) {
    gu::setup_sink_pid(node, 0).connect_driver(in);
  }
  if (color != 0) {
    gu::set_color(node, color);
  }
  auto out = node.create_driver_pin(0);
  gu::set_ubits(out, bits);
  return out;
}

size_t cells(hhds::Graph& g, Ntype_op op) {
  size_t n = 0;
  for (auto node : g.body().nodes()) {
    n += gu::type_op_of(node) == op ? 1 : 0;
  }
  return n;
}

TEST(ShareCells, TwinsShareOneWhateverTheOperandOrder) {
  auto g = make_graph("lgdb_share_twins");
  auto a = g->get_input_pin("a");
  auto b = g->get_input_pin("b");
  cell(*g, Ntype_op::And, 8, {a, b}).connect_sink(g->get_output_pin("o"));
  cell(*g, Ntype_op::And, 8, {b, a}).connect_sink(g->get_output_pin("p"));
  const auto stats = share_duplicate_cells(g.get());
  EXPECT_EQ(stats.cells, 1U);
  EXPECT_EQ(cells(*g, Ntype_op::And), 1U);
  EXPECT_EQ(g->get_output_pin("o").get_driver_pin(), g->get_output_pin("p").get_driver_pin());
}

TEST(ShareCells, ASharedCellSharesWhatIsBuiltOnIt) {
  auto g = make_graph("lgdb_share_cascade");
  auto a = g->get_input_pin("a");
  auto b = g->get_input_pin("b");
  auto c = g->get_input_pin("c");
  cell(*g, Ntype_op::Or, 8, {cell(*g, Ntype_op::And, 8, {a, b}), c}).connect_sink(g->get_output_pin("o"));
  cell(*g, Ntype_op::Or, 8, {c, cell(*g, Ntype_op::And, 8, {a, b})}).connect_sink(g->get_output_pin("p"));
  const auto stats = share_duplicate_cells(g.get());
  EXPECT_EQ(stats.cells, 2U);
  EXPECT_EQ(cells(*g, Ntype_op::And), 1U);
  EXPECT_EQ(cells(*g, Ntype_op::Or), 1U);
}

TEST(ShareCells, AnotherWidthOrAnotherOperandIsAnotherCell) {
  auto g = make_graph("lgdb_share_distinct");
  auto a = g->get_input_pin("a");
  auto b = g->get_input_pin("b");
  auto c = g->get_input_pin("c");
  auto x = cell(*g, Ntype_op::And, 8, {a, b});
  auto y = cell(*g, Ntype_op::And, 4, {a, b});  // narrower result
  auto z = cell(*g, Ntype_op::And, 8, {a, c});  // another operand
  cell(*g, Ntype_op::Xor, 8, {x, y, z}).connect_sink(g->get_output_pin("o"));
  EXPECT_EQ(share_duplicate_cells(g.get()).cells, 0U);
  EXPECT_EQ(cells(*g, Ntype_op::And), 3U);
}

// Color 2 already feeds color 1. A color-2 twin of a color-1 cell would then be read from color 1 by color 2: the two
// colors would depend on each other, so the twin stays.
TEST(ShareCells, ASharedCellNeverLoopsTwoColors) {
  auto g = make_graph("lgdb_share_loop");
  auto a = g->get_input_pin("a");
  auto b = g->get_input_pin("b");
  auto c = g->get_input_pin("c");
  auto first  = cell(*g, Ntype_op::And, 8, {a, b}, 1);
  auto second = cell(*g, Ntype_op::And, 8, {a, b}, 2);
  auto in2    = cell(*g, Ntype_op::Or, 8, {second, c}, 2);   // color 2 reads its own twin
  cell(*g, Ntype_op::Xor, 8, {first, in2}, 1).connect_sink(g->get_output_pin("o"));  // color 2 -> color 1
  const auto stats = share_duplicate_cells(g.get());
  EXPECT_EQ(stats.cells, 0U);
  EXPECT_EQ(stats.refused, 1U);
  EXPECT_EQ(cells(*g, Ntype_op::And), 2U);
}

// Without that dependency the twin goes, and the stat says the kept cell is in another color.
TEST(ShareCells, TwinsInTwoIndependentColorsShare) {
  auto g = make_graph("lgdb_share_colors");
  auto a = g->get_input_pin("a");
  auto b = g->get_input_pin("b");
  cell(*g, Ntype_op::And, 8, {a, b}, 1).connect_sink(g->get_output_pin("o"));
  cell(*g, Ntype_op::Not, 8, {cell(*g, Ntype_op::And, 8, {a, b}, 2)}, 2).connect_sink(g->get_output_pin("p"));
  const auto stats = share_duplicate_cells(g.get());
  EXPECT_EQ(stats.cells, 1U);
  EXPECT_EQ(stats.cross_color, 1U);
}
}  // namespace
