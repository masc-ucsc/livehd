// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sharing_hint.hpp"

#include "gtest/gtest.h"
#include "node_util.hpp"

namespace livehd::synth {
namespace gu = graph_util;
namespace {
class SharingHint : public ::testing::Test {
protected:
  hhds::GraphLibrary           library;
  std::shared_ptr<hhds::Graph> graph;
  hhds::Node_class             prefix, decoder, consumer;
  void                         SetUp() override {
    auto io = library.create_io("hint");
    for (uint32_t i = 0; i < 4; ++i) {
      auto name = std::to_string(i);
      io->add_input(name, i + 1);
      io->set_bits(name, 1);
      io->set_unsign(name, true);
    }
    graph           = io->create_graph();
    const auto gate = [&](hhds::Pin_class a, hhds::Pin_class b, Ntype_op op) {
      auto node = gu::create_typed_node(*graph, op);
      a.connect_sink(gu::setup_sink_pid(node, 0));
      b.connect_sink(gu::setup_sink_pid(node, 0));
      gu::set_ubits(node.create_driver_pin(0), 1);
      return node;
    };
    prefix   = gate(graph->get_input_pin("0"), graph->get_input_pin("1"), Ntype_op::And);
    decoder  = gate(prefix.get_driver_pin(0), graph->get_input_pin("2"), Ntype_op::And);
    consumer = gate(decoder.get_driver_pin(0), graph->get_input_pin("3"), Ntype_op::Xor);
    gate(decoder.get_driver_pin(0), graph->get_input_pin("1"), Ntype_op::Xor);
    graph->commit();
  }
};
}  // namespace

TEST_F(SharingHint, ZeroIsUnrestrictedAndCompleteClosureAvoidsCombBackEdges) {
  const auto control = preserve_shared_cones(*graph, 0);
  EXPECT_TRUE(control.completed);
  EXPECT_EQ(control.nodes, 0U);
  EXPECT_EQ(gu::node_color_of(prefix), 0);
  const auto hinted = preserve_shared_cones(*graph, 2);
  EXPECT_TRUE(hinted.completed);
  EXPECT_EQ(hinted.roots, 1U);
  EXPECT_EQ(hinted.nodes, 2U);
  EXPECT_NE(gu::node_color_of(decoder), 0);
  EXPECT_EQ(gu::node_color_of(decoder), gu::node_color_of(prefix));
  EXPECT_EQ(gu::node_color_of(consumer), 0);
}

TEST_F(SharingHint, RejectedAdmissionAndWideSignalsPublishNoColorChanges) {
  const auto refused = preserve_shared_cones(*graph, 2, [](std::string_view stage) { return stage != "sharing-hint-cone"; });
  EXPECT_FALSE(refused.completed);
  EXPECT_EQ(gu::node_color_of(decoder), 0);
  EXPECT_EQ(gu::node_color_of(prefix), 0);
  gu::set_ubits(decoder.get_driver_pin(0), 2);
  const auto wide = preserve_shared_cones(*graph, 2);
  EXPECT_TRUE(wide.completed);
  EXPECT_EQ(wide.nodes, 0U);
  EXPECT_EQ(gu::node_color_of(decoder), 0);
  EXPECT_FALSE(preserve_shared_cones(*graph, 1).completed);
}

TEST_F(SharingHint, ComplementedDecoderLeavesBelongToTheCompleteClosure) {
  auto inverse = gu::create_typed_node(*graph, Ntype_op::Not);
  graph->get_input_pin("0").connect_sink(gu::setup_sink_pid(inverse, 0));
  gu::set_ubits(inverse.create_driver_pin(0), 1);
  auto old_input = graph->get_input_pin("0");
  prefix.get_sink_pin(0).del_sink(old_input);
  inverse.get_driver_pin(0).connect_sink(prefix.get_sink_pin(0));
  const auto hinted = preserve_shared_cones(*graph, 2);
  EXPECT_TRUE(hinted.completed);
  EXPECT_EQ(hinted.roots, 1U);
  EXPECT_EQ(hinted.nodes, 3U);
  EXPECT_EQ(gu::node_color_of(inverse), gu::node_color_of(decoder));
}

TEST_F(SharingHint, FixedWordShiftIsWiringButVariableShiftIsNotAdmitted) {
  auto shift = gu::create_typed_node(*graph, Ntype_op::SRA);
  graph->get_input_pin("0").connect_sink(gu::setup_sink_pid(shift, 0));
  gu::create_const(*graph, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_pid(shift, 1));
  gu::set_ubits(shift.create_driver_pin(0), 4);
  prefix.get_sink_pin(0).del_sink(graph->get_input_pin("0"));
  shift.get_driver_pin(0).connect_sink(prefix.get_sink_pin(0));
  const auto hinted = preserve_shared_cones(*graph, 2);
  EXPECT_TRUE(hinted.completed);
  EXPECT_EQ(hinted.roots, 1U);
  EXPECT_EQ(hinted.nodes, 3U);
  EXPECT_EQ(gu::node_color_of(shift), gu::node_color_of(decoder));
  for (auto node : graph->body().nodes(hhds::Node_order::forward)) {
    gu::set_color(node, 0);
  }
  shift.get_sink_pin(1).del_sink();
  graph->get_input_pin("3").connect_sink(shift.get_sink_pin(1));
  const auto variable = preserve_shared_cones(*graph, 2);
  EXPECT_TRUE(variable.completed);
  EXPECT_EQ(variable.roots, 0U);
  EXPECT_EQ(variable.closure_rejections, 1U);
  EXPECT_EQ(gu::node_color_of(shift), 0);
}
}  // namespace livehd::synth
