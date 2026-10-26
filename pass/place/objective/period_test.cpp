// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "period.hpp"

#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "gtest/gtest.h"

namespace tc = livehd::placement::timeclosure;
namespace {
tc::Objective_network fork_fixture() {
  return {
      .vertices         = 5,
      .reference_period = 1000,
      .arcs             = {{0, 1, 3000}, {1, 2, 3000}, {0, 3, 5800}, {0, 4, 1000}},
      .boundaries       = {{0, 0}},
      .endpoints        = {{2, 1000}, {3, 1000}, {4, 1000}}
  };
}
void conserved(const tc::Objective_network& graph, const tc::Period_objective& result) {
  EXPECT_NEAR(std::accumulate(result.endpoint_mass.begin(), result.endpoint_mass.end(), 0.), 1, 1e-12);
  EXPECT_NEAR(std::accumulate(result.boundary_mass.begin(), result.boundary_mass.end(), 0.), 1, 1e-12);
  for (size_t v = 0; v < graph.vertices; ++v) {
    double incoming = 0, outgoing = 0;
    for (size_t i = 0; i < graph.arcs.size(); ++i) {
      if (graph.arcs[i].to == v) {
        incoming += result.arc_mass[i];
      }
      if (graph.arcs[i].from == v) {
        outgoing += result.arc_mass[i];
      }
      EXPECT_GE(result.arc_mass[i], 0);
    }
    for (size_t i = 0; i < graph.boundaries.size(); ++i) {
      if (graph.boundaries[i].vertex == v) {
        incoming += result.boundary_mass[i];
      }
    }
    for (size_t i = 0; i < graph.endpoints.size(); ++i) {
      if (graph.endpoints[i].vertex == v) {
        outgoing += result.endpoint_mass[i];
      }
    }
    EXPECT_NEAR(incoming, result.vertex_mass[v], 1e-12) << v;
    EXPECT_NEAR(outgoing, result.vertex_mass[v], 1e-12) << v;
  }
}
}  // namespace

TEST(PeriodObjective, InfeasibleRequestConcentratesOnWorstPathsAndConservesFlow) {
  const auto graph = fork_fixture();
  const auto r     = tc::period_objective(graph);
  EXPECT_DOUBLE_EQ(r.achievable_period, 6000);
  EXPECT_DOUBLE_EQ(r.fixed_wns, -5000);
  EXPECT_DOUBLE_EQ(r.optimization_period, 5400);
  EXPECT_EQ(r.in_window, (std::vector<bool>{true, true, false}));
  EXPECT_GT(r.endpoint_mass[0], .8);
  EXPECT_GT(r.endpoint_mass[1], 0);
  EXPECT_EQ(r.endpoint_mass[2], 0);
  EXPECT_DOUBLE_EQ(r.arc_mass[0], r.arc_mass[1]);
  EXPECT_EQ(r.arc_mass[3], 0);
  conserved(graph, r);
}
TEST(PeriodObjective, RequirementsAndLaunchOffsetsDeterminePeriodRatherThanMaximumArrival) {
  tc::Objective_network graph{
      .vertices         = 4,
      .reference_period = 1000,
      .arcs             = {{0, 2, 900}, {1, 3, 700}},
      .boundaries       = {   {0, 100},    {1, 200}},
      .endpoints        = {  {2, 1500},    {3, 800}}
  };
  const auto r = tc::period_objective(graph);
  EXPECT_DOUBLE_EQ(r.arrivals[2], 1000);
  EXPECT_DOUBLE_EQ(r.arrivals[3], 900);
  EXPECT_DOUBLE_EQ(r.achievable_period, 1100);
  EXPECT_DOUBLE_EQ(r.fixed_wns, -100);
  EXPECT_EQ(r.endpoint_mass[0], 0);
  EXPECT_EQ(r.endpoint_mass[1], 1);
  conserved(graph, r);
}
TEST(PeriodObjective, ReconvergentParallelPathsMatchAnEnumeratedPathOracleAndDerivatives) {
  tc::Objective_network graph{
      .vertices         = 4,
      .reference_period = 1,
      .arcs             = {{0, 1, 2}, {0, 2, 3}, {1, 3, 4}, {2, 3, 2}, {0, 3, 5.5}},
      .boundaries       = {{0, 1}},
      .endpoints        = {{3, 1}}
  };
  const auto   r   = tc::period_objective(graph, {.window_fraction = .1, .temperature_fraction = .05});
  const double tau = r.temperature;
  const double a = std::exp((6. - 6.5) / tau), b = std::exp((7. - 6.5) / tau), c = 1;
  const double total = a + b + c;
  EXPECT_NEAR(r.soft_period, 6.5 + tau * std::log(total), 1e-12);
  EXPECT_NEAR(r.arc_mass[0], b / total, 1e-12);
  EXPECT_NEAR(r.arc_mass[1], a / total, 1e-12);
  EXPECT_NEAR(r.arc_mass[4], c / total, 1e-12);
  conserved(graph, r);
  for (size_t i = 0; i < graph.arcs.size(); ++i) {
    auto high = graph, low = graph;
    high.arcs[i].delay  += 1e-5;
    low.arcs[i].delay   -= 1e-5;
    const auto hi        = tc::period_objective(high, {.window_fraction = .1, .temperature_fraction = .05});
    const auto lo        = tc::period_objective(low, {.window_fraction = .1, .temperature_fraction = .05});
    // T* also sets tau. Hold tau fixed by changing its dimensionless fraction.
    const auto fixed_hi  = tc::period_objective(
        high,
        {.window_fraction = .1, .temperature_fraction = tau / std::max(graph.reference_period, hi.achievable_period)});
    const auto fixed_lo = tc::period_objective(
        low,
        {.window_fraction = .1, .temperature_fraction = tau / std::max(graph.reference_period, lo.achievable_period)});
    EXPECT_NEAR((fixed_hi.soft_period - fixed_lo.soft_period) / 2e-5, r.arc_mass[i], 1e-8);
  }
}
TEST(PeriodObjective, UnitsDoNotChangeMultipliersOrWindowAndRanksCanSwap) {
  auto       graph        = fork_fixture();
  const auto ps           = tc::period_objective(graph);
  graph.reference_period *= .001;
  for (auto& a : graph.arcs) {
    a.delay *= .001;
  }
  for (auto& b : graph.boundaries) {
    b.arrival *= .001;
  }
  for (auto& e : graph.endpoints) {
    e.required *= .001;
  }
  const auto ns = tc::period_objective(graph);
  EXPECT_EQ(ps.in_window, ns.in_window);
  EXPECT_NEAR(ps.achievable_period, ns.achievable_period * 1000, 1e-9);
  EXPECT_NEAR(ps.soft_period, ns.soft_period * 1000, 1e-9);
  for (size_t i = 0; i < ps.arc_mass.size(); ++i) {
    EXPECT_NEAR(ps.arc_mass[i], ns.arc_mass[i], 1e-12);
  }
  graph.arcs[2].delay = 6.2;
  const auto swapped  = tc::period_objective(graph);
  EXPECT_GT(swapped.endpoint_mass[1], swapped.endpoint_mass[0]);
  conserved(graph, swapped);
}
TEST(PeriodObjective, ZeroDelayAndZeroAchievablePeriodRetainFiniteNormalizedFlow) {
  tc::Objective_network graph{.vertices         = 2,
                              .reference_period = 1000,
                              .arcs             = {{0, 1, 0}},
                              .boundaries       = {{0, 0}},
                              .endpoints        = {{1, 1000}}};
  auto                  r = tc::period_objective(graph);
  EXPECT_EQ(r.achievable_period, 0);
  EXPECT_EQ(r.soft_period, 0);
  EXPECT_EQ(r.optimization_period, 0);
  conserved(graph, r);
  graph.endpoints[0].required = 2000;
  r                           = tc::period_objective(graph);
  EXPECT_EQ(r.achievable_period, 0);
  EXPECT_EQ(r.fixed_wns, 2000);
  conserved(graph, r);
}
TEST(PeriodObjective, LargeOffsetsAndTinyTemperaturePreserveMassForTiedPaths) {
  tc::Objective_network graph{
      .vertices         = 3,
      .reference_period = 1,
      .arcs             = {{0, 2, 0}, {1, 2, 0}},
      .boundaries       = {{0, 1e16}, {1, 1e16}},
      .endpoints        = {{2, 1}}
  };
  const auto r = tc::period_objective(graph, {.window_fraction = .1, .temperature_fraction = 1e-20});
  EXPECT_TRUE(std::isfinite(r.soft_period));
  EXPECT_DOUBLE_EQ(r.arc_mass[0], .5);
  EXPECT_DOUBLE_EQ(r.arc_mass[1], .5);
  conserved(graph, r);
}
TEST(PeriodObjective, BoundaryAndEndpointAtInternalVerticesConserveFlow) {
  tc::Objective_network graph{
      .vertices         = 3,
      .reference_period = 10,
      .arcs             = {{0, 1, 2}, {1, 2, 1}},
      .boundaries       = {   {0, 1},    {1, 3}},
      .endpoints        = {   {1, 9},   {2, 10}}
  };
  const auto r = tc::period_objective(graph);
  conserved(graph, r);
  EXPECT_GT(r.boundary_mass[1], 0);
  EXPECT_GT(r.endpoint_mass[0], 0);
}
TEST(PeriodObjective, InvalidOrUnconstrainedNetworksCannotInventZeroDelayPaths) {
  auto graph = fork_fixture();
  graph.endpoints.clear();
  EXPECT_THROW(tc::period_objective(graph), std::invalid_argument);
  graph = fork_fixture();
  graph.boundaries.clear();
  EXPECT_THROW(tc::period_objective(graph), std::invalid_argument);
  graph = fork_fixture();
  graph.arcs.push_back({2, 0, 1});
  EXPECT_THROW(tc::period_objective(graph), std::invalid_argument);
  graph            = fork_fixture();
  graph.arcs[0].to = 99;
  EXPECT_THROW(tc::period_objective(graph), std::invalid_argument);
  graph               = fork_fixture();
  graph.arcs[0].delay = std::numeric_limits<double>::infinity();
  EXPECT_THROW(tc::period_objective(graph), std::invalid_argument);
  graph = fork_fixture();
  graph.endpoints.push_back(graph.endpoints[0]);
  EXPECT_THROW(tc::period_objective(graph), std::invalid_argument);
  graph = fork_fixture();
  graph.boundaries.push_back(graph.boundaries[0]);
  EXPECT_THROW(tc::period_objective(graph), std::invalid_argument);
  graph = fork_fixture();
  EXPECT_THROW(tc::period_objective(graph, {.window_fraction = 1}), std::invalid_argument);
  EXPECT_THROW(tc::period_objective(graph, {.temperature_fraction = 0}), std::invalid_argument);
}
TEST(PeriodObjective, SignedCharacterizedDelaysDoNotBecomeZeroDelayBoundaries) {
  tc::Objective_network graph{.vertices         = 2,
                              .reference_period = 10,
                              .arcs             = {{0, 1, -2}},
                              .boundaries       = {{0, 10}},
                              .endpoints        = {{1, 10}}};
  const auto            r = tc::period_objective(graph);
  EXPECT_DOUBLE_EQ(r.arrivals[1], 8);
  EXPECT_DOUBLE_EQ(r.achievable_period, 8);
  EXPECT_DOUBLE_EQ(r.fixed_wns, 2);
  conserved(graph, r);
}
TEST(PeriodGuard, ImprovingSoftWindowCannotAcceptAWorseWorstPeriod) {
  auto       baseline_graph  = fork_fixture();
  const auto baseline        = tc::period_objective(baseline_graph);
  auto       trial_graph     = baseline_graph;
  trial_graph.arcs[1].delay += 1;
  trial_graph.arcs[2].delay  = 5401;
  const auto trial           = tc::period_objective(trial_graph);
  ASSERT_LT(trial.soft_period, baseline.soft_period);
  ASSERT_GT(trial.achievable_period, baseline.achievable_period);
  tc::Period_guard    guard;
  tc::Objective_epoch epoch{.topology = 1, .timing_model = 1, .calibration = 1};
  guard.rebase(baseline.achievable_period, epoch, true);
  EXPECT_EQ(guard.consider(trial.achievable_period, epoch, true), tc::Period_decision::regression);
  EXPECT_EQ(guard.best_period(), 6000);
  EXPECT_EQ(guard.temperature_fraction(), .01);
  EXPECT_EQ(guard.consider(5900, epoch, true), tc::Period_decision::improve);
  EXPECT_EQ(guard.best_period(), 5900);
}
TEST(PeriodGuard, RepeatedNoiseDoesNotDriftRetainedBestAndTemperatureIsBounded) {
  tc::Period_guard guard({.repeatability_noise = .01, .regression_tolerance = .02});
  guard.rebase(6, {}, true);
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(guard.consider(6.015, {}, true), tc::Period_decision::within_noise);
  }
  EXPECT_EQ(guard.best_period(), 6);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(guard.consider(6.1, {}, true), tc::Period_decision::regression);
  }
  EXPECT_EQ(guard.temperature_fraction(), 1e-5);
}
TEST(PeriodGuard, EpochCoverageAndLegalityChangesRequireRetainedStateReevaluation) {
  tc::Period_guard    guard;
  tc::Objective_epoch epoch{1, 2, 3, 4, true};
  guard.rebase(6, epoch, true);
  auto changed = epoch;
  changed.calibration++;
  EXPECT_EQ(guard.consider(5, changed, true), tc::Period_decision::incomparable);
  changed       = epoch;
  changed.legal = false;
  EXPECT_EQ(guard.consider(5, changed, true), tc::Period_decision::incomparable);
  changed                               = epoch;
  changed.characterized_boundary_policy = true;
  EXPECT_EQ(guard.consider(5, changed, true), tc::Period_decision::incomparable);
  changed                         = epoch;
  changed.bounded_boundary_policy = true;
  EXPECT_EQ(guard.consider(5, changed, true), tc::Period_decision::incomparable);
  EXPECT_EQ(guard.consider(5, epoch, false), tc::Period_decision::unsupported);
  EXPECT_EQ(guard.best_period(), 6);
  EXPECT_EQ(guard.temperature_fraction(), .02);
  guard.rebase(6.2, changed, true);
  EXPECT_EQ(guard.consider(6, changed, true), tc::Period_decision::improve);
  EXPECT_THROW(guard.rebase(6, epoch, false), std::invalid_argument);
  EXPECT_THROW(tc::Period_guard({.repeatability_noise = .02, .regression_tolerance = .02}), std::invalid_argument);
}
