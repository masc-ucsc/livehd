// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_gate.hpp"

#include "gtest/gtest.h"
#include "inline_sub.hpp"
#include "node_util.hpp"

namespace {
namespace gu = livehd::graph_util;
using livehd::clock_gate::classify_leaf;
using livehd::clock_gate::Leaf_kind;

TEST(ClockGateLeaf, TracesTheSampledClockAndTheGatedOutput) {
  hhds::GraphLibrary library;
  const auto         io = library.create_io("gate");
  io->add_input("clock", 0);
  io->add_input("enable", 1);
  io->add_output("gated", 2);
  for (const auto* port : {"clock", "enable", "gated"}) {
    io->set_bits(port, 1);
  }
  const auto graph  = io->create_graph();
  const auto clock  = graph->get_input_pin("clock");
  const auto enable = graph->get_input_pin("enable");
  const auto invert = gu::create_typed_node(*graph, Ntype_op::Not, 1);
  clock.connect_sink(invert.create_sink_pin(0));
  const auto latch   = gu::create_typed_node(*graph, Ntype_op::Latch, 1);
  auto       control = gu::setup_sink_by_name(latch, "enable");
  invert.create_driver_pin(0).connect_sink(control);
  enable.connect_sink(gu::setup_sink_by_name(latch, "din"));
  const auto gate = gu::create_typed_node(*graph, Ntype_op::And, 1);
  clock.connect_sink(gu::setup_sink_by_name(gate, "as"));
  const auto signed_q = gu::fit_to_port(*graph, latch.create_driver_pin(0), 1, true);
  signed_q.connect_sink(gu::setup_sink_by_name(gate, "as"));
  const auto gated  = gate.create_driver_pin(0);
  const auto output = graph->get_output_pin("gated");
  const auto low    = gu::create_get_mask(*graph, gated, 0, 1).create_driver_pin(0);
  gu::set_ubits(low, 1);
  low.connect_sink(output);
  EXPECT_EQ(classify_leaf(*graph), Leaf_kind::latch);

  control.del_sink();
  enable.connect_sink(control);
  EXPECT_EQ(classify_leaf(*graph), Leaf_kind::none);
  control.del_sink();
  clock.connect_sink(control);
  output.del_sink();
  const auto high = gu::create_get_mask(*graph, gated, 1, 2).create_driver_pin(0);
  gu::set_ubits(high, 1);
  high.connect_sink(output);
  EXPECT_EQ(classify_leaf(*graph), Leaf_kind::none);
  output.del_sink();
  gated.connect_sink(output);
  EXPECT_EQ(classify_leaf(*graph), Leaf_kind::latch);

  const auto extra = gu::create_typed_node(*graph, Ntype_op::Flop, 1);
  clock.connect_sink(gu::setup_sink_by_name(extra, "clock_pin"));
  enable.connect_sink(gu::setup_sink_by_name(extra, "din"));
  EXPECT_EQ(classify_leaf(*graph), Leaf_kind::none);
}
}  // namespace
