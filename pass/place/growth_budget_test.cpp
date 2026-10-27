// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "growth_budget.hpp"

#include "gtest/gtest.h"
#include "node_util.hpp"

namespace livehd::placement {
namespace {
auto instance(hhds::Graph& graph, const std::shared_ptr<hhds::GraphIO>& io) {
  auto node = graph_util::create_typed_node(graph, Ntype_op::Sub);
  node.set_subnode(io);
  return node;
}
TEST(GrowthBudget, SharedNestedColorsUseTheWholeDesignBudget) {
  physical::Technology tech;
  tech.macros["INV"].klass = "CORE";
  tech.macros["INV"].width = tech.macros["INV"].height = 1;
  hhds::GraphLibrary source, candidate;
  const auto         inv   = source.create_io("INV");
  const auto         color = source.create_io("color")->create_graph();
  for (int i = 0; i < 2; ++i) {
    instance(*color, inv);
  }
  const auto wrapper = source.create_io("wrapper")->create_graph();
  for (int i = 0; i < 2; ++i) {
    instance(*wrapper, color->get_io());
  }
  const auto top = source.create_io("top")->create_graph();
  for (int i = 0; i < 2; ++i) {
    instance(*top, wrapper->get_io());
  }
  for (int i = 0; i < 292; ++i) {
    instance(*top, inv);
  }
  Growth_budget budget{physical_size(*top, tech)};
  ASSERT_EQ(budget.baseline.cells, 300U);
  ASSERT_DOUBLE_EQ(budget.baseline.area_um2, 300);
  const auto replacement     = candidate.create_io("color")->create_graph();
  const auto replacement_inv = candidate.create_io("INV");
  for (int i = 0; i < 4; ++i) {
    instance(*replacement, replacement_inv);
  }
  // Doubling one color is legal when its four occurrences fit the shared 3%.
  auto proposed = physical_size(*top, tech, replacement.get());
  EXPECT_EQ(proposed.cells, 308U);
  EXPECT_TRUE(budget.allows(proposed));
  EXPECT_TRUE(budget.allows(proposed));  // Querying does not spend headroom.
  instance(*replacement, replacement_inv);
  proposed = physical_size(*top, tech, replacement.get());
  EXPECT_EQ(proposed.cells, 312U);
  EXPECT_FALSE(budget.allows(proposed));
  EXPECT_EQ(physical_size(*top, tech).cells, 300U);
}
TEST(GrowthBudget, MasterSwapsRespectAreaAndMacrosCannotSubsidizeGrowth) {
  physical::Technology tech;
  for (const auto* name : {"INV", "BIG"}) {
    tech.macros[name].klass  = "CORE";
    tech.macros[name].width  = std::string_view(name) == "BIG" ? 2 : 1;
    tech.macros[name].height = 1;
  }
  tech.macros["RAM"].klass = "BLOCK";
  tech.macros["RAM"].width = tech.macros["RAM"].height = 1000;
  hhds::GraphLibrary library;
  const auto         inv     = library.create_io("INV");
  const auto         top     = library.create_io("top")->create_graph();
  const auto         changed = instance(*top, inv);
  for (int i = 1; i < 10; ++i) {
    instance(*top, inv);
  }
  instance(*top, library.create_io("RAM"));
  Growth_budget budget{physical_size(*top, tech)};
  EXPECT_EQ(budget.baseline.cells, 10U);
  EXPECT_DOUBLE_EQ(budget.baseline.area_um2, 10);
  const auto candidate = physical_size(*top,
                                       tech,
                                       top.get(),
                                       {
                                           {changed.get_debug_nid(), "BIG"}
  });
  EXPECT_EQ(candidate.cells, 10U);
  EXPECT_DOUBLE_EQ(candidate.area_um2, 11);
  EXPECT_FALSE(budget.allows(candidate));
  EXPECT_EQ(changed.get_subnode_io()->get_name(), "INV");
}
TEST(GrowthBudget, ResetAndRailTreeBuffersAreNotGrowth) {
  physical::Technology tech;
  tech.macros["BUF"].klass = "CORE";
  tech.macros["BUF"].width = tech.macros["BUF"].height = 1;
  hhds::GraphLibrary library;
  const auto         buf = library.create_io("BUF");
  const auto         top = library.create_io("top")->create_graph();
  for (int i = 0; i < 10; ++i) {
    instance(*top, buf);
  }
  const Growth_budget budget{physical_size(*top, tech)};
  // The placement trees (synth.place.reset_fanout/domino_rail_fanout) mint
  // these names, flattened with or without a hierarchy prefix.
  for (const auto* name : {"__place_rsttree0", "u0.__place_rsttree3", "__place_railtree1"}) {
    instance(*top, buf).set_name(name);
  }
  EXPECT_EQ(physical_size(*top, tech).cells, 10U);
  EXPECT_DOUBLE_EQ(physical_size(*top, tech).area_um2, 10);
  instance(*top, buf).set_name("g_place_rsttree");  // any other new cell is growth
  EXPECT_EQ(physical_size(*top, tech).cells, 11U);
  EXPECT_FALSE(budget.allows(physical_size(*top, tech)));
}
}  // namespace
}  // namespace livehd::placement
