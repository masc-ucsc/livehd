// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "prects_networks.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
Prects_library library() {
  const auto path = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "controls.lib";
  std::ofstream(path) << R"(library(test) {
    cell(unary) {
      area: 1;
      pin(i) { direction: input; }
      pin(o) { direction: output; function: "i"; }
    }
    cell(invert) {
      area: 1;
      pin(i) { direction: input; }
      pin(o) { direction: output; function: "!i"; }
    }
    cell(state) {
      area: 2;
      ff(IQ, IQN) { clocked_on: "c"; next_state: "d"; clear: "!r"; }
      pin(c) { direction: input; clock: true; }
      pin(r) { direction: input; }
      pin(d) { direction: input; }
      pin(q) { direction: output; function: "IQ"; }
    }
    cell(gate) {
      area: 2;
      clock_gating_integrated_cell: latch_posedge;
      latch(IQ, IQN) { enable: "!c"; data_in: "e"; }
      pin(c) { direction: input; clock_gate_clock_pin: true; }
      pin(e) { direction: input; clock_gate_enable_pin: true; }
      pin(q) { direction: output; clock_gate_out_pin: true; function: "c & IQ"; }
    }
  })";
  auto roles = prects_library(path.string());
  roles.ports.insert("explicit_sync");
  return roles;
}
}  // namespace

TEST(PrectsNetworks, DistributionClosureExcludesSiblingsButPreservesDataAndGateEnable) {
  auto roles = library();
  ASSERT_TRUE(roles.controls["state"].contains("r"));
  ASSERT_EQ(roles.links["unary"].size(), 1);
  ASSERT_EQ(roles.links["invert"].size(), 1);
  ASSERT_EQ(roles.links["gate"].size(), 1);
  physical::Technology tech;
  physical::Floorplan  fp;
  for (const auto& [name, master] : std::vector<std::pair<std::string, std::string>>{
           {  "u0",  "unary"},
           {  "u1",  "unary"},
           {  "u2", "invert"},
           {  "r0",  "unary"},
           {  "r1", "invert"},
           {  "ff",  "state"},
           { "icg",   "gate"},
           {"data",  "unary"},
           {"sync",  "unary"}
  }) {
    fp.components.push_back({name, master, {}});
  }
  fp.nets = {
      {              "root",     {{"PIN", "unknown_clock"}, {"u0", "i"}}},
      {            "branch",     {{"u0", "o"}, {"u1", "i"}, {"u2", "i"}}},
      {              "leaf",                 {{"u1", "o"}, {"icg", "c"}}},
      {           "sibling", {{"u2", "o"}, {"PIN", "exported_waveform"}}},
      {             "gated",                 {{"icg", "q"}, {"ff", "c"}}},
      {        "reset_root",     {{"PIN", "unknown_reset"}, {"r0", "i"}}},
      {         "reset_mid",                  {{"r0", "o"}, {"r1", "i"}}},
      {        "reset_leaf",                  {{"r1", "o"}, {"ff", "r"}}},
      {"reset_counter_data",   {{"PIN", "reset_counter"}, {"data", "i"}}},
      {          "data_out",                {{"data", "o"}, {"ff", "d"}}},
      {            "enable",           {{"PIN", "enable"}, {"icg", "e"}}},
      {                 "q",            {{"ff", "q"}, {"PIN", "result"}}},
      {         "sync_root",   {{"PIN", "explicit_sync"}, {"sync", "i"}}},
      {         "sync_leaf",        {{"sync", "o"}, {"PIN", "sync_out"}}},
  };
  timing::Feedback timing;
  for (const auto& net : fp.nets) {
    for (const auto& pin : net.connections) {
      timing.pins[pin.instance == "PIN" ? pin.pin : pin.instance + ":" + pin.pin].slack = -100;
    }
  }
  const auto before   = fp;
  const auto excluded = prects_networks(tech, fp, roles, timing);
  EXPECT_EQ(excluded.ignored,
            (std::vector<bool>{true, true, true, true, true, true, true, true, false, false, false, false, true, true}));
  EXPECT_TRUE(excluded.timing_cuts.contains("unknown_reset"));
  EXPECT_TRUE(excluded.timing_cuts.contains("r0:o"));
  EXPECT_TRUE(excluded.timing_cuts.contains("ff:r"));
  EXPECT_TRUE(excluded.timing_cuts.contains("explicit_sync"));
  for (const auto* clock : {"unknown_clock", "u0:o", "u1:o", "u2:o", "icg:q", "ff:c"}) {
    EXPECT_FALSE(excluded.timing_cuts.contains(clock)) << clock;
  }
  EXPECT_FALSE(excluded.timing_cuts.contains("icg:e"));
  const auto data = prects_data_timing(timing, excluded);
  EXPECT_FALSE(data.pins.contains("r0:o"));
  EXPECT_FALSE(data.pins.contains("ff:c"));
  EXPECT_TRUE(data.pins.contains("ff:d"));
  EXPECT_TRUE(data.pins.contains("icg:e"));
  EXPECT_TRUE(data.pins.contains("reset_counter"));
  EXPECT_TRUE(timing.pins.contains("r0:o"));
  EXPECT_EQ(fp.nets, before.nets);
  EXPECT_EQ(fp.components, before.components);
}

TEST(PrectsNetworks, MissingDistributionEndpointsDoNotJoinDataToResetOrClock) {
  auto                 roles = library();
  physical::Technology tech;
  physical::Floorplan  fp;
  fp.components = {
      { "missing_input",  "unary", {}},
      {"missing_output", "invert", {}},
      {      "complete",  "unary", {}},
      {            "ff",  "state", {}}
  };
  fp.nets = {
      {      "data",                         {{"PIN", "data"}, {"ff", "d"}}},
      {     "reset",  {{"PIN", "rst"}, {"missing_input", "o"}, {"ff", "r"}}},
      {     "clock", {{"PIN", "clk"}, {"missing_output", "i"}, {"ff", "c"}}},
      { "branch_in",                 {{"PIN", "branch"}, {"complete", "i"}}},
      {"branch_out",              {{"complete", "o"}, {"PIN", "reset_out"}}}
  };
  roles.reset_ports.insert("reset_out");
  // Duplicate references to an existing endpoint must not create extra links.
  fp.nets[3].connections.push_back({"complete", "i"});
  const auto result = prects_networks(tech, fp, roles);
  EXPECT_EQ(result.ignored, (std::vector<bool>{false, true, true, true, true}));
  EXPECT_FALSE(result.pins.contains("data"));
  EXPECT_FALSE(result.pins.contains("ff:d"));
  EXPECT_FALSE(result.pins.contains("missing_input:i"));
  EXPECT_FALSE(result.pins.contains("missing_output:o"));
  EXPECT_TRUE(result.timing_cuts.contains("missing_input:o"));
  EXPECT_FALSE(result.timing_cuts.contains("missing_output:i"));
  EXPECT_TRUE(result.timing_cuts.contains("branch"));
  EXPECT_TRUE(result.timing_cuts.contains("complete:o"));
}

TEST(PrectsNetworks, LefAndTimingEvidenceNeedNoFiniteSlack) {
  auto                 roles = library();
  physical::Technology tech;
  tech.macros["opaque"].pins["K"].use = "CLOCK";
  physical::Floorplan fp;
  fp.components = {
      {"b",  "unary", {}},
      {"m", "opaque", {}},
      {"t",  "unary", {}}
  };
  fp.nets = {
      {"n0", {{"PIN", "a"}, {"b", "i"}}},
      {"n1",   {{"b", "o"}, {"m", "K"}}},
      {"n2", {{"PIN", "b"}, {"t", "i"}}},
      {"n3", {{"t", "o"}, {"PIN", "c"}}}
  };
  timing::Feedback timing;
  timing.pins["t/o"].clock = true;
  const auto excluded      = prects_networks(tech, fp, roles, timing);
  EXPECT_EQ(excluded.ignored, (std::vector<bool>{true, true, true, true}));
  EXPECT_TRUE(excluded.timing_cuts.empty());
}
TEST(PrectsNetworks, DenseRankingClocksAndFilteringMatchNamedFeedback) {
  auto                 roles = library();
  physical::Technology tech;
  physical::Floorplan  fp;
  fp.components = {
      {"b", "unary", {}},
      {"t", "unary", {}}
  };
  fp.nets = {
      {       "clock", {{"PIN", "clk"}, {"b", "i"}}},
      {"clock_branch",   {{"b", "o"}, {"PIN", "q"}}},
      {        "data",   {{"PIN", "d"}, {"t", "i"}}}
  };
  timing::Feedback named;
  named.pins["b/o"].clock = true;
  named.pins["t:i"].slack = -1;
  timing::Feedback dense;
  dense.ranking_only = true;
  auto layout        = std::make_shared<timing::Feedback::Ranking::Layout>();
  layout->labels     = {"b/o", "t:i"};
  auto& rank         = dense.ranking.emplace();
  rank.layout        = layout;
  rank.pins          = {
      {std::numeric_limits<double>::infinity(),  true},
      {                                     -1, false}
  };
  const auto expected = prects_networks(tech, fp, roles, named);
  const auto actual   = prects_networks(tech, fp, roles, dense);
  EXPECT_EQ(actual.ignored, expected.ignored);
  EXPECT_EQ(actual.pins, expected.pins);
  EXPECT_EQ(actual.timing_cuts, expected.timing_cuts);
  const auto filtered = prects_data_timing(dense, actual);
  EXPECT_FALSE(filtered.is_clock_pin("b", "o"));
  EXPECT_TRUE(dense.is_clock_pin("b", "o"));
  EXPECT_EQ(filtered.pin_rank("t", "i").slack, -1);
}
TEST(PrectsNetworks, ExplicitPortsReplaceOptionalMetadataAndLibraryRoots) {
  auto roles = library();
  roles.pins.insert("registered:q");
  physical::Technology tech;
  physical::Floorplan  fp;
  fp.components = {
      {"registered", "state", {}},
      {      "sink", "state", {}},
      {      "data", "unary", {}}
  };
  fp.nets = {
      {     "clock",      {{"PIN", "clk"}, {"sink", "c"}}},
      {     "reset", {{"registered", "q"}, {"sink", "d"}}},
      {"functional",   {{"PIN", "chosen"}, {"data", "i"}}}
  };
  EXPECT_EQ(prects_networks(tech, fp, roles).ignored, (std::vector<bool>{true, true, false}));
  auto override           = prects_library("", "chosen");
  roles.ports             = override.ports;
  roles.override_controls = override.override_controls;
  timing::Feedback timing;
  timing.pins["sink:c"].clock = true;
  EXPECT_EQ(prects_networks(tech, fp, roles, timing).ignored, (std::vector<bool>{false, false, true}));
  roles.ports               = {"clk"};
  const auto clock_override = prects_networks(tech, fp, roles, timing);
  EXPECT_EQ(clock_override.ignored, (std::vector<bool>{true, false, false}));
  EXPECT_TRUE(clock_override.timing_cuts.empty());  // clock is still needed for launch/capture
}

TEST(PrectsNetworks, ExplicitPortsCannotDisableRecognizedResetFalsePaths) {
  auto roles              = library();
  roles.ports             = {"chosen"};
  roles.override_controls = true;
  ASSERT_TRUE(roles.resets.at("state").contains("r"));
  roles.reset_ports.insert("trigger");
  roles.reset_pins.insert("registered:q");
  physical::Technology tech;
  physical::Floorplan  fp;
  fp.components = {
      {"r0", "unary", {}}, {"r1", "invert", {}}, {"ff", "state", {}}, {"data", "functional_logic", {}}
  };
  fp.nets = {
      {"root", {{"PIN", "unlisted"}, {"r0", "i"}}},
      {"leaf", {{"r0", "o"}, {"r1", "i"}, {"ff", "r"}}},
      {"sibling", {{"r1", "o"}, {"PIN", "out"}}},
      {"sync", {{"registered", "q"}, {"ff", "d"}}},
      {"port", {{"PIN", "trigger"}, {"data", "i"}}},
      {"functional", {{"data", "o"}, {"PIN", "result"}}},
      {"clock", {{"PIN", "clk"}, {"ff", "c"}}},
      {"explicit", {{"PIN", "chosen"}}}
  };
  const auto excluded = prects_networks(tech, fp, roles);
  EXPECT_EQ(excluded.ignored, (std::vector<bool>{true, true, true, true, true, false, false, true}));
  for (const auto* pin : {"unlisted", "r0:o", "ff:r", "out", "registered:q", "ff:d", "trigger", "data:i"}) {
    EXPECT_TRUE(excluded.timing_cuts.contains(pin)) << pin;
  }
  EXPECT_FALSE(excluded.timing_cuts.contains("data:o"));
  EXPECT_FALSE(excluded.timing_cuts.contains("ff:c"));
}

}  // namespace livehd::placement
