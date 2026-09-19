// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "enableopt.hpp"

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace {
namespace gu = livehd::graph_util;

TEST(Enableopt, ConsumesDeepPrivateHoldChain) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_enableopt_deep_hold");
  auto  io  = lib.create_io("deep_hold");
  io->add_input("en", 1);
  io->add_input("data", 2);
  io->add_output("q", 3);
  auto graph = io->create_graph();
  auto flop  = gu::create_typed_node(*graph, Ntype_op::Flop);
  auto q     = flop.create_driver_pin(0);
  auto en    = graph->get_input_pin("en");
  auto data  = graph->get_input_pin("data");
  gu::setup_sink_by_name(flop, "enable").connect_driver(en);
  auto value = data;
  for (int i = 0; i < 2048; ++i) {
    auto mux = gu::create_typed_node(*graph, Ntype_op::Mux);
    mux.create_sink_pin(0).connect_driver(en);
    mux.create_sink_pin(1).connect_driver(q);
    mux.create_sink_pin(2).connect_driver(value);
    value = mux.create_driver_pin(0);
  }
  gu::setup_sink_by_name(flop, "din").connect_driver(value);
  q.connect_sink(graph->get_output_pin("q"));
  Enableopt{}.do_trans(graph);
  EXPECT_EQ(gu::get_driver_of_sink_name(flop, "din"), data);
  EXPECT_EQ(gu::get_driver_of_sink_name(flop, "enable"), en);
  for (auto node : graph->body().nodes()) {
    EXPECT_NE(gu::type_op_of(node), Ntype_op::Mux);
  }
}

TEST(Enableopt, SharedEnableClauseAndPrivateBranchFacts) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_enableopt_shared_control");
  auto  io  = lib.create_io("shared_control");
  io->add_input("a", 1);
  io->add_input("b", 2);
  io->add_input("data", 3);
  io->add_output("q", 4);
  auto graph     = io->create_graph();
  auto zero      = gu::create_const(*graph, *Dlop::create_integer(0));
  auto condition = [&](const char* name) {
    auto eq = gu::create_typed_node(*graph, Ntype_op::EQ);
    gu::append_sink_operand(eq, Ntype_op::EQ, 0).connect_driver(graph->get_input_pin(name));
    gu::append_sink_operand(eq, Ntype_op::EQ, 0).connect_driver(zero);
    return eq.create_driver_pin(0);
  };
  auto a = condition("a"), b = condition("b");
  auto either = gu::create_typed_node(*graph, Ntype_op::Or);
  gu::append_sink_operand(either, Ntype_op::Or, 0).connect_driver(a);
  gu::append_sink_operand(either, Ntype_op::Or, 0).connect_driver(b);
  std::vector<hhds::Node_class> states;
  for (int i = 0; i < 1024; ++i) {
    auto flop = gu::create_typed_node(*graph, Ntype_op::Flop);
    auto q    = flop.create_driver_pin(0);
    auto hold = gu::create_typed_node(*graph, Ntype_op::Mux);
    hold.create_sink_pin(0).connect_driver(b);
    hold.create_sink_pin(1).connect_driver(q);
    hold.create_sink_pin(2).connect_driver(graph->get_input_pin("data"));
    auto reset = gu::create_typed_node(*graph, Ntype_op::Mux);
    reset.create_sink_pin(0).connect_driver(a);
    reset.create_sink_pin(1).connect_driver(hold.create_driver_pin(0));
    reset.create_sink_pin(2).connect_driver(zero);
    gu::setup_sink_by_name(flop, "din").connect_driver(reset.create_driver_pin(0));
    gu::setup_sink_by_name(flop, "enable").connect_driver(either.create_driver_pin(0));
    states.push_back(flop);
  }
  states.front().get_driver_pin(0).connect_sink(graph->get_output_pin("q"));
  Enableopt{}.do_trans(graph);
  for (auto flop : states) {
    auto data = gu::get_driver_of_sink_name(flop, "din").get_master_node();
    ASSERT_EQ(gu::type_op_of(data), Ntype_op::Mux);
    EXPECT_EQ(data.get_sink_pin(1).get_driver_pin(), graph->get_input_pin("data"));
    EXPECT_EQ(data.get_sink_pin(2).get_driver_pin(), zero);
  }
}

TEST(Enableopt, SharedIndexedMuxKeepsEveryArm) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_enableopt_shared_indexed");
  auto  io  = lib.create_io("shared_indexed");
  io->add_input("index", 1);
  io->add_output("q", 2);
  auto graph = io->create_graph();
  auto index = graph->get_input_pin("index");
  auto mux   = gu::create_typed_node(*graph, Ntype_op::Mux);
  mux.create_sink_pin(0).connect_driver(index);
  for (int i = 0; i < 1024; ++i) {
    mux.create_sink_pin(i + 1).connect_driver(gu::create_const(*graph, *Dlop::create_integer(i)));
  }
  auto                          data = mux.create_driver_pin(0);
  std::vector<hhds::Node_class> states;
  for (int i = 0; i < 1024; ++i) {
    auto flop = gu::create_typed_node(*graph, Ntype_op::Flop);
    gu::setup_sink_by_name(flop, "enable").connect_driver(index);
    gu::setup_sink_by_name(flop, "din").connect_driver(data);
    states.push_back(flop);
  }
  states.front().create_driver_pin(0).connect_sink(graph->get_output_pin("q"));
  Enableopt{}.do_trans(graph);
  for (auto flop : states) {
    EXPECT_EQ(gu::get_driver_of_sink_name(flop, "din"), data);
    EXPECT_EQ(gu::get_driver_of_sink_name(flop, "enable"), index);
  }
  EXPECT_EQ(mux.inp_pins_snapshot().size(), 1025u);
}
}  // namespace
