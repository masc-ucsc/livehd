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
    return [n](std::string_view, size_t) { return n; };
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
      EXPECT_EQ(node.attr(attrs::pcrs_stage).get(), 1u);
      EXPECT_LE(fanout(node.get_driver_pin("Q")), 8u);
    }
  }
}

TEST_F(Pcrs, TwoLevelsAreBalancedAndKeepEveryLoad) {
  add_flops(40, clock);  // 80 reset pins
  const auto lib   = cells();
  const auto stats = build_reset(*graph, tech, lib, {.fanout = 8}, depth(2));
  EXPECT_EQ(stats.sinks, 80u);
  EXPECT_EQ(stats.flops, 12u);  // 10 leaf flops + 2 above them
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
