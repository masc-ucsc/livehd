// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "register_merge.hpp"

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

namespace livehd::synth {
namespace {

namespace gu = livehd::graph_util;

constexpr hhds::Port_id kClk = 1, kData = 2, kEnable = 3, kOutA = 4, kOutB = 5;

struct Fixture {
  hhds::GraphLibrary           library;
  std::shared_ptr<hhds::GraphIO> io;
  std::shared_ptr<hhds::Graph>   graph;

  Fixture() {
    io = library.create_io("top");
    io->add_input("clk", kClk);
    io->add_input("d", kData);
    io->add_input("en", kEnable);
    io->add_output("a", kOutA);
    io->add_output("b", kOutB);
    for (const char* port : {"clk", "d", "en", "a", "b"}) {
      io->set_bits(port, 8);
      io->set_unsign(port, true);
    }
    graph = io->create_graph();
  }

  // A register on d with an optional enable and reset value.
  hhds::Node_class reg(const hhds::Pin_class& din, const hhds::Pin_class& enable, int bits, int reset_value = -1) {
    auto flop = gu::create_typed_node(*graph, Ntype_op::Flop, bits);
    gu::set_unsign(flop.create_driver_pin(0));
    din.connect_sink(gu::setup_sink_pid(flop, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "din"))));
    graph->get_input_pin("clk").connect_sink(
        gu::setup_sink_pid(flop, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin"))));
    if (!enable.is_invalid()) {
      enable.connect_sink(gu::setup_sink_pid(flop, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "enable"))));
    }
    if (reset_value >= 0) {
      gu::create_const(*graph, *Dlop::create_integer(reset_value))
          .connect_sink(gu::setup_sink_pid(flop, static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "initial"))));
    }
    return flop;
  }

  size_t flops() const {
    size_t n = 0;
    for (auto node : graph->body().nodes()) {
      n += gu::type_op_of(node) == Ntype_op::Flop ? 1 : 0;
    }
    return n;
  }
};

std::optional<Register_merge_stats> merge(Fixture& f) {
  const std::vector<std::shared_ptr<hhds::Graph>> defs{f.graph};
  return merge_equal_registers(defs, nullptr);
}

TEST(RegisterMerge, MergesRegistersWithTheSameInputs) {
  Fixture f;
  auto    a = f.reg(f.graph->get_input_pin("d"), f.graph->get_input_pin("en"), 8, 0);
  auto    b = f.reg(f.graph->get_input_pin("d"), f.graph->get_input_pin("en"), 8, 0);
  a.create_driver_pin(0).connect_sink(f.graph->get_output_pin("a"));
  b.create_driver_pin(0).connect_sink(f.graph->get_output_pin("b"));
  ASSERT_EQ(f.flops(), 2U);

  const auto stats = merge(f);
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers, 1U);
  EXPECT_EQ(stats->bits, 8U);
  ASSERT_EQ(f.flops(), 1U);
  // Both outputs read the survivor.
  for (auto node : f.graph->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Flop) {
      size_t readers = 0;
      for ([[maybe_unused]] auto edge : node.create_driver_pin(0).out_edges()) {
        ++readers;
      }
      EXPECT_EQ(readers, 2U);
    }
  }
}

TEST(RegisterMerge, KeepsRegistersThatDifferInResetValueEnableOrWidth) {
  Fixture f;
  auto    base = f.reg(f.graph->get_input_pin("d"), f.graph->get_input_pin("en"), 8, 0);
  auto    other_reset = f.reg(f.graph->get_input_pin("d"), f.graph->get_input_pin("en"), 8, 1);
  auto    no_enable = f.reg(f.graph->get_input_pin("d"), {}, 8, 0);
  auto    narrow = f.reg(f.graph->get_input_pin("d"), f.graph->get_input_pin("en"), 4, 0);
  base.create_driver_pin(0).connect_sink(f.graph->get_output_pin("a"));
  other_reset.create_driver_pin(0).connect_sink(f.graph->get_output_pin("b"));
  no_enable.create_driver_pin(0).connect_sink(f.graph->get_output_pin("b"));
  narrow.create_driver_pin(0).connect_sink(f.graph->get_output_pin("b"));

  const auto stats = merge(f);
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers, 0U);
  EXPECT_EQ(f.flops(), 4U);
}

TEST(RegisterMerge, RegistersFedByMergedTwinsMergeToo) {
  Fixture f;
  auto    a = f.reg(f.graph->get_input_pin("d"), f.graph->get_input_pin("en"), 8);
  auto    b = f.reg(f.graph->get_input_pin("d"), f.graph->get_input_pin("en"), 8);
  auto    c = f.reg(a.create_driver_pin(0), {}, 8);
  auto    e = f.reg(b.create_driver_pin(0), {}, 8);
  c.create_driver_pin(0).connect_sink(f.graph->get_output_pin("a"));
  e.create_driver_pin(0).connect_sink(f.graph->get_output_pin("b"));
  ASSERT_EQ(f.flops(), 4U);

  const auto stats = merge(f);
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->registers, 2U);
  EXPECT_GE(stats->rounds, 2U);
  EXPECT_EQ(f.flops(), 2U);
}

TEST(RegisterMerge, AnAdmissionRefusalIsReported) {
  Fixture f;
  f.reg(f.graph->get_input_pin("d"), {}, 8);
  Preparation_budget budget;
  budget.admission = [](std::string_view, uint64_t) { return false; };
  const std::vector<std::shared_ptr<hhds::Graph>> defs{f.graph};
  EXPECT_FALSE(merge_equal_registers(defs, &budget));
  EXPECT_TRUE(budget.refused);
}

}  // namespace
}  // namespace livehd::synth
