// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_lg2lnast.hpp"

#include "gtest/gtest.h"
#include "lnast.hpp"
#include "node_util.hpp"

TEST(GraphToLnast, BooleanConstantUsedAsConcatWidth) {
  namespace gu = livehd::graph_util;
  hhds::GraphLibrary library;
  const auto         io = library.create_io("join_bits");
  io->add_input("high", 0);
  io->add_input("low", 1);
  io->add_output("result", 2);
  io->set_bits("high", 1);
  io->set_bits("low", 3);
  io->set_bits("result", 4);
  const auto graph  = io->create_graph();
  const auto concat = gu::create_typed_node(*graph, Ntype_op::Concat, 4);
  graph->get_input_pin("high").connect_sink(concat.create_sink_pin(0));
  gu::create_const(*graph, *Dlop::create_bool(true)).connect_sink(concat.create_sink_pin(1));
  graph->get_input_pin("low").connect_sink(concat.create_sink_pin(2));
  gu::create_const(*graph, *Dlop::create_integer(3)).connect_sink(concat.create_sink_pin(3));
  concat.create_driver_pin(0).connect_sink(graph->get_output_pin("result"));

  std::vector<std::shared_ptr<Lnast>> units;
  ASSERT_TRUE(Pass_lg2lnast::lower({graph}, "", units));
  ASSERT_EQ(units.size(), 1u);
  const auto& tree = *units.front();
  for (const auto node : tree.depth_preorder()) {
    if (tree.get_type(node) == Lnast_ntype::Lnast_ntype_concat) {
      std::vector<std::string> operands;
      for (const auto child : tree.children(node)) {
        operands.emplace_back(tree.get_name(child));
      }
      ASSERT_EQ(operands.size(), 5u);
      EXPECT_EQ(operands[2], "1");
      EXPECT_EQ(operands[4], "3");
      return;
    }
  }
  FAIL() << "missing concat in lowered graph";
}

TEST(GraphToLnast, MultiplyThreeFactors) {
  namespace gu = livehd::graph_util;
  hhds::GraphLibrary library;
  const auto         io = library.create_io("product");
  io->add_input("a", 0);
  io->add_input("b", 1);
  io->add_input("c", 2);
  io->add_output("p", 3);
  for (auto name : {"a", "b", "c"}) {
    io->set_bits(name, 4);
  }
  io->set_bits("p", 12);
  const auto graph   = io->create_graph();
  const auto product = gu::create_typed_node(*graph, Ntype_op::Mult, 12);
  int        i       = 0;
  for (auto name : {"a", "b", "c"}) {
    graph->get_input_pin(name).connect_sink(product.create_sink_pin(i++));
  }
  product.create_driver_pin(0).connect_sink(graph->get_output_pin("p"));
  std::vector<std::shared_ptr<Lnast>> units;
  ASSERT_TRUE(Pass_lg2lnast::lower({graph}, "", units));
  ASSERT_EQ(units.size(), 1u);
  int multiplies = 0;
  for (const auto node : units.front()->depth_preorder()) {
    if (units.front()->get_type(node) == Lnast_ntype::Lnast_ntype_mult) {
      ++multiplies;
    }
  }
  EXPECT_EQ(multiplies, 2);
}
