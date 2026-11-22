// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_gate.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "clock_tree.hpp"
#include "gtest/gtest.h"

namespace livehd::pcrs {
namespace {
liberty::Cell_library library() {
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "discrete-clock.lib";
  std::ofstream(path) << R"lib(library(test) {
    cell(transparent_high) { area:1; latch(IQ,IQN) { enable:"G"; data_in:"D"; }
      pin(G){direction:input;} pin(D){direction:input;} pin(Q){direction:output;function:"IQ";} }
    cell(transparent_low) { area:1; latch(IQ,IQN) { enable:"!G"; data_in:"D"; }
      pin(G){direction:input;} pin(D){direction:input;} pin(Q){direction:output;function:"IQ";} }
    cell(enable_ff) { area:1; ff(IQ,IQN) { clocked_on:"CK"; next_state:"D"; }
      pin(CK){direction:input;} pin(D){direction:input;} pin(Q){direction:output;function:"IQ";} }
    cell(both) { area:1; pin(A){direction:input;} pin(B){direction:input;} pin(Z){direction:output;function:"A & B";} }
    cell(either) { area:1; pin(A){direction:input;} pin(B){direction:input;} pin(Z){direction:output;function:"A | B";} }
    cell(nand3) { area:1; pin(A){direction:input;} pin(B){direction:input;} pin(C){direction:input;} pin(Z){direction:output;function:"!(A & B & !C)";} }
    cell(negate) { area:1; pin(A){direction:input;} pin(Z){direction:output;function:"!A";} }
  })lib";
  return liberty::Cell_library(path.string());
}

TEST(DiscreteClockGate, RecognizesWideInvertedGateWithMixedInputPolarity) {
  auto                lib = library();
  physical::Floorplan fp;
  fp.components = {
      {"g",           "nand3", {}},
      {"l", "transparent_low", {}}
  };
  fp.nets = {
      { "clk", {{"PIN", "clk"}, {"g", "A"}, {"l", "G"}}},
      {"held",                 {{"l", "Q"}, {"g", "B"}}},
      {"test",            {{"PIN", "test"}, {"g", "C"}}},
      {  "en",              {{"PIN", "en"}, {"l", "D"}}},
      {"gclk",               {{"g", "Z"}, {"PIN", "q"}}}
  };
  const auto paths = discrete_gate_paths(fp, lib, {false, false, false, false, true});
  ASSERT_EQ(paths.gates.size(), 1u);
  EXPECT_EQ(paths.gates[0].clock_pin, "A");
  EXPECT_TRUE(paths.gates[0].inverted_output);
}

TEST(ClockGateMerge, RequiresIdenticalClockEnableAndTestNetsAndPrivateFlopLoads) {
  auto lib = library();
  lib.icgs.push_back({"icg", "CK", "E", "T", "Q", 1});
  liberty::Dff_cell flop{};
  flop.name    = "ff";
  flop.clk_pin = "CK";
  lib.dffs.push_back(flop);
  physical::Floorplan fp;
  fp.components = {
      { "a", "icg", {}},
      { "b", "icg", {}},
      {"ra",  "ff", {}},
      {"rb",  "ff", {}}
  };
  for (auto& cell : fp.components) {
    cell.place.status = physical::Status::PLACED;
  }
  fp.components[1].place.x = 3000;
  fp.nets                  = {
      {  "ck", {{"PIN", "clk"}, {"a", "CK"}, {"b", "CK"}}},
      {  "en",    {{"PIN", "en"}, {"a", "E"}, {"b", "E"}}},
      {"test",  {{"PIN", "test"}, {"a", "T"}, {"b", "T"}}},
      {  "qa",                 {{"a", "Q"}, {"ra", "CK"}}},
      {  "qb",                 {{"b", "Q"}, {"rb", "CK"}}}
  };
  std::vector<bool> clocks{true, false, false, true, true};
  const auto        pairs = gate_merges(fp, lib, clocks);
  ASSERT_EQ(pairs.size(), 2u);
  EXPECT_EQ(pairs[0].keep, "a");
  EXPECT_EQ(pairs[1].keep, "b");
  EXPECT_DOUBLE_EQ(pairs[0].distance_um, 3);
  for (const size_t net : {0u, 1u, 2u}) {
    auto       different = fp;
    const auto pin       = different.nets[net].connections.back();
    different.nets[net].connections.pop_back();
    different.nets.push_back({
        "other",
        {{"PIN", "other"}, pin}
    });
    auto flags = clocks;
    flags.push_back(net == 0);
    EXPECT_TRUE(gate_merges(different, lib, flags).empty());
  }
  fp.nets.back().connections.push_back({"PIN", "observable"});
  EXPECT_TRUE(gate_merges(fp, lib, clocks).empty());
  fp.nets.back().connections.pop_back();
  fp.snapshot_ndr.net_rules["qa"] = "wide";
  EXPECT_TRUE(gate_merges(fp, lib, clocks).empty());
  fp.snapshot_ndr.net_rules.clear();
  fp.components.front().place.status = physical::Status::FIXED;
  EXPECT_TRUE(gate_merges(fp, lib, clocks).empty());
}
TEST(DiscreteClockGate, IdentifiesAnEnableSampledOnTheOppositeClockEdge) {
  auto                lib = library();
  physical::Floorplan fp;
  fp.components = {
      {"control", "enable_ff", {}},
      {    "inv",    "negate", {}},
      {   "gate",      "both", {}}
  };
  fp.nets = {
      {   "clk", {{"PIN", "clk"}, {"gate", "A"}, {"inv", "A"}}},
      { "phase",             {{"inv", "Z"}, {"control", "CK"}}},
      {"enable",             {{"PIN", "en"}, {"control", "D"}}},
      {  "held",             {{"control", "Q"}, {"gate", "B"}}},
      { "gated",             {{"gate", "Z"}, {"PIN", "gated"}}}
  };
  const std::vector<bool> clocks = {true, true, false, false, true};
  auto                    paths  = discrete_gate_paths(fp, lib, clocks);
  ASSERT_EQ(paths.gates.size(), 1u);
  EXPECT_TRUE(paths.gates[0].edge_triggered_enable);
  EXPECT_EQ(paths.gates[0].upstream_net, 0u);
  EXPECT_TRUE(paths.leaf_group.empty());
  EXPECT_TRUE(paths.direct_sinks.empty());
  EXPECT_TRUE(paths.private_phase_targets.empty());
  fp.nets[0].connections.push_back(fp.nets[1].connections.back());
  fp.nets[1].connections.pop_back();
  EXPECT_TRUE(discrete_gate_paths(fp, lib, clocks).gates.empty());
}

TEST(DiscreteClockGate, NestedGatesUseTheLastCommonForkAndKeepEnableDataSeparate) {
  const auto          lib = library();
  physical::Floorplan fp;
  fp.components = {
      {"en0",  "transparent_low", {}},
      {"en1", "transparent_high", {}},
      { "g0",             "both", {}},
      { "g1",             "both", {}},
      {"inv",           "negate", {}},
      {"en2", "transparent_high", {}},
      { "g2",           "either", {}}
  };
  fp.nets = {
      {    "clock", {{"PIN", "clock"}, {"en0", "G"}, {"g0", "A"}, {"en2", "G"}, {"g2", "A"}}},
      {   "enable",            {{"PIN", "enable"}, {"en0", "D"}, {"en1", "D"}, {"en2", "D"}}},
      {    "held0",                                              {{"en0", "Q"}, {"g0", "B"}}},
      {    "outer",                                 {{"g0", "Z"}, {"g1", "A"}, {"inv", "A"}}},
      {"low_phase",                                             {{"inv", "Z"}, {"en1", "G"}}},
      {    "held1",                                              {{"en1", "Q"}, {"g1", "B"}}},
      {    "inner",                                          {{"g1", "Z"}, {"PIN", "clk_o"}}},
      {    "held2",                                              {{"en2", "Q"}, {"g2", "B"}}},
      {"idle_high",                                         {{"g2", "Z"}, {"PIN", "clk_or"}}}
  };
  std::vector<bool> clocks(fp.nets.size());
  clocks[6] = clocks[8] = true;
  auto paths            = discrete_gate_paths(fp, lib, clocks);
  ASSERT_EQ(paths.gates.size(), 3u);
  EXPECT_EQ(paths.gates[0].instance, "g0");
  EXPECT_EQ(paths.gates[1].upstream_net, 3u);
  EXPECT_EQ(paths.gates[1].clock_pin, "A");
  EXPECT_EQ(paths.gates[1].control_pin, "B");
  EXPECT_EQ(paths.gates[1].latch_data, "D");
  EXPECT_EQ(paths.direct_sinks.at(4), std::set<std::string>{"en1:G"});
  EXPECT_EQ(paths.direct_sinks.size(), 1u);
  ASSERT_EQ(paths.private_phase_targets.size(), 1u);
  EXPECT_EQ(paths.private_phase_targets.at("inv"), "en1:G");
  fp.nets[4].connections.push_back({"PIN", "shared_phase"});
  EXPECT_TRUE(discrete_gate_paths(fp, lib, clocks).private_phase_targets.empty());
  fp.nets[4].connections.pop_back();
  EXPECT_EQ(paths.leaf_group.at("inv:A"), paths.leaf_group.at("g1:A"));
  EXPECT_EQ(paths.leaf_group.at("g0:A"), paths.leaf_group.at("en0:G"));
  EXPECT_EQ(paths.leaf_group.at("g2:A"), paths.leaf_group.at("en2:G"));
  EXPECT_FALSE(paths.leaf_group.contains("g0:B"));

  // A second gate with a separate enable latch shares the phase inverter.
  auto shared = fp;
  shared.components.push_back({"g3", "both", {}});
  shared.components.push_back({"en3", "transparent_high", {}});
  shared.nets[1].connections.push_back({"en3", "D"});
  shared.nets[3].connections.push_back({"g3", "A"});
  shared.nets[4].connections.push_back({"en3", "G"});
  shared.nets.push_back({
      "held3",
      {{"en3", "Q"}, {"g3", "B"}}
  });
  shared.nets.push_back({
      "out3",
      {{"g3", "Z"}, {"PIN", "shared_clk"}}
  });
  auto seeded = clocks;
  seeded.resize(shared.nets.size());
  seeded.back()           = true;
  const auto shared_paths = discrete_gate_paths(shared, lib, seeded);
  ASSERT_EQ(shared_paths.gates.size(), 4u);
  EXPECT_EQ(shared_paths.leaf_group.at("g3:A"), shared_paths.leaf_group.at("g1:A"));
  EXPECT_EQ(shared_paths.leaf_group.at("g3:A"), shared_paths.leaf_group.at("inv:A"));
  EXPECT_EQ(shared_paths.direct_sinks.at(4), (std::set<std::string>{"en1:G", "en3:G"}));
  EXPECT_TRUE(shared_paths.private_phase_targets.empty());

  // A latch open in the wrong phase or clocked from a different source is
  // not proof of a glitch-free gate. Clock/control names do not override it.
  fp.components[1].macro = "transparent_low";
  EXPECT_EQ(discrete_gate_paths(fp, lib, clocks).gates.size(), 1u);  // only seeded OR
  fp.components[1].macro = "transparent_high";
  fp.nets[3].connections.pop_back();
  fp.nets[1].connections.push_back({"inv", "A"});
  EXPECT_EQ(discrete_gate_paths(fp, lib, clocks).gates.size(), 1u);
  clocks[8] = false;
  EXPECT_TRUE(discrete_gate_paths(fp, lib, clocks).gates.empty());
}
TEST(DiscreteClockGate, PhaseForkStaysOnOneElectricalLeafEvenAcrossTheSpatialCut) {
  std::vector<Clock_sink> sinks;
  for (int i = 0; i < 18; ++i) {
    sinks.push_back({"pin" + std::to_string(i), double(i), 0, 1});
  }
  sinks.front().coupled_group = sinks.back().coupled_group = "fork";
  Clock_driver buf{"small", "i", "q", 1, 20, 1, 20, 0, 4};
  Clock_limits limits{4, 0.02, 1, 0};
  for (int order = 0; order < 2; ++order) {
    const auto tree   = plan_clock_tree(sinks, {buf}, limits);
    unsigned   leaves = 0;
    for (const auto& branch : tree.branches) {
      if (!branch.leaf) {
        continue;
      }
      const auto n = std::ranges::count_if(branch.children, [&](auto c) { return !sinks[c].coupled_group.empty(); });
      if (n) {
        EXPECT_EQ(n, 2);
        ++leaves;
      }
    }
    EXPECT_EQ(leaves, 1u);
    std::ranges::reverse(sinks);
  }
  limits.radius_um = 1;
  EXPECT_THROW(plan_clock_tree(sinks, {buf}, limits), Clock_infeasible);
}
TEST(DiscreteClockGate, RelocatingOnePhasePinPreservesUnrelatedLeafRegions) {
  std::vector<Clock_sink> sinks;
  for (int i = 0; i < 12; ++i) {
    sinks.push_back({"clk" + std::to_string(i), double(i), 0, 1});
  }
  Clock_driver buffer{"buffer", "A", "Y", 1, 20, 1, 0, 0, 3};
  Clock_limits limits{3, 0, 0, 0};
  const auto   members = [&](const Clock_tree& tree) {
    std::set<std::set<std::string>> groups;
    for (const auto& branch : tree.branches) {
      if (branch.leaf) {
        std::set<std::string> names;
        for (const auto child : branch.children) {
          names.insert(sinks[child].name);
        }
        groups.insert(std::move(names));
      }
    }
    return groups;
  };
  const auto original        = members(plan_clock_tree(sinks, {buffer}, limits));
  sinks[0].previous_position = std::pair{0., 0.};
  sinks[0].x_um              = 10.1;
  sinks[0].coupled_group = sinks[10].coupled_group = "gate";
  const auto tree                                  = plan_clock_tree(sinks, {buffer}, limits);
  EXPECT_TRUE(tree.used_previous_leaf_regions);
  const auto changed = members(tree);
  EXPECT_TRUE(std::ranges::any_of(changed, [](const auto& g) { return g.contains("clk0") && g.contains("clk10"); }));
  for (const auto& region : original) {
    if (!region.contains("clk0") && !region.contains("clk10")) {
      EXPECT_TRUE(changed.contains(region));
    }
  }
  std::ranges::reverse(sinks);
  EXPECT_EQ(changed, members(plan_clock_tree(sinks, {buffer}, limits)));
}
TEST(DiscreteClockGate, RetainedSourcePinsConsumeWireCapacitanceFanoutAndSlewBudget) {
  const std::vector<Clock_sink> sinks{
      {"flop", 2, 0, 1}
  };
  Clock_limits limits{8, 1, 1, 0};
  Clock_driver buf{"buf", "a", "y", 1, 20, 1, 20, 0, 8};
  Clock_source source{0, 0, buf};
  auto         original = plan_clock_tree(sinks, {buf}, limits, source);
  source.direct.push_back({"latch", 0, 3, 2, false, 20});
  auto retained = plan_clock_tree(sinks, {buf}, limits, source);
  EXPECT_DOUBLE_EQ(retained.source_load_ff, original.source_load_ff + 5);
  EXPECT_DOUBLE_EQ(retained.source_wire_um, original.source_wire_um + 3);
  // A separate arm adds load at the source but no wire delay after its output pin.
  EXPECT_NEAR(retained.arrival_ps[0], original.arrival_ps[0], 1e-8);
  auto shared           = source;
  shared.direct[0].x_um = 1;
  shared.direct[0].y_um = 0;
  const auto tapped     = plan_clock_tree(sinks, {buf}, limits, shared);
  EXPECT_DOUBLE_EQ(tapped.source_wire_um, original.source_wire_um);
  EXPECT_NEAR(tapped.arrival_ps[0] - original.arrival_ps[0], 0.001 * 1 * 2, 1e-8);
  auto direct_only = plan_clock_tree({}, {}, limits, source);
  EXPECT_TRUE(direct_only.branches.empty());
  EXPECT_DOUBLE_EQ(direct_only.source_load_ff, 5);
  EXPECT_DOUBLE_EQ(direct_only.source_wire_um, 3);
  EXPECT_GT(direct_only.source_far_slew_ps, 0);
  source.driver.max_fanout = 1;
  EXPECT_THROW(plan_clock_tree(sinks, {buf}, limits, source), Clock_infeasible);
  source.driver.max_fanout     = 8;
  source.direct[0].max_slew_ps = 0.0001;
  EXPECT_THROW(plan_clock_tree({}, {}, limits, source), Clock_infeasible);
  EXPECT_THROW(plan_clock_tree(sinks, {buf}, limits, source), Clock_infeasible);
}
}  // namespace
}  // namespace livehd::pcrs
