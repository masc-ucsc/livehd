// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_census.hpp"

#include "gtest/gtest.h"
#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "node_util.hpp"
#include "rapidjson/document.h"

namespace gu = livehd::graph_util;
namespace {
const char* cells = R"lib(
library(unit) {
 time_unit : "1ps"; capacitive_load_unit(1,ff);
 cell(FF) { ff(Qi,Qn) { clocked_on : "C"; next_state : "D"; }
   pin(C) { direction : input; capacitance : 2; } pin(D) { direction : input; }
   pin(Q) { direction : output; function : "Qi"; } }
 cell(GATE) { dont_use : true; clock_gating_integrated_cell : latch_posedge_precontrol;
   pin(C) { direction : input; clock_gate_clock_pin : true; }
   pin(E) { direction : input; clock_gate_enable_pin : true; }
   pin(T) { direction : input; clock_gate_test_pin : true; }
   pin(Y) { direction : output; state_function : "C & IQ"; clock_gate_out_pin : true; }}
 cell(INV) { pin(A) { direction : input; } pin(Y) { direction : output; function : "!A"; }}
 cell(LAT) { latch(IQ,IQN) { enable : "!C"; data_in : "D"; }
   pin(C) { direction : input; } pin(D) { direction : input; }
   pin(Y) { direction : output; function : "IQ"; } }
 cell(AND2) { pin(A) { direction : input; } pin(B) { direction : input; }
   pin(Y) { direction : output; function : "A & B"; }}
}
)lib";

struct Fixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> top;
  Fixture() {
    for (const auto& [name, inputs] : std::map<std::string, std::vector<std::string>>{
             {  "FF",      {"C", "D"}},
             {"GATE", {"C", "E", "T"}},
             { "INV",           {"A"}},
             {"AND2",      {"A", "B"}},
             { "LAT",      {"C", "D"}}
    }) {
      auto io  = lib.create_io(name);
      int  pid = 0;
      for (const auto& pin : inputs) {
        io->add_input(pin, ++pid);
      }
      io->add_output(name == "FF" ? "Q" : "Y", ++pid);
    }
    auto io = lib.create_io("top");
    io->add_input("clk", 1);
    io->add_input("en", 2);
    io->add_input("extra", 3);
    top = io->create_graph();
  }
  hhds::Node_class cell(const char* master, const std::string& name) {
    auto n = gu::create_typed_node(*top, Ntype_op::Sub);
    n.set_subnode(lib.find_io(master));
    n.attr(hhds::attrs::name).set(name);
    return n;
  }
  hhds::Pin_class gate(const char* name, hhds::Pin_class clock, hhds::Pin_class en, bool test = false) {
    auto g = cell("GATE", name);
    g.attr(hhds::attrs::srcid).set(top->source_locator().mint("clock.sv", 8, 12, 3));
    clock.connect_sink(g.create_sink_pin("C"));
    en.connect_sink(g.create_sink_pin("E"));
    gu::create_const(*top, *Dlop::create_integer(test)).connect_sink(g.create_sink_pin("T"));
    return g.create_driver_pin("Y");
  }
  std::string analyze() { return livehd::pcrs::clock_census(*top, cells); }
};

TEST(ClockCensus, DiscreteGateTracesThroughSharedClockForkToLatchedEnable) {
  Fixture f;
  auto    clk = f.top->get_input_pin("clk"), en = f.top->get_input_pin("en");
  auto    latch = f.cell("LAT", "latch"), gate = f.cell("AND2", "gate"), reg = f.cell("FF", "r");
  clk.connect_sink(latch.create_sink_pin("C"));
  en.connect_sink(latch.create_sink_pin("D"));
  clk.connect_sink(gate.create_sink_pin("A"));
  latch.create_driver_pin("Y").connect_sink(gate.create_sink_pin("B"));
  gate.create_driver_pin("Y").connect_sink(reg.create_sink_pin("C"));
  rapidjson::Document report;
  report.Parse(livehd::pcrs::clock_census(*f.top, cells).c_str());
  ASSERT_EQ(report["domains"].Size(), 1u);
  EXPECT_STREQ(report["domains"][0]["root"].GetString(), "clk");
  ASSERT_EQ(report["gating_functions"].Size(), 1u);
  EXPECT_EQ(report["gating_functions"][0]["flop_clock_pins"].GetUint(), 1u);
  EXPECT_STREQ(report["gating_functions"][0]["enable"]["pin"].GetString(), "en");
}

TEST(ClockCensus, OppositeEdgeEnableHasItsOwnGatingFunction) {
  for (bool opposite_edge : {false, true}) {
    Fixture f;
    auto    clk = f.top->get_input_pin("clk"), en = f.top->get_input_pin("en");
    auto    stored = f.cell("FF", "enable_ff"), gate = f.cell("AND2", "gate"), reg = f.cell("FF", "reg");
    auto    phase = clk;
    if (opposite_edge) {
      auto inv = f.cell("INV", "phase");
      clk.connect_sink(inv.create_sink_pin("A"));
      phase = inv.create_driver_pin("Y");
    }
    phase.connect_sink(stored.create_sink_pin("C"));
    en.connect_sink(stored.create_sink_pin("D"));
    clk.connect_sink(gate.create_sink_pin("A"));
    stored.create_driver_pin("Q").connect_sink(gate.create_sink_pin("B"));
    gate.create_driver_pin("Y").connect_sink(reg.create_sink_pin("C"));
    rapidjson::Document report;
    report.Parse(f.analyze().c_str());
    const auto& groups = report["gating_functions"];
    ASSERT_EQ(groups.Size(), opposite_edge ? 1u : 0u);
    if (opposite_edge) {
      EXPECT_STREQ(groups[0]["style"].GetString(), "discrete_and_edge_enable");
      EXPECT_STREQ(groups[0]["enable"]["pin"].GetString(), "en");
      EXPECT_EQ(groups[0]["flop_clock_pins"].GetUint(), 1u);
    }
  }
}

TEST(ClockCensus, ClonesShareFunctionWhileParentAndTestControlRemainDistinct) {
  Fixture    f;
  const auto c = f.top->get_input_pin("clk"), en = f.top->get_input_pin("en");
  auto a = f.gate("a", c, en), b = f.gate("b", c, en), nested = f.gate("nested", b, en), tested = f.gate("tested", c, en, true);
  for (unsigned i = 0; i < 39; ++i) {
    auto ff     = f.cell("FF", std::format("reg{}", i));
    auto driver = i < 16 ? a : i < 32 ? b : i < 38 ? nested : tested;
    driver.connect_sink(ff.create_sink_pin("C"));
  }
  rapidjson::Document d;
  d.Parse(f.analyze().c_str());
  ASSERT_FALSE(d.HasParseError());
  EXPECT_EQ(d["domains"].Size(), 1u);
  EXPECT_EQ(d["domains"][0]["max_gate_depth"].GetUint(), 2u);
  EXPECT_EQ(d["flop_clock_pins"].GetUint(), 39u);
  EXPECT_EQ(d["gating_functions"].Size(), 3u);
  EXPECT_EQ(d["integrated_gate_library"]["declared_masters"].GetUint(), 1u);
  EXPECT_EQ(d["integrated_gate_library"]["dont_use_masters"].GetUint(), 1u);
  EXPECT_EQ(d["integrated_gate_library"]["mapper_eligible_masters"].Size(), 0u);
  EXPECT_DOUBLE_EQ(d["flop_share_in_functions_below_6"].GetDouble(), 1. / 39);
  EXPECT_DOUBLE_EQ(d["flop_share_in_functions_above_30"].GetDouble(), 32. / 39);
  bool parent = false, chained = false;
  for (const auto& group : d["gating_functions"].GetArray()) {
    if (group["gates"].Size() == 2) {
      parent = true;
      EXPECT_EQ(group["flop_clock_pins"].GetUint(), 32u);
      EXPECT_STREQ(group["test"]["kind"].GetString(), "literal");
      EXPECT_FALSE(group["test"]["value"].GetBool());
    }
    if (group["parent"].GetUint() != 0) {
      chained = true;
      EXPECT_EQ(group["flop_clock_pins"].GetUint(), 6u);
    }
  }
  EXPECT_TRUE(parent && chained);
  ASSERT_EQ(d["items"].Size(), 1u);
  const auto& item = d["items"][0];
  EXPECT_STREQ(item["kind"].GetString(), "small_gating_function");
  EXPECT_STREQ(item["pin"].GetString(), "tested:Y");
  ASSERT_TRUE(item["source"].IsString());
  EXPECT_TRUE(std::string_view(item["source"].GetString()).ends_with("clock.sv:3"));
  EXPECT_EQ(item["clock_pin_cap_ff"].GetDouble(), 2);
}

TEST(ClockCensus, SharingTheRootRemovesDerivedDomainAndMixedEdgeCosts) {
  for (bool shared : {false, true}) {
    Fixture f;
    auto    c = f.top->get_input_pin("clk"), en = f.top->get_input_pin("en");
    auto    logic = f.cell("AND2", "derived");
    c.connect_sink(logic.create_sink_pin("A"));
    en.connect_sink(logic.create_sink_pin("B"));
    auto inv = f.cell("INV", "fall");
    c.connect_sink(inv.create_sink_pin("A"));
    for (unsigned i = 0; i < 8; ++i) {
      auto clock = shared || i < 5 ? c : i == 5 ? inv.create_driver_pin("Y") : logic.create_driver_pin("Y");
      clock.connect_sink(f.cell("FF", std::format("reg{}", i)).create_sink_pin("C"));
    }
    rapidjson::Document d;
    d.Parse(f.analyze().c_str());
    ASSERT_FALSE(d.HasParseError());
    EXPECT_EQ(d["unresolved_clock_pins"].GetUint(), 0u);
    EXPECT_EQ(d["domains"].Size(), shared ? 1u : 2u);
    if (shared) {
      EXPECT_EQ(d["items"].Size(), 0u);
    } else {
      ASSERT_EQ(d["items"].Size(), 3u);
      EXPECT_STREQ(d["items"][2]["kind"].GetString(), "mixed_edges");
      EXPECT_DOUBLE_EQ(d["items"][2]["clock_pin_cap_ff"].GetDouble(), 2);
      EXPECT_STREQ(d["items"][0]["kind"].GetString(), "logic_driven_clock");
      bool found = false;
      for (const auto& domain : d["domains"].GetArray()) {
        if (!domain.HasMember("driver_inputs")) {
          continue;
        }
        found = true;
        EXPECT_STREQ(domain["driver_master"].GetString(), "AND2");
        EXPECT_STREQ(domain["driver_inputs"]["A"]["pin"].GetString(), "clk");
        EXPECT_STREQ(domain["driver_inputs"]["B"]["pin"].GetString(), "en");
      }
      EXPECT_TRUE(found);
      EXPECT_DOUBLE_EQ(d["items"][1]["clock_pin_cap_ff"].GetDouble(), 4);
      EXPECT_TRUE(d["items"][1]["source"].IsNull());
    }
  }
}
TEST(ClockCensus, RelatedEnableFindingDisappearsWhenRegistersShareTheEnable) {
  for (bool shared : {false, true}) {
    Fixture f;
    auto    clock = f.top->get_input_pin("clk"), en = f.top->get_input_pin("en");
    auto    conditional = f.cell("AND2", "conditional");
    en.connect_sink(conditional.create_sink_pin("A"));
    f.top->get_input_pin("extra").connect_sink(conditional.create_sink_pin("B"));
    const auto a = f.gate("base", clock, en);
    const auto b = f.gate("narrow", clock, shared ? en : conditional.create_driver_pin("Y"));
    for (unsigned i = 0; i < 12; ++i) {
      (i < 6 ? a : b).connect_sink(f.cell("FF", std::format("reg{}", i)).create_sink_pin("C"));
    }
    rapidjson::Document d;
    d.Parse(f.analyze().c_str());
    ASSERT_FALSE(d.HasParseError());
    EXPECT_EQ(d["gating_functions"].Size(), shared ? 1u : 2u);
    ASSERT_EQ(d["items"].Size(), shared ? 0u : 1u);
    if (!shared) {
      const auto& issue = d["items"][0];
      EXPECT_STREQ(issue["kind"].GetString(), "related_gating_functions");
      EXPECT_STREQ(issue["related_gate"].GetString(), "base:Y");
      EXPECT_STREQ(issue["pin"].GetString(), "narrow:Y");
      EXPECT_DOUBLE_EQ(issue["clock_pin_cap_ff"].GetDouble(), 12);
    }
  }
}

TEST(ClockCensus, TiedClockAndUnconnectedClockHaveSeparateCoverage) {
  Fixture f;
  gu::create_const(*f.top, *Dlop::create_integer(0)).connect_sink(f.cell("FF", "test_reg").create_sink_pin("C"));
  f.cell("FF", "floating");
  rapidjson::Document d;
  d.Parse(f.analyze().c_str());
  ASSERT_FALSE(d.HasParseError());
  EXPECT_EQ(d["domains"].Size(), 0u);
  EXPECT_EQ(d["constant_source_clock_pins"].GetUint(), 1u);
  EXPECT_EQ(d["unresolved_clock_pins"].GetUint(), 1u);
  EXPECT_EQ(d["pins_without_active_root"].Size(), 2u);
}
TEST(ClockCensus, GateChoicesUseTheRegisterMappersEligibilityRules) {
  Fixture     f;
  std::string library = cells;
  library.replace(library.find("dont_use : true"), std::string_view("dont_use : true").size(), "dont_use : false");
  rapidjson::Document d;
  d.Parse(livehd::pcrs::clock_census(*f.top, library).c_str());
  ASSERT_FALSE(d.HasParseError());
  const auto& gates = d["integrated_gate_library"];
  EXPECT_EQ(gates["dont_use_masters"].GetUint(), 0u);
  ASSERT_EQ(gates["mapper_eligible_masters"].Size(), 1u);
  EXPECT_STREQ(gates["mapper_eligible_masters"][0].GetString(), "GATE");
}
}  // namespace
