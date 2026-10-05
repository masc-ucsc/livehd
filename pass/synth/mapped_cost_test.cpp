// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "mapped_cost.hpp"

#include <limits>

#include "gtest/gtest.h"
#include "node_util.hpp"

namespace livehd::synth {
namespace {
TEST(MappedCost, SharedDefinitionsAreWeightedByPhysicalInstancesAndAbsorbedRowsAreZero) {
  Mapped_design design;
  auto top = design.library.create_io("top"), child = design.library.create_io("child"), leaf = design.library.create_io("leaf");
  design.top     = top->create_graph();
  auto body      = child->create_graph();
  auto leaf_body = leaf->create_graph();
  (void)leaf_body;
  for (unsigned i = 0; i < 2; ++i) {
    graph_util::create_typed_node(*design.top, Ntype_op::Sub).set_subnode(child);
  }
  for (unsigned i = 0; i < 3; ++i) {
    graph_util::create_typed_node(*body, Ntype_op::Sub).set_subnode(leaf);
  }
  for (const auto& [name, gates] : {
           std::pair{     "top",   1},
           std::pair{   "child",   4},
           std::pair{    "leaf",   7},
           std::pair{"absorbed", 100}
  }) {
    Region_qor row;
    row.module = name;
    row.gates  = gates;
    row.area   = gates * 0.5;
    row.delay  = 5;
    design.regions.push_back(std::move(row));
  }
  auto cost = mapped_cost(design);
  ASSERT_TRUE(cost);
  EXPECT_EQ(cost->gates, 51U);
  EXPECT_DOUBLE_EQ(cost->area, 25.5);
  EXPECT_DOUBLE_EQ(cost->maximum_region_delay, 5);
  EXPECT_FALSE(mapped_cost(design, [] { return false; }));
}
TEST(MappedCost, InstanceCountOverflowNeverProducesAnUnderestimatedGateCount) {
  Mapped_design design;
  auto          child = design.library.create_io("leaf");
  auto          body  = child->create_graph();
  for (unsigned i = 0; i < 64; ++i) {
    auto parent = design.library.create_io("level" + std::to_string(i));
    body        = parent->create_graph();
    graph_util::create_typed_node(*body, Ntype_op::Sub).set_subnode(child);
    graph_util::create_typed_node(*body, Ntype_op::Sub).set_subnode(child);
    child = parent;
  }
  design.top = body;
  EXPECT_FALSE(mapped_cost(design));
}
TEST(MappedCost, RejectsDuplicateAndNonfiniteReachableCosts) {
  Mapped_design design;
  design.top = design.library.create_io("top")->create_graph();
  Region_qor row;
  row.module     = "top";
  row.gates      = 3;
  row.area       = 1.5;
  row.delay      = 2;
  design.regions = {row, row};
  EXPECT_FALSE(mapped_cost(design));
  design.regions.resize(1);
  design.regions[0].delay = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(mapped_cost(design));
  design.regions[0].delay = 2;
  // An absorbed definition cannot invalidate the complete emitted design.
  row.module              = "absorbed";
  row.area                = std::numeric_limits<double>::infinity();
  design.regions.push_back(row);
  const auto cost = mapped_cost(design);
  ASSERT_TRUE(cost);
  EXPECT_EQ(cost->gates, 3U);
}
}  // namespace
}  // namespace livehd::synth
