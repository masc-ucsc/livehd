// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sim_loop_fusion.hpp"

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "sim_color_plan.hpp"

namespace {
namespace gu = livehd::graph_util;
std::shared_ptr<hhds::Graph> make_pair(std::string_view tag, bool dependent, bool different_count) {
  auto& library  = livehd::Hhds_graph_library::instance(std::string("lgdb_sim_fusion_") + std::string(tag));
  auto  child_io = library.create_io("body");
  child_io->add_input("seed", 0);
  child_io->add_input("index", 1);
  child_io->add_output("result", 2);
  for (auto name : {"seed", "index", "result"}) {
    child_io->set_bits(name, 8);
  }
  auto child  = child_io->create_graph();
  auto invert = gu::create_typed_node(*child, Ntype_op::Not);
  child->get_input_pin("seed").connect_sink(invert.create_sink_pin(0));
  auto value = invert.create_driver_pin(0);
  gu::set_bits(value, 8);
  value.connect_sink(child->get_output_pin("result"));
  auto io = library.create_io("parent");
  io->add_input("a", 0);
  io->add_input("b", 1);
  io->add_output("x", 2);
  io->add_output("y", 3);
  for (auto name : {"a", "b", "x", "y"}) {
    io->set_bits(name, 8);
  }
  auto            graph = io->create_graph();
  hhds::Pin_class first_result;
  for (size_t i = 0; i < 2; ++i) {
    hhds::Subnode_loop descriptor;
    descriptor.first       = 3;
    descriptor.step        = 2;
    descriptor.count       = i == 1 && different_count ? 5 : 4;
    descriptor.index_input = 1;
    auto loop              = gu::create_typed_node(*graph, Ntype_op::Sub);
    loop.set_subnode(child_io, descriptor);
    auto seed = graph->get_input_pin(i == 0 ? "a" : "b");
    if (i == 1 && dependent) {
      auto bridge = gu::create_typed_node(*graph, Ntype_op::Not);
      first_result.connect_sink(bridge.create_sink_pin(0));
      seed = bridge.create_driver_pin(0);
      gu::set_bits(seed, 8);
    }
    seed.connect_sink(loop.create_sink_pin(0));
    auto output = loop.create_driver_pin(2);
    gu::set_bits(output, 8);
    output.connect_sink(loop.create_sink_pin(0));
    output.connect_sink(graph->get_output_pin(i == 0 ? "x" : "y"));
    if (i == 0) {
      first_result = output;
    }
  }
  return graph;
}
}  // namespace

TEST(SimLoopFusion, IndependentLoopsShareOneIndexAndKeepBothCarries) {
  auto       graph  = make_pair("independent", false, false);
  const auto bodies = livehd::sim::fuse_parallel_loops(graph.get());
  ASSERT_EQ(bodies.size(), 1u);
  size_t loops = 0;
  for (auto node : graph->body().nodes()) {
    if (auto descriptor = node.subnode_loop()) {
      ++loops;
      EXPECT_EQ(descriptor->first, 3);
      EXPECT_EQ(descriptor->step, 2);
      EXPECT_EQ(descriptor->count, 4u);
      ASSERT_TRUE(descriptor->index_input);
      EXPECT_EQ(node.subnode_group().carries().size(), 2u);
    }
  }
  EXPECT_EQ(loops, 1u);
  EXPECT_EQ(bodies.front()->get_io()->get_input_pin_decls().size(), 3u);
  for (const auto& port : bodies.front()->get_io()->get_input_pin_decls()) {
    EXPECT_EQ(gu::bits_of(bodies.front()->get_input_pin(port.name)), 8);
  }
  auto plan = livehd::sim::Color_plan::discover(graph.get());
  EXPECT_TRUE(plan.complete()) << plan.report();
  EXPECT_EQ(plan.summary().compact_loops, 1u);
}

TEST(SimLoopFusion, TransitiveDependencyPreventsFusion) {
  auto graph = make_pair("dependent", true, false);
  EXPECT_TRUE(livehd::sim::fuse_parallel_loops(graph.get()).empty());
  auto plan = livehd::sim::Color_plan::discover(graph.get());
  EXPECT_TRUE(plan.complete()) << plan.report();
  EXPECT_EQ(plan.summary().compact_loops, 2u);
}

TEST(SimLoopFusion, UnequalIterationCountsRemainSeparate) {
  auto graph = make_pair("count", false, true);
  EXPECT_TRUE(livehd::sim::fuse_parallel_loops(graph.get()).empty());
}
