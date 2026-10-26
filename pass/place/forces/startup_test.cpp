// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "startup.hpp"

#include "gtest/gtest.h"
namespace tc   = livehd::placement::timeclosure;
namespace late = livehd::placement::late;
namespace {
late::Result paths(double scale = 1) {
  late::Result r;
  r.certified            = true;
  r.achievable_period_ps = 100 * scale;
  r.wns_ps               = 0;
  r.pins.resize(11);
  r.pins[0].name                      = "driver";
  r.pins[0].transitions[0].arrival_ps = 0;
  r.feasible_graph.emplace();
  auto& g               = *r.feasible_graph;
  g.reference_period_ps = 100 * scale;
  g.launch_domains      = {std::nullopt};
  g.vertices.push_back({0, 0, 0, 0, std::nullopt});
  for (size_t i = 1; i <= 10; ++i) {
    const double a                       = (110 - 10 * i) * scale;
    r.pins[i].name                       = "sink" + std::to_string(i);
    r.pins[i].endpoint                   = true;
    r.pins[i].transitions[0].arrival_ps  = a;
    r.pins[i].transitions[0].required_ps = 100 * scale;
    r.pins[i].transitions[0].predecessor = late::Predecessor{0, 0};
    g.vertices.push_back({i, 0, 0, std::nullopt, 100 * scale});
    g.arcs.push_back({0, i, i - 1, a, true});
  }
  return r;
}
TEST(Startup, MeanWirePriorPreservesPinsIdealNetsAndWireCapacitanceAccounting) {
  livehd::physical::Parasitics rc;
  rc.identity = "prior";
  rc.nets     = {
      {"d:Y", {{"d:Y", 1}, {"u:A", 1}, {"v:A", 2}}, {{0, 1, 1}, {0, 2, 2}}, 3},
      {"clk",           {{"clk", 0}, {"u:CLK", 0}},            {{0, 1, 0}}, 0}
  };
  rc.nets[1].edges[0].resistance_ohm = 1e-9;  // Native zero-wire pin contact.
  const auto old                     = rc.nets;
  EXPECT_NEAR(tc::startup_mean_branch_length(rc, {2, .2, 4, .4}), 20. / 3, 1e-12);
  const auto mean = tc::startup_average_parasitics(rc, {2, .2, 4, .4}, 10);
  ASSERT_EQ(mean.nets.size(), 2u);
  EXPECT_EQ(mean.nets[1], old[1]);
  livehd::physical::Parasitics ideal;
  ideal.nets = {old[1]};
  EXPECT_DOUBLE_EQ(tc::startup_mean_branch_length(ideal, {0, 0, 0, 0}), 0);
  EXPECT_EQ(rc.nets, old);
  EXPECT_EQ(mean.nets[0].driver, "d:Y");
  EXPECT_DOUBLE_EQ(mean.nets[0].wire_length_um, 20);
  EXPECT_DOUBLE_EQ(mean.nets[0].nodes[0].capacitance_ff, 3);
  EXPECT_DOUBLE_EQ(mean.nets[0].nodes[1].capacitance_ff, 1.5);
  EXPECT_DOUBLE_EQ(mean.nets[0].nodes[2].capacitance_ff, 1.5);
  EXPECT_DOUBLE_EQ(mean.nets[0].edges[0].resistance_ohm, 30);
  EXPECT_THROW(tc::startup_average_parasitics(rc, {2, .2, 4, .4}, -1), std::invalid_argument);
}
TEST(Startup, SelectsThirtyPercentByPathRankRatherThanSlackWindow) {
  const auto r = tc::startup_path_weights(paths());
  EXPECT_EQ(r.endpoints, 10u);
  EXPECT_EQ(r.selected, 3u);
  ASSERT_EQ(r.weights.pins.size(), 3u);
  for (const auto& n : {"sink1", "sink2", "sink3"}) {
    EXPECT_DOUBLE_EQ(r.weights.pins.at(n).weight, 1);
  }
  EXPECT_FALSE(r.weights.pins.contains("sink4"));
}
TEST(Startup, IsUnitInvariantAndExcludesClockForces) {
  const auto a = tc::startup_path_weights(paths()), b = tc::startup_path_weights(paths(.001));
  ASSERT_EQ(a.weights.pins.size(), b.weights.pins.size());
  for (const auto& [pin, w] : a.weights.pins) {
    EXPECT_DOUBLE_EQ(w.weight, b.weights.pins.at(pin).weight);
  }
  auto clock          = paths();
  clock.pins[0].clock = true;
  EXPECT_TRUE(tc::startup_path_weights(clock).weights.pins.empty());
}
TEST(Startup, RejectsInvalidCoverageOrMissingPredecessors) {
  auto t = paths();
  EXPECT_THROW(tc::startup_path_weights(t, 0), std::invalid_argument);
  t.pins[1].transitions[0].predecessor.reset();
  EXPECT_THROW(tc::startup_path_weights(t), std::invalid_argument);
  t           = paths();
  t.certified = false;
  EXPECT_THROW(tc::startup_path_weights(t), std::invalid_argument);
}
}  // namespace
