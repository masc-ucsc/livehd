// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pcrs.hpp"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"

namespace livehd::pcrs {
namespace {
namespace gu = graph_util;
using Pin    = hhds::Pin_class;

constexpr const char* kLib = R"(library(pcrs) {
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
  cell(AND2x1) {
    area : 3;
    pin(A) { direction : input; capacitance : 1; }
    pin(B) { direction : input; capacitance : 1; }
    pin(Y) { direction : output; function : "A & B"; }
  }
  cell(ICGx1) {
    area : 3;
    clock_gating_integrated_cell : "latch_posedge";
    latch (IQ, IQN) { data_in : "GATE"; enable : "!CLK"; }
    pin(CLK) { direction : input; clock_gate_clock_pin : true; clock : true; }
    pin(GATE) { direction : input; clock_gate_enable_pin : true; }
    pin(GCLK) { direction : output; clock_gate_out_pin : true; function : "IQ & CLK"; }
  }
  cell(DFFx1) {
    area : 5;
    ff(IQ, IQN) { next_state : "D"; clocked_on : "CLK"; }
    pin(CLK) { direction : input; capacitance : 1; clock : true; }
    pin(D)   { direction : input; capacitance : 1; }
    pin(Q)   { direction : output; function : "IQ"; }
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

// The same library with only an inverting-Q plain flop (ASAP7's DFFHQN family).
constexpr const char* kLibQn = R"(library(pcrs_qn) {
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
  cell(DFFQNx1) {
    area : 4;
    ff(IQN, IQNN) { next_state : "!D"; clocked_on : "CLK"; }
    pin(CLK) { direction : input; capacitance : 1; clock : true; }
    pin(D)   { direction : input; capacitance : 1; }
    pin(QN)  { direction : output; function : "IQN"; }
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

// What a sink sees looking up its reset: the root, the flop levels and the inversions in between.
struct Path {
  Pin  root;
  int  latency    = 0;
  int  inversions = 0;
  bool clocks_ok  = true;  // every minted flop is clocked like the sink's own flop
};

class Pcrs : public ::testing::Test {
protected:
  hhds::GraphLibrary           library;
  std::shared_ptr<hhds::Graph> graph;
  physical::Technology         tech;
  std::string                  lib_text = kLib;
  Pin                          reset, clock, clock2;

  void SetUp() override {
    tech.macros["BUFx1"]     = core_macro({"A", "Y"});
    tech.macros["ICGx1"]     = core_macro({"CLK", "GATE", "GCLK"});
    tech.macros["INVx1"]     = core_macro({"A", "Y"});
    tech.macros["AND2x1"]    = core_macro({"A", "B", "Y"});
    tech.macros["DFFx1"]     = core_macro({"CLK", "D", "Q"});
    tech.macros["DFFQNx1"]   = core_macro({"CLK", "D", "QN"});
    tech.macros["DFFASRNx1"] = core_macro({"CLK", "D", "RESETN", "SETN", "QN"});
    const auto io            = library.create_io("top");
    for (const auto* name : {"clk", "clk2", "rst"}) {
      io->add_input(name, io->get_input_pin_decls().size());
      io->set_bits(name, 1);
    }
    graph  = io->create_graph();
    clock  = graph->get_input_pin("clk");
    clock2 = graph->get_input_pin("clk2");
    reset  = graph->get_input_pin("rst");
    for (auto p : {clock, clock2, reset}) {
      gu::set_bits(p, 1);
    }
  }

  liberty::Cell_library cells() {
    const char* tmp = std::getenv("TEST_TMPDIR");
    const auto  dir = (tmp ? std::filesystem::path(tmp) : std::filesystem::temp_directory_path())
                     / ::testing::UnitTest::GetInstance()->current_test_info()->name();
    std::filesystem::create_directories(dir);
    const auto path = (dir / "pcrs.lib").string();
    std::ofstream(path) << lib_text;
    return liberty::Cell_library(path);
  }

  std::shared_ptr<hhds::GraphIO> io_of(const std::string& master, std::initializer_list<const char*> inputs, const char* output) {
    auto io = library.find_io(master);
    if (!io) {
      io               = library.create_io(master);
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

  // An async flop on `clk` whose clear and preset both ride `rst_driver`.
  void flop(const std::string& name, Pin rst_driver, Pin clk) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(io_of("DFFASRNx1", {"CLK", "D", "RESETN", "SETN"}, "QN"));
    n.set_name(name);
    n.create_sink_pin("CLK").connect_driver(clk);
    n.create_sink_pin("D").connect_driver(clk);
    n.create_sink_pin("SETN").connect_driver(rst_driver);
    n.create_sink_pin("RESETN").connect_driver(rst_driver);
    gu::set_bits(n.create_driver_pin("QN"), 1);
  }

  Pin inverter(const std::string& name, Pin driver) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(io_of("INVx1", {"A"}, "Y"));
    n.set_name(name);
    n.create_sink_pin("A").connect_driver(driver);
    auto out = n.create_driver_pin("Y");
    gu::set_bits(out, 1);
    return out;
  }

  static size_t fanout(Pin driver) {
    size_t n = 0;
    for (const auto& e : driver.out_edges()) {
      (void)e;
      ++n;
    }
    return n;
  }

  static Pin driver_named(const hhds::Node_class& n, const char* pin) { return n.get_sink_pin(pin).get_driver_pins().front(); }

  Path look(Pin sink, Pin sink_clock) {
    Path path;
    auto driver = sink.get_driver_pins().front();
    while (!driver.get_master_node().is_input_node()) {
      const auto node = driver.get_master_node();
      const auto io   = node.get_subnode_io();
      const auto name = io ? std::string(io->get_name()) : std::string{};
      if (name == "BUFx1") {
        driver = driver_named(node, "A");
      } else if (name == "INVx1") {
        ++path.inversions;
        driver = driver_named(node, "A");
      } else if ((name == "DFFx1" || name == "DFFQNx1") && is_pcrs_cell(std::string(node.get_name()))) {
        ++path.latency;
        path.inversions += name == "DFFQNx1";
        path.clocks_ok = path.clocks_ok && driver_named(node, "CLK") == sink_clock;
        driver         = driver_named(node, "D");
      } else {
        break;
      }
    }
    path.root = driver;
    return path;
  }

  size_t stats_flops() { return minted("DFFx1") + minted("DFFQNx1"); }

  size_t minted(const char* master) {
    size_t n = 0;
    for (auto node : graph->body().nodes()) {
      const auto io = node.get_subnode_io();
      n += io && io->get_name() == master && is_pcrs_cell(std::string(node.get_name()));
    }
    return n;
  }

  // Every reset pin of every async flop is `latency` flop levels from `reset` through `inversions` inversions.
  void expect_uniform(int latency, int inversions, const std::map<std::string, Pin>& clocks = {}) {
    size_t seen = 0;
    for (auto node : graph->body().nodes()) {
      const auto io = node.get_subnode_io();
      if (!io || io->get_name() != "DFFASRNx1") {
        continue;
      }
      const auto name = std::string(node.get_name());
      const auto clk  = driver_named(node, "CLK");
      for (const char* pin : {"RESETN", "SETN"}) {
        const auto path = look(node.get_sink_pin(pin), clk);
        EXPECT_EQ(path.root, reset) << name << "." << pin;
        EXPECT_EQ(path.latency, latency) << name << "." << pin;
        EXPECT_EQ(path.inversions % 2, inversions % 2) << name << "." << pin;
        EXPECT_TRUE(path.clocks_ok) << name << "." << pin;
        ++seen;
      }
    }
    EXPECT_GT(seen, 0u);
    (void)clocks;
  }

  void add_flops(int n, Pin clk, const std::string& prefix = "core.u") {
    for (int i = 0; i < n; ++i) {
      flop(prefix + std::to_string(i / 10) + ".r" + std::to_string(i) + (clk == clock ? "a" : "b"), reset, clk);
    }
  }

  static Depth_policy depth(unsigned n) {
    return [n](std::string_view, size_t, std::optional<unsigned>) { return n; };
  }
};

TEST_F(Pcrs, OneFlopLevelServesFortySinksWithEightPerDriver) {
  add_flops(20, clock);  // 40 reset pins
  const auto lib   = cells();
  const auto stats = build_reset(*graph, tech, lib, {.fanout = 8}, depth(1));
  EXPECT_EQ(stats.networks, 1u);
  EXPECT_EQ(stats.sinks, 40u);
  EXPECT_EQ(stats.flops, 5u);  // 40 / 8
  ASSERT_EQ(stats.domains.size(), 1u);
  EXPECT_EQ(stats.domains[0].depth, 1u);
  EXPECT_EQ(minted("DFFx1"), 5u);
  EXPECT_LE(fanout(reset), 8u);
  expect_uniform(1, 0);
  for (auto node : graph->body().nodes()) {
    if (is_pcrs_cell(std::string(node.get_name())) && node.get_subnode_io()->get_name() == "DFFx1") {
      EXPECT_EQ(node.attr(attrs::pcrs_stage).get(), 1u);  // level 1, Q not inverted
      EXPECT_LE(fanout(node.get_driver_pin("Q")), 8u);
    }
  }
}

TEST_F(Pcrs, TwoLevelsAreBalancedAndKeepEveryLoad) {
  add_flops(40, clock);  // 80 reset pins
  const auto lib   = cells();
  const auto stats = build_reset(*graph, tech, lib, {.fanout = 8}, depth(2));
  EXPECT_EQ(stats.sinks, 80u);
  EXPECT_EQ(stats.flops, 13u);  // 10 leaf flops + 3 above them: the branching is 10^(1/2) -> 4 per level, not the cap
  expect_uniform(2, 0);
  EXPECT_LE(fanout(reset), 8u);
}

TEST_F(Pcrs, DepthZeroIsAPlainBufferTree) {
  add_flops(40, clock);
  const auto lib   = cells();
  const auto stats = build_reset(*graph, tech, lib, {.fanout = 8}, depth(0));
  EXPECT_EQ(stats.flops, 0u);
  EXPECT_GT(stats.buffers, 0u);
  EXPECT_LE(fanout(reset), 8u);
  expect_uniform(0, 0);
}

TEST_F(Pcrs, SmallNetsStayFlatByDefault) {
  add_flops(3, clock);
  const auto lib   = cells();
  const auto stats = build_reset(*graph, tech, lib, {.fanout = 32});
  EXPECT_EQ(stats.flops, 0u);
  EXPECT_EQ(stats.buffers, 0u);
  EXPECT_EQ(fanout(reset), 6u);
  expect_uniform(0, 0);
}

TEST_F(Pcrs, DefaultDepthIsOneFlopLevelWhenFanoutForcesATree) {
  add_flops(40, clock);
  const auto lib   = cells();
  const auto stats = build_reset(*graph, tech, lib, {.fanout = 32});
  EXPECT_EQ(stats.domains.at(0).depth, 1u);
  expect_uniform(1, 0);
}

TEST_F(Pcrs, RebuildingChangesDepthWithoutLeavingTheOldTree) {
  add_flops(40, clock);
  const auto lib = cells();
  build_reset(*graph, tech, lib, {.fanout = 8}, depth(1));
  EXPECT_EQ(minted("DFFx1"), 10u);
  build_reset(*graph, tech, lib, {.fanout = 8}, depth(3));
  expect_uniform(3, 0);
  build_reset(*graph, tech, lib, {.fanout = 8}, depth(0));
  EXPECT_EQ(minted("DFFx1"), 0u);
  expect_uniform(0, 0);
  const auto again = build_reset(*graph, tech, lib, {.fanout = 8}, depth(2));
  EXPECT_EQ(minted("DFFx1"), again.flops);
  expect_uniform(2, 0);
  // Same depth twice: same shape.
  const auto first  = build_reset(*graph, tech, lib, {.fanout = 8}, depth(2));
  const auto second = build_reset(*graph, tech, lib, {.fanout = 8}, depth(2));
  EXPECT_EQ(first.flops, second.flops);
  EXPECT_EQ(first.buffers, second.buffers);
  expect_uniform(2, 0);
}

TEST_F(Pcrs, AnUnchangedNetworkIsLeftAloneAndTheDefaultKeepsTheCurrentDepth) {
  add_flops(40, clock);
  const auto lib   = cells();
  const auto first = build_reset(*graph, tech, lib, {.fanout = 8}, depth(2));
  ASSERT_EQ(first.domains.size(), 1u);
  EXPECT_FALSE(first.domains[0].kept);
  EXPECT_EQ(first.domains[0].cells.size(), first.flops + first.buffers);
  std::set<std::string> before;
  for (auto node : graph->body().nodes()) {
    if (is_pcrs_cell(std::string(node.get_name()))) {
      before.insert(std::string(node.get_name()));
    }
  }
  // Same depth: untouched, with the same cells reported.
  std::optional<unsigned> seen;
  const auto second = build_reset(*graph, tech, lib, {.fanout = 8}, [&](std::string_view, size_t, std::optional<unsigned> current) {
    seen = current;
    return 2u;
  });
  EXPECT_EQ(seen, std::optional<unsigned>{2u});
  ASSERT_EQ(second.domains.size(), 1u);
  EXPECT_TRUE(second.domains[0].kept);
  EXPECT_EQ(second.removed, 0u);
  EXPECT_EQ(second.domains[0].flops, first.flops);
  EXPECT_EQ(second.domains[0].cells.size(), first.domains[0].cells.size());
  std::set<std::string> after;
  for (auto node : graph->body().nodes()) {
    if (is_pcrs_cell(std::string(node.get_name()))) {
      after.insert(std::string(node.get_name()));
    }
  }
  EXPECT_EQ(before, after) << "an unchanged network must keep its cells, names and so their placement";
  // The default policy keeps the depth in the graph, not its own start value.
  const auto third = build_reset(*graph, tech, lib, {.fanout = 8});
  EXPECT_TRUE(third.domains[0].kept);
  EXPECT_EQ(third.domains[0].depth, 2u);
  // A different depth rebuilds.
  const auto fourth = build_reset(*graph, tech, lib, {.fanout = 8}, depth(1));
  EXPECT_FALSE(fourth.domains[0].kept);
  expect_uniform(1, 0);
}

TEST_F(Pcrs, ADomainGrowsOnlyWhenItsCellsAreOnTheWorstPath) {
  add_flops(40, clock);
  const auto lib = cells();
  Depth_state state;
  auto        built = build_reset(*graph, tech, lib, {.fanout = 8}, state.policy(8));
  ASSERT_EQ(built.domains.size(), 1u);
  EXPECT_EQ(built.domains[0].depth, 1u);
  ASSERT_FALSE(built.domains[0].cells.empty());
  // A path elsewhere in the design: nothing grows.
  EXPECT_TRUE(retarget_for_wns(built, {"core.u0.r0a:CLK", "core.u1.r3a:QN", "somewhere:D"}, state).empty());
  EXPECT_FALSE(state.get(built.domains[0].name).has_value());
  // A path through one of the domain's flops: one more level, for the whole domain.
  const auto flop_pin = built.domains[0].cells.front() + ":Q";
  EXPECT_EQ(retarget_for_wns(built, {"x:CLK", flop_pin, "y:D"}, state), built.domains[0].name);
  EXPECT_EQ(state.get(built.domains[0].name), std::optional<unsigned>{2u});
  built = build_reset(*graph, tech, lib, {.fanout = 8}, state.policy(8));
  EXPECT_EQ(built.domains[0].depth, 2u);
  EXPECT_FALSE(built.domains[0].kept);
  expect_uniform(2, 0);
  // The depth now in the graph is kept by the next call.
  built = build_reset(*graph, tech, lib, {.fanout = 8}, state.policy(8));
  EXPECT_TRUE(built.domains[0].kept);
  // A cap stops it.
  state.max_depth = 2;
  EXPECT_TRUE(retarget_for_wns(built, {built.domains[0].cells.front() + ":Q"}, state).empty());
  EXPECT_EQ(state.get(built.domains[0].name), std::optional<unsigned>{2u});
}

TEST_F(Pcrs, AReasonToDeepenJumpsToTheDepthWhereHopsBranchByAtMostEight) {
  add_flops(200, clock);  // 400 reset pins -> 50 leaf groups at fanout 8
  const auto lib = cells();
  Depth_state state;
  state.max_depth = 16;
  const auto built = build_reset(*graph, tech, lib, {.fanout = 8}, state.policy(8));
  EXPECT_EQ(built.domains[0].depth, 1u);
  // log_8(50) = 1.88 -> 2 levels; log_2(50) -> 6 levels for a branching of 2.
  EXPECT_EQ(retarget_for_wns(built, {built.domains[0].cells.front() + ":Q"}, state), "rst@clk");
  EXPECT_EQ(state.get("rst@clk"), std::optional<unsigned>{2u});
  Depth_state narrow;
  narrow.max_depth = 16;
  narrow.set("rst@clk", 1);
  EXPECT_FALSE(retarget_for_wns(built, {built.domains[0].cells.front() + ":Q"}, narrow, 2).empty());
  EXPECT_EQ(narrow.get("rst@clk"), std::optional<unsigned>{6u});
  // A small domain still gains one level.
  Depth_state small;
  small.set("rst@clk", 1);
  Stats tiny   = built;
  tiny.domains[0].sinks = 8;
  EXPECT_FALSE(retarget_for_wns(tiny, {built.domains[0].cells.front() + ":Q"}, small).empty());
  EXPECT_EQ(small.get("rst@clk"), std::optional<unsigned>{2u});
}

TEST_F(Pcrs, ADepthZeroNetworkGrowsWhenItsBuffersAreOnTheWorstPath) {
  add_flops(40, clock);
  const auto lib = cells();
  Depth_state state;
  state.set("rst@clk", 0);
  auto built = build_reset(*graph, tech, lib, {.fanout = 8}, state.policy(8));
  ASSERT_EQ(built.domains[0].depth, 0u);
  ASSERT_GT(built.domains[0].buffers, 0u);
  EXPECT_EQ(retarget_for_wns(built, {built.domains[0].cells.front() + ":Y"}, state), "rst@clk");
  built = build_reset(*graph, tech, lib, {.fanout = 8}, state.policy(8));
  EXPECT_EQ(built.domains[0].depth, 2u);  // 10 leaf groups: log_8(10) -> 2 levels
  expect_uniform(2, 0);
}

TEST_F(Pcrs, ADomainShrinksOnlyWithSlackBeyondItsLongestHop) {
  add_flops(40, clock);
  const auto lib = cells();
  Depth_state state;
  state.set("rst@clk", 2);
  const auto built = build_reset(*graph, tech, lib, {.fanout = 8}, state.policy(8));
  ASSERT_EQ(built.domains[0].depth, 2u);
  const auto roomy = [](std::string_view) { return Pin_timing{.slack = 900.0, .arrival = 80.0}; };
  const auto tight = [](std::string_view label) {
    return Pin_timing{.slack = label.ends_with(":Q") ? 30.0 : 900.0, .arrival = 80.0};
  };
  const auto blind = [](std::string_view) { return Pin_timing{}; };
  Depth_state other = state;
  EXPECT_TRUE(retarget_shrink(built, tight, 0, other).empty());  // a Q with 30 ps left: a merged hop would not fit
  EXPECT_TRUE(retarget_shrink(built, blind, 0, other).empty());  // no evidence: keep
  EXPECT_TRUE(retarget_shrink(built, roomy, 900, other).empty());  // the margin counts
  EXPECT_EQ(other.get("rst@clk"), std::optional<unsigned>{2u});
  const auto shrunk = retarget_shrink(built, roomy, 0, state);
  ASSERT_EQ(shrunk.size(), 1u);
  EXPECT_EQ(state.get("rst@clk"), std::optional<unsigned>{1u});
  const auto again = build_reset(*graph, tech, lib, {.fanout = 8}, state.policy(8));
  EXPECT_EQ(again.domains[0].depth, 1u);
  expect_uniform(1, 0);
}

// A synchronous reset: no async pin anywhere, the reset only gates the data of plain flops (AND2 -> D).
TEST_F(Pcrs, ASynchronousResetKeepsItsRootAcrossRebuildsAndIsFoundByItsOwnCells) {
  // The role sits on a mapper-made buffer behind the port, as in a mapped netlist: the rebuild deletes that buffer.
  auto mapped = gu::create_typed_node(*graph, Ntype_op::Sub);
  mapped.set_subnode(io_of("BUFx1", {"A"}, "Y"));
  mapped.set_name("mapped_rst_buf");
  mapped.create_sink_pin("A").connect_driver(reset);
  auto reset_buf = mapped.create_driver_pin("Y");
  gu::set_bits(reset_buf, 1);
  gu::add_signal_roles(reset_buf, attrs::signal_reset);
  for (int i = 0; i < 40; ++i) {
    auto gate = gu::create_typed_node(*graph, Ntype_op::Sub);
    gate.set_subnode(io_of("AND2x1", {"A", "B"}, "Y"));
    gate.set_name("core.u" + std::to_string(i / 10) + ".and" + std::to_string(i));
    gate.create_sink_pin("A").connect_driver(reset_buf);
    gate.create_sink_pin("B").connect_driver(clock2);
    auto y = gate.create_driver_pin("Y");
    gu::set_bits(y, 1);
    auto ff = gu::create_typed_node(*graph, Ntype_op::Sub);
    ff.set_subnode(io_of("DFFx1", {"CLK", "D"}, "Q"));
    ff.set_name("core.u" + std::to_string(i / 10) + ".ff" + std::to_string(i));
    ff.create_sink_pin("CLK").connect_driver(clock);
    ff.create_sink_pin("D").connect_driver(y);
    gu::set_bits(ff.create_driver_pin("Q"), 1);
  }
  const auto lib   = cells();
  Depth_state state;
  auto        first = build_reset(*graph, tech, lib, {.fanout = 8}, state.policy(8));
  ASSERT_EQ(first.domains.size(), 1u);
  EXPECT_EQ(first.domains[0].depth, 1u);
  EXPECT_EQ(first.domains[0].sinks, 40u);
  // The cells it minted have no role: the second call must still find the network, keep it, and report it.
  auto second = build_reset(*graph, tech, lib, {.fanout = 8}, state.policy(8));
  ASSERT_EQ(second.networks, 1u);
  ASSERT_EQ(second.domains.size(), 1u);
  EXPECT_TRUE(second.domains[0].kept);
  EXPECT_EQ(second.domains[0].flops, first.flops);
  EXPECT_FALSE(second.domains[0].cells.empty());
  // And a decision made on its cells deepens it.
  EXPECT_EQ(retarget_for_wns(second, {second.domains[0].cells.front() + ":Q"}, state), second.domains[0].name);
  const auto third = build_reset(*graph, tech, lib, {.fanout = 8}, state.policy(8));
  EXPECT_EQ(third.domains[0].depth, 2u);
  size_t levels2 = 0;
  for (auto node : graph->body().nodes()) {
    if (is_pcrs_cell(std::string(node.get_name())) && node.get_subnode_io()->get_name() == "DFFx1") {
      levels2 += node.attr(attrs::pcrs_stage).get() == 2u;
    }
  }
  EXPECT_GT(levels2, 0u);
}

TEST_F(Pcrs, ADeeperTreeBranchesLessAtEveryLevel) {
  add_flops(100, clock);  // 200 reset pins, 25 leaf groups at fanout 8
  const auto lib = cells();
  const auto d1  = build_reset(*graph, tech, lib, {.fanout = 8}, depth(1));
  const auto d2  = build_reset(*graph, tech, lib, {.fanout = 8}, depth(2));
  const auto d3  = build_reset(*graph, tech, lib, {.fanout = 8}, depth(3));
  // Level sizes: depth 1: 25 | depth 2: r=5 -> 5, 25 | depth 3: r=3 -> 3, 9, 25.
  EXPECT_EQ(d1.flops, 25u);
  EXPECT_EQ(d2.flops, 30u);
  EXPECT_EQ(d3.flops, 37u);
  expect_uniform(3, 0);
  // No flop of any level drives more than the cap, and the widest branching is below it.
  size_t widest = 0;
  for (auto node : graph->body().nodes()) {
    if (is_pcrs_cell(std::string(node.get_name())) && node.get_subnode_io()->get_name() == "DFFx1" &&
        node.attr(attrs::pcrs_stage).get() < 3u) {
      widest = std::max(widest, fanout(node.get_driver_pin("Q")));
    }
  }
  EXPECT_LE(widest, 3u);
}

TEST_F(Pcrs, EveryClockDomainGetsItsOwnTreeOnItsOwnClock) {
  add_flops(20, clock);
  add_flops(20, clock2, "other.u");
  const auto lib   = cells();
  const auto stats = build_reset(*graph, tech, lib, {.fanout = 8}, depth(1));
  EXPECT_EQ(stats.networks, 1u);
  ASSERT_EQ(stats.domains.size(), 2u);
  EXPECT_EQ(stats.flops, 10u);  // 5 per domain
  expect_uniform(1, 0);         // each minted flop is clocked like the flop it serves
}

TEST_F(Pcrs, InvertingQFlopsGetOneInverterSoTheResetKeepsItsPolarity) {
  lib_text = kLibQn;
  add_flops(20, clock);
  const auto lib   = cells();
  const auto stats = build_reset(*graph, tech, lib, {.fanout = 8}, depth(1));
  EXPECT_EQ(stats.flops, 5u);
  EXPECT_EQ(minted("DFFQNx1"), 5u);
  expect_uniform(1, 0);  // 1 inverter + 1 inverting flop = even
  build_reset(*graph, tech, lib, {.fanout = 8}, depth(2));
  expect_uniform(2, 0);  // two inverting flops cancel by themselves
}

TEST_F(Pcrs, AnInvertedBranchKeepsItsPolarityAndTheSameLatency) {
  for (int i = 0; i < 10; ++i) {
    flop("pos.r" + std::to_string(i), reset, clock);
  }
  const auto not_reset = inverter("inv", reset);
  for (int i = 0; i < 10; ++i) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(io_of("DFFASRNx1", {"CLK", "D", "RESETN", "SETN"}, "QN"));
    n.set_name("neg.r" + std::to_string(i));
    n.create_sink_pin("CLK").connect_driver(clock);
    n.create_sink_pin("D").connect_driver(clock);
    n.create_sink_pin("SETN").connect_driver(not_reset);
    n.create_sink_pin("RESETN").connect_driver(not_reset);
    gu::set_bits(n.create_driver_pin("QN"), 1);
  }
  const auto lib = cells();
  build_reset(*graph, tech, lib, {.fanout = 4}, depth(2));
  for (auto node : graph->body().nodes()) {
    const auto io = node.get_subnode_io();
    if (!io || io->get_name() != "DFFASRNx1") {
      continue;
    }
    const bool negative = std::string(node.get_name()).starts_with("neg.");
    for (const char* pin : {"RESETN", "SETN"}) {
      const auto path = look(node.get_sink_pin(pin), driver_named(node, "CLK"));
      EXPECT_EQ(path.root, reset);
      EXPECT_EQ(path.latency, 2) << node.get_name();
      EXPECT_EQ(path.inversions % 2, negative ? 1 : 0) << node.get_name();
    }
  }
}

TEST_F(Pcrs, FlopsOfAGatedDomainRideTheUngatedClock) {
  auto g = gu::create_typed_node(*graph, Ntype_op::Sub);
  g.set_subnode(io_of("ICGx1", {"CLK", "GATE"}, "GCLK"));
  g.set_name("icg");
  g.create_sink_pin("CLK").connect_driver(clock);
  g.create_sink_pin("GATE").connect_driver(clock2);
  auto gclk = g.create_driver_pin("GCLK");
  gu::set_bits(gclk, 1);
  for (int i = 0; i < 20; ++i) {
    flop("core.u" + std::to_string(i / 10) + ".g" + std::to_string(i), reset, gclk);
  }
  const auto lib   = cells();
  const auto stats = build_reset(*graph, tech, lib, {.fanout = 8}, depth(1));
  EXPECT_EQ(stats.domains.size(), 1u);
  EXPECT_EQ(stats.flops, 5u);
  for (auto node : graph->body().nodes()) {
    if (is_pcrs_cell(std::string(node.get_name())) && node.get_subnode_io()->get_name() == "DFFx1") {
      EXPECT_EQ(driver_named(node, "CLK"), clock) << "a reset stage on a gated clock would stall while the clock is stopped";
    }
  }
}

TEST_F(Pcrs, NeighboursShareADriver) {
  // Two far-apart groups of 8: names interleave them, positions must not.
  std::map<std::string, std::pair<double, double>> at;
  for (int i = 0; i < 16; ++i) {
    const auto name = std::format("u{}.r", i);
    flop(name, reset, clock);
    at[name] = {(i % 2) ? 1000.0 + i : 10.0 + i, 5.0};
  }
  const auto lib   = cells();
  const auto stats = build_reset(*graph, tech, lib, {.fanout = 8}, depth(1), [&](std::string_view instance) -> std::optional<std::pair<double, double>> {
    const auto it = at.find(std::string(instance));
    return it == at.end() ? std::nullopt : std::optional{it->second};
  });
  EXPECT_EQ(stats.flops, 4u);  // 32 reset pins / 8
  for (auto node : graph->body().nodes()) {
    if (!is_pcrs_cell(std::string(node.get_name())) || node.get_subnode_io()->get_name() != "DFFx1") {
      continue;
    }
    std::set<bool> sides;
    for (const auto& e : node.get_driver_pin("Q").out_edges()) {
      const auto name = std::string(e.sink.get_master_node().get_name());
      sides.insert(at.at(name).first > 500);
    }
    EXPECT_EQ(sides.size(), 1u) << node.get_name() << ": one driver serves both far-apart groups";
  }
}

TEST_F(Pcrs, BypassTurnsTheTreeIntoWiresAndKeepsPolarity) {
  add_flops(40, clock);
  const auto lib = cells();
  build_reset(*graph, tech, lib, {.fanout = 8}, depth(2));
  const auto flops = stats_flops();
  EXPECT_EQ(flops, 13u);
  EXPECT_EQ(bypass_stages(*graph), 13u);
  EXPECT_EQ(minted("DFFx1"), 0u);
  expect_uniform(0, 0);  // every reset pin is back on the root, through buffers only
}

TEST_F(Pcrs, BypassOfInvertingFlopsRestoresThePolarityWithInverters) {
  lib_text = kLibQn;
  add_flops(20, clock);
  const auto lib = cells();
  build_reset(*graph, tech, lib, {.fanout = 8}, depth(1));
  EXPECT_EQ(bypass_stages(*graph), 5u);
  size_t inverters = 0;
  for (auto node : graph->body().nodes()) {
    inverters += gu::type_op_of(node) == Ntype_op::Not;
  }
  EXPECT_EQ(inverters, 5u);  // one per inverting flop; the library INVx1 in front of level 1 stays
  EXPECT_EQ(bypass_stages(*graph), 0u);
}

TEST_F(Pcrs, NonCellLoadsKeepTheirDirectConnection) {
  add_flops(20, clock);
  graph->get_io()->add_output("probe", 0);
  const auto lib = cells();
  build_reset(*graph, tech, lib, {.fanout = 8}, depth(1));
  EXPECT_LE(fanout(reset), 8u);
}

TEST_F(Pcrs, DeadLibraryDoesNothing) {
  add_flops(20, clock);
  lib_text = R"(library(empty) { time_unit : "1ns"; })";
  const auto lib   = cells();
  const auto stats = build_reset(*graph, tech, lib, {.fanout = 8}, depth(1));
  EXPECT_EQ(stats.flops, 0u);
  EXPECT_EQ(fanout(reset), 40u);
}

}  // namespace
}  // namespace livehd::pcrs
