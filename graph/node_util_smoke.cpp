// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <array>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

TEST(NodeUtil, HotmuxWalkPairsAndFallbackInPortOrder) {
  auto& lib   = livehd::Hhds_graph_library::instance("lgdb_hotmux_pair_walk");
  auto  io    = lib.create_io("top");
  auto  graph = io->create_graph();
  auto  node  = gu::create_typed_node(*graph, Ntype_op::Hotmux);
  EXPECT_TRUE(gu::hotmux_inputs(node).arms.empty());
  EXPECT_TRUE(gu::hotmux_inputs(node).fallback.is_invalid());

  std::array<hhds::Pin_class, 5> pins;
  for (size_t i = 0; i < pins.size(); ++i) {
    pins[i] = gu::create_const(*graph, *Dlop::create_integer(static_cast<int64_t>(i)));
  }
  // Pin creation and edge insertion are deliberately out of order.
  for (const auto pid : {3, 0, 2, 1}) {
    pins[pid].connect_sink(node.create_sink_pin(pid));
  }
  const auto paired = gu::hotmux_inputs(node);
  ASSERT_EQ(paired.arms.size(), 2);
  EXPECT_EQ(paired.arms[0], std::make_pair(pins[0], pins[1]));
  EXPECT_EQ(paired.arms[1], std::make_pair(pins[2], pins[3]));
  EXPECT_TRUE(paired.fallback.is_invalid());
  pins[4].connect_sink(node.create_sink_pin(4));
  const auto fallback = gu::hotmux_inputs(node);
  EXPECT_EQ(fallback.arms, paired.arms);
  EXPECT_EQ(fallback.fallback, pins[4]);

  for (const auto& occurrence : graph->occurrences().nodes()) {
    if (gu::type_op_of(occurrence) != Ntype_op::Hotmux) {
      continue;
    }
    const auto inputs = gu::hotmux_inputs(occurrence);
    ASSERT_EQ(inputs.arms.size(), 2);
    EXPECT_EQ(gu::const_of(inputs.arms[0].first).to_just_i64(), 0);
    EXPECT_EQ(gu::const_of(inputs.arms[1].second).to_just_i64(), 3);
    EXPECT_EQ(gu::const_of(inputs.fallback).to_just_i64(), 4);
  }
}

TEST(NodeUtil, MaskConstructionChecksBeforeCreatingNodes) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_mask_mint_checks");
  auto  io  = lib.create_io("top");
  io->add_input("a", 0);
  auto       graph       = io->create_graph();
  auto       value       = graph->get_input_pin("a");
  const auto count_nodes = [&] {
    size_t count = 0;
    for (auto node : graph->body().nodes()) {
      (void)node;
      ++count;
    }
    return count;
  };
  for (auto mask : {gu::create_const(*graph, *Dlop::create_integer(0x55)),
                    gu::create_const(*graph, *Dlop::create_integer(-5)),
                    gu::create_const(*graph, *Dlop::create_integer(0)),
                    gu::create_const(*graph, *Dlop::from_pyrope("0ub1?1")),
                    value,
                    hhds::Pin_class{}}) {
    const auto before = count_nodes();
    EXPECT_THROW((void)gu::create_get_mask(*graph, value, mask), std::invalid_argument);
    EXPECT_THROW((void)gu::create_set_mask(*graph, value, mask, value), std::invalid_argument);
    EXPECT_EQ(count_nodes(), before);
  }
  EXPECT_THROW((void)gu::create_get_mask(*graph, value, -1, 3), std::invalid_argument);
  EXPECT_THROW((void)gu::create_get_mask(*graph, value, 4, 4), std::invalid_argument);
  const auto get = gu::create_get_mask(*graph, value, 3, 70);
  const auto set = gu::create_set_mask(*graph, value, 3, 70, value);
  for (auto node : {get, set}) {
    EXPECT_EQ(gu::get_driver_of_sink_name(node, "a"), value);
    EXPECT_EQ(gu::mask_window(gu::const_of(gu::get_driver_of_sink_name(node, "mask"))), std::make_pair(3, 70));
  }
  EXPECT_EQ(gu::get_driver_of_sink_name(set, "value"), value);
  const auto whole = gu::create_const(*graph, gu::mask_whole_const());
  EXPECT_NO_THROW((void)gu::create_get_mask(*graph, value, whole));
  EXPECT_NO_THROW((void)gu::create_set_mask(*graph, value, whole, value));
}
