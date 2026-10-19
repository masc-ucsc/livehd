// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "reset_tree.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "node_util.hpp"

namespace livehd::placement {
namespace {
namespace gu = graph_util;
using Pin    = hhds::Pin_class;

// BUFx1 and an ASAP7-shaped clear+preset QN flop (clear "!SETN", preset "!RESETN").
constexpr const char* kLib = R"(library(rst_tree) {
  time_unit : "1ns";
  cell(BUFx1) {
    area : 2;
    pin(A) { direction : input; capacitance : 1; }
    pin(Y) { direction : output; function : "A"; }
  }
  cell(INVx1) {
    area : 1;
    pin(A) { direction : input; capacitance : 1; }
    pin(Y) { direction : output; function : "A'"; }
  }
  cell(DFFASRNx1) {
    area : 6.5;
    ff(IQN, IQNN) { next_state : "!D"; clocked_on : "CLK"; clear : "!SETN"; preset : "!RESETN"; clear_preset_var1 : L; clear_preset_var2 : L; }
    pin(CLK) { direction : input; capacitance : 1; clock : true; }
    pin(D)   { direction : input; capacitance : 1; }
    pin(RESETN) { direction : input; capacitance : 1; }
    pin(SETN) { direction : input; capacitance : 1; }
    pin(QN) { direction : output; function : "IQN"; }
  }
})";

physical::Macro core_macro(std::initializer_list<const char*> pins) {
  physical::Macro m;
  m.klass  = "CORE";
  m.width  = 1;
  m.height = 1;
  for (const auto* pin : pins) {
    m.pins[pin] = {};
  }
  return m;
}

class Reset_tree : public ::testing::Test {
protected:
  hhds::GraphLibrary           library;
  std::shared_ptr<hhds::Graph> graph;
  physical::Technology         tech;
  std::string                  liberty;
  Pin                          reset, clock, tie;
  void                         SetUp() override {
    const auto dir = std::filesystem::path(std::getenv("TEST_TMPDIR")) / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::create_directories(dir);
    liberty = (dir / "rst_tree.lib").string();
    std::ofstream(liberty) << kLib;
    tech.macros["BUFx1"]      = core_macro({"A", "Y"});
    tech.macros["INVx1"]      = core_macro({"A", "Y"});
    tech.macros["DFFASRNx1"]  = core_macro({"CLK", "D", "RESETN", "SETN", "QN"});
    const auto io = library.create_io("top");
    for (const auto* name : {"clk", "rst", "tie"}) {
      io->add_input(name, io->get_input_pin_decls().size());
      io->set_bits(name, 1);
    }
    graph = io->create_graph();
    clock = graph->get_input_pin("clk");
    reset = graph->get_input_pin("rst");
    tie   = graph->get_input_pin("tie");
    gu::set_bits(clock, 1);
    gu::set_bits(reset, 1);
    gu::set_bits(tie, 1);
  }
  std::shared_ptr<hhds::GraphIO> io_of(const std::string& master, std::initializer_list<const char*> inputs, const char* output) {
    auto io = library.find_io(master);
    if (!io) {
      io = library.create_io(master);
      hhds::Port_id id = 0;
      for (const auto* name : inputs) {
        io->add_input(name, id++);
        io->set_bits(name, 1);
      }
      io->add_output(output, 0);
      io->set_bits(output, 1);
    }
    return io;
  }
  // An async flop on `rst` for both clear and preset (the pins are only fanout here).
  void flop(const std::string& name, Pin rst_driver) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(io_of("DFFASRNx1", {"CLK", "D", "RESETN", "SETN"}, "QN"));
    n.set_name(name);
    n.create_sink_pin("CLK").connect_driver(clock);
    n.create_sink_pin("D").connect_driver(clock);
    n.create_sink_pin("SETN").connect_driver(rst_driver);
    n.create_sink_pin("RESETN").connect_driver(rst_driver);
    gu::set_bits(n.create_driver_pin("QN"), 1);
  }
  void inverter(const std::string& name, Pin driver) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(io_of("INVx1", {"A"}, "Y"));
    n.set_name(name);
    n.create_sink_pin("A").connect_driver(driver);
    gu::set_bits(n.create_driver_pin("Y"), 1);
  }
  static size_t fanout(Pin driver) {
    size_t n = 0;
    for (const auto& e : driver.out_edges()) {
      (void)e;
      ++n;
    }
    return n;
  }
  // The net a sink pin is on, looking through identity buffers: the original reset driver.
  static Pin root_of(Pin sink) {
    auto driver = sink.get_driver_pins().front();
    while (!driver.get_master_node().is_input_node()) {
      const auto io = driver.get_master_node().get_subnode_io();
      if (!io || io->get_name() != "BUFx1") {
        return driver;
      }
      driver = driver.get_master_node().get_sink_pin("A").get_driver_pins().front();
    }
    return driver;
  }
};

TEST_F(Reset_tree, BoundsAWideResetNetAndKeepsEveryLoad) {
  for (int i = 0; i < 40; ++i) {
    flop("core.u" + std::to_string(i / 10) + ".r" + std::to_string(i), reset);
  }
  for (int i = 0; i < 4; ++i) {
    inverter("core.inv" + std::to_string(i), reset);  // sync-reset style loads of the same net
  }
  ASSERT_EQ(fanout(reset), 84u);
  const auto stats = buffer_reset_nets(*graph, tech, liberty, 8);
  EXPECT_EQ(stats.nets, 1u);
  EXPECT_EQ(stats.sinks, 84u);
  EXPECT_GE(stats.levels, 2u);  // 84 -> 11 -> 2 buffers
  EXPECT_LE(fanout(reset), 8u);
  size_t buffers = 0, loads = 0;
  for (auto n : graph->body().nodes()) {
    const auto io = n.get_subnode_io();
    if (!io) {
      continue;
    }
    if (io->get_name() == "BUFx1") {
      ++buffers;
      EXPECT_LE(fanout(n.get_driver_pin("Y")), 8u) << n.get_name();
      EXPECT_NE(std::string(n.get_name()).find("__place_rsttree"), std::string::npos);
      continue;
    }
    const std::vector<const char*> pins = io->get_name() == "DFFASRNx1" ? std::vector<const char*>{"SETN", "RESETN"}
                                                                          : std::vector<const char*>{"A"};
    for (const auto* pin : pins) {
      EXPECT_TRUE(root_of(n.get_sink_pin(pin)) == reset) << n.get_name() << ":" << pin;
      ++loads;
    }
  }
  EXPECT_EQ(buffers, stats.buffers);
  EXPECT_EQ(loads, 84u);  // 40 flops x (SETN, RESETN) + 4 inverters: all still on the reset
}

TEST_F(Reset_tree, GroupsNeighboursAndIsIdempotent) {
  for (int i = 0; i < 32; ++i) {
    flop("g" + std::to_string(i / 8) + ".r" + std::to_string(i), reset);
  }
  const auto first = buffer_reset_nets(*graph, tech, liberty, 16);
  EXPECT_EQ(first.nets, 1u);
  // 64 pins in groups of 16, taken in name order: the first buffer serves the eight flops of hierarchy g0 only.
  std::set<std::string> served;
  for (auto n : graph->body().nodes()) {
    const auto io = n.get_subnode_io();
    if (io && io->get_name() == "BUFx1" && std::string(n.get_name()).starts_with("g0.")) {
      for (const auto& e : n.get_driver_pin("Y").out_edges()) {
        served.insert(std::string(e.sink.get_master_node().get_name()).substr(0, 2));
      }
    }
  }
  EXPECT_EQ(served, (std::set<std::string>{"g0"}));
  const auto again = buffer_reset_nets(*graph, tech, liberty, 16);
  EXPECT_EQ(again.nets, 0u);
  EXPECT_EQ(again.buffers, 0u);
}

TEST_F(Reset_tree, SmallNetsAndOffAreUntouched) {
  for (int i = 0; i < 6; ++i) {
    flop("r" + std::to_string(i), reset);
  }
  EXPECT_EQ(buffer_reset_nets(*graph, tech, liberty, 0).buffers, 0u);
  EXPECT_EQ(buffer_reset_nets(*graph, tech, liberty, 1).buffers, 0u);
  EXPECT_EQ(buffer_reset_nets(*graph, tech, liberty, 12).buffers, 0u);  // 12 pins fit one driver
  EXPECT_EQ(fanout(reset), 12u);
}

TEST_F(Reset_tree, LeavesADriverThatResolvesToAConstant) {
  for (int i = 0; i < 20; ++i) {
    flop("r" + std::to_string(i), reset);
    flop("t" + std::to_string(i), tie);  // the tied inactive pins of clear+preset flops: a wiring alias of a constant
  }
  const auto stats = buffer_reset_nets(*graph, tech, liberty, 8, [&](const Pin& driver) { return driver == tie; });
  EXPECT_EQ(stats.nets, 1u);
  EXPECT_EQ(fanout(tie), 40u);  // untouched
  EXPECT_LE(fanout(reset), 8u);
}

TEST_F(Reset_tree, NeedsAPlaceableBuffer) {
  for (int i = 0; i < 20; ++i) {
    flop("r" + std::to_string(i), reset);
  }
  tech.macros.erase("BUFx1");
  EXPECT_EQ(buffer_reset_nets(*graph, tech, liberty, 4).buffers, 0u);
  EXPECT_EQ(fanout(reset), 40u);
}
}  // namespace
}  // namespace livehd::placement
