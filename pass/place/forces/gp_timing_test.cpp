// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gp_timing.hpp"

#include <limits>

#include "gtest/gtest.h"
#include "node_util.hpp"

namespace tc    = livehd::placement::timeclosure;
namespace late  = livehd::placement::late;
namespace model = livehd::liberty::timing;
namespace {
struct Circuit {
  hhds::GraphLibrary                               owner;
  std::shared_ptr<hhds::Graph>                     graph;
  tc::Gp_timing_settings                           settings;
  livehd::physical::Parasitics                     rc;
  std::map<std::string, std::pair<double, double>> locations{
      {  "a",    {0, 0}},
      {"u:A", {1000, 0}},
      {"u:Y", {2000, 0}},
      {  "z", {3000, 0}}
  };
  Circuit(bool tied = false) {
    const std::string text = R"(library(test){time_unit:"1ps";capacitive_load_unit(1,ff);default_input_pin_cap:1;
cell(BUF){pin(A){direction:input;capacitance:2;}pin(Y){direction:output;function:A;
timing(){related_pin:A;timing_sense:positive_unate;
cell_rise(scalar){values("5");}cell_fall(scalar){values(")"
                             + std::string(tied ? "5" : "7") + R"(");}
rise_transition(scalar){values("1");}fall_transition(scalar){values("1");}}}}})";
    settings.models = std::make_shared<model::Library>(model::parse(text));
    auto io         = owner.create_io("gp_session");
    io->add_input("a", 0);
    io->add_input("clk", 1);
    io->add_output("z", 0);
    for (const auto& name : {"a", "clk", "z"}) {
      io->set_bits(name, 1);
    }
    graph    = io->create_graph();
    auto buf = owner.create_io("BUF");
    buf->add_input("A", 0);
    buf->add_output("Y", 0);
    buf->set_bits("A", 1);
    buf->set_bits("Y", 1);
    auto node = livehd::graph_util::create_typed_node(*graph, Ntype_op::Sub);
    node.set_name("u");
    node.set_subnode(buf);
    node.create_sink_pin("A").connect_driver(graph->get_input_pin("a"));
    graph->get_output_pin("z").connect_driver(node.create_driver_pin("Y"));
    settings.constraints.clocks["main"] = {.period_ps = 100, .roots = {"clk"}};
    settings.constraints.inputs["a"]    = {
        {10, 10},
        { 1,  1},
        std::nullopt,
        "main"
    };
    settings.constraints.outputs["z"] = {
        {100, 100},
        {  2,   2},
        "main"
    };
    settings.constraints.complete = true;
    rc.nets                       = {
        {  "a", {{"a", 0}, {"u:A", 12}}, {{0, 1, 1000}}},
        {"u:Y", {{"u:Y", 0}, {"z", 14}}, {{0, 1, 1000}}}
    };
  }
  livehd::placement::Rc_policy policy() {
    return {
        {1000, 12, 1000, 12},
        1000,
        "analytic_test"
    };
  }
};
}  // namespace
TEST(GpTiming, NativeContinuousWeightsMatchReferenceAndReuseUnchangedGeometry) {
  Circuit               c;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  const auto            first = session.update(c.rc, c.locations, 1, .5);
  EXPECT_FALSE(first.weights.pins.empty());
  EXPECT_GT(first.reference_period_ps.value(), 20);
  EXPECT_NEAR(first.reference_period_ps.value(), first.continuous_period_ps.value(), 1e-9);
  const auto next = session.update(c.rc, c.locations, 2, .45);
  EXPECT_EQ(next.reference_period_ps, first.reference_period_ps);
  EXPECT_GT(next.calibration.reused, 0);
  EXPECT_EQ(next.decision, tc::Period_decision::within_noise);
  EXPECT_THROW(session.update(c.rc, c.locations, 2, .4), std::invalid_argument);
}

TEST(GpTiming, CharacterizedBoundaryPolicyIsExplicitAndSharedByEveryTimingGate) {
  Circuit c;
  auto    models = std::make_shared<model::Library>(*c.settings.models);
  auto&   arc    = models->cells.at("BUF").pins.at("Y").arcs[0];
  for (unsigned t = 0; t < 2; ++t) {
    const double intercept = t == 0 ? 5 : 7;
    arc.delay[t]           = model::Table{
        model::Variable::output_load,
        model::Variable::none,
                  {0, 20},
                  {},
                  {intercept, intercept + 40}
    };
    arc.slew[t] = model::Table{model::Variable::none, model::Variable::none, {}, {}, {0}};
  }
  c.settings.models                    = models;
  c.rc.nets[1].nodes[0].capacitance_ff = 2;
  tc::Gp_timing_session strict(*c.graph, c.settings, c.policy());
  EXPECT_THROW(strict.set_legal_reference(c.rc, true), std::invalid_argument);
  EXPECT_THROW(strict.update(c.rc, c.locations, 10, .05), std::invalid_argument);
  c.settings.driver.characterized_boundary = true;
  tc::Gp_timing_session boundary(*c.graph, c.settings, c.policy());
  const double          reference = boundary.set_legal_reference(c.rc, true);
  const auto            first     = boundary.update(c.rc, c.locations, 10, .05);
  EXPECT_EQ(first.reference_period_ps, reference);
  EXPECT_NEAR(first.continuous_period_ps.value(), reference, 1e-8);
  EXPECT_FALSE(first.weights.pins.empty());
  EXPECT_FALSE(boundary.check_legal_output(c.rc, true).passed());
  boundary.update(c.rc, c.locations, 20, .05);
  EXPECT_TRUE(boundary.check_legal_output(c.rc, true).passed());
}
TEST(GpTiming, WorseHardPeriodIsRejectedAndFailedTrialLeavesSessionUnchanged) {
  Circuit c;
  c.settings.correction_max_age = 1;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy()), control(*c.graph, c.settings, c.policy());
  session.update(c.rc, c.locations, 1, .5);
  control.update(c.rc, c.locations, 1, .5);
  auto worse = c.rc;
  for (auto& net : worse.nets) {
    for (auto& edge : net.edges) {
      edge.resistance_ohm *= 2;
    }
  }
  EXPECT_THROW(session.update(worse, c.locations, 2, .5), std::invalid_argument);
  const auto actual = session.update(c.rc, c.locations, 2, .5), expected = control.update(c.rc, c.locations, 2, .5);
  EXPECT_EQ(actual.reference_period_ps, expected.reference_period_ps);
  ASSERT_EQ(actual.weights.pins.size(), expected.weights.pins.size());
  for (const auto& [name, w] : actual.weights.pins) {
    EXPECT_EQ(w.weight, expected.weights.pins.at(name).weight);
  }
}
TEST(GpTiming, MissingConstraintCoverageAndUnrankedTimingCannotEnableForces) {
  Circuit c;
  c.settings.constraints.complete = false;
  EXPECT_THROW(tc::Gp_timing_session(*c.graph, c.settings, c.policy()), std::invalid_argument);
  c.settings.constraints.complete = true;
  c.settings.models.reset();
  EXPECT_THROW(tc::Gp_timing_session(*c.graph, c.settings, c.policy()), std::invalid_argument);
  Circuit               ties(true);
  tc::Gp_timing_session tied(*ties.graph, ties.settings, ties.policy());
  EXPECT_THROW(tied.update(ties.rc, ties.locations, 1, .5), std::invalid_argument);
}

TEST(GpTiming, ComparisonRejectionObserverCannotAcceptOrCommitTheProposedState) {
  Circuit c;
  c.settings.correction_max_age = 1000;
  c.settings.wire.max_pin_displacement_fraction.reset();
  c.settings.wire.branch_relative_displacement = false;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy()), control(*c.graph, c.settings, c.policy());
  session.update(c.rc, c.locations, 1, .5);
  control.update(c.rc, c.locations, 1, .5);
  auto moved             = c.locations;
  moved.at("u:A").first += 1000;
  unsigned   calls       = 0;
  const auto observer    = [&](const tc::Gp_timing_rejection& rejected) {
    ++calls;
    EXPECT_TRUE(rejected.reference.certified);
    EXPECT_TRUE(rejected.continuous.certified);
    EXPECT_FALSE(rejected.comparison.comparison_gates_pass);
    EXPECT_GT(rejected.star.stats.reused, 0);
    EXPECT_EQ(rejected.star.locations, moved);
  };
  EXPECT_THROW(session.update(c.rc, moved, 2, .5, observer), std::invalid_argument);
  ASSERT_EQ(calls, 1);
  const auto actual = session.update(c.rc, c.locations, 2, .5), expected = control.update(c.rc, c.locations, 2, .5);
  EXPECT_EQ(actual.reference_period_ps, expected.reference_period_ps);
  EXPECT_EQ(actual.temperature_fraction, expected.temperature_fraction);
  EXPECT_EQ(actual.calibration.reused, expected.calibration.reused);
  ASSERT_EQ(actual.weights.pins.size(), expected.weights.pins.size());
  for (const auto& [name, weight] : actual.weights.pins) {
    EXPECT_EQ(weight.weight, expected.weights.pins.at(name).weight);
  }
}

TEST(GpTiming, UnavailableRankingIsObservedWithoutInventingAPassingRank) {
  Circuit               c(true);
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  unsigned              calls    = 0;
  const auto            observer = [&](const tc::Gp_timing_rejection& rejected) {
    ++calls;
    EXPECT_TRUE(rejected.comparison.numerical_gates_pass);
    EXPECT_FALSE(rejected.comparison.ranking_gates_pass);
    EXPECT_FALSE(rejected.comparison.ranking.tau_b.has_value());
    EXPECT_FALSE(rejected.comparison.ranking.unavailable.empty());
    throw std::runtime_error("diagnostic sink failure");
  };
  EXPECT_THROW(session.update(c.rc, c.locations, 1, .5, observer), std::runtime_error);
  EXPECT_EQ(calls, 1);
  EXPECT_THROW(session.update(c.rc, c.locations, 1, .5), std::invalid_argument);
}

TEST(GpTiming, OptInRecalibrationRetriesOnceAndRechecksTheOriginalGates) {
  Circuit c;
  c.settings.correction_max_age = 1000;
  c.settings.wire.max_pin_displacement_fraction.reset();
  c.settings.wire.branch_relative_displacement = false;
  c.settings.recalibrate_on_mismatch           = true;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  const auto            first  = session.update(c.rc, c.locations, 1, .5);
  auto                  moved  = c.locations;
  moved.at("u:A").first       += 1000;
  unsigned   calls             = 0;
  const auto next              = session.update(c.rc, moved, 2, .5, [&](const auto& rejected) {
    ++calls;
    EXPECT_FALSE(rejected.comparison.numerical_gates_pass);
    EXPECT_TRUE(rejected.retry_planned);
    EXPECT_EQ(rejected.attempt, 0);
  });
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(next.recalibrated_after_mismatch);
  EXPECT_EQ(next.calibration.reused, 0);
  EXPECT_EQ(next.calibration.calibrated, 2);
  EXPECT_NEAR(*next.reference_period_ps, *next.continuous_period_ps, 1e-8);
  EXPECT_EQ(next.reference_period_ps, first.reference_period_ps);
  EXPECT_FALSE(next.rollback);
}

TEST(GpTiming, RecalibrationCannotWaiveUnrankedTimingAndDoesNotAdvanceAFailedIteration) {
  Circuit c(true);
  c.settings.recalibrate_on_mismatch = true;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  std::vector<unsigned> attempts;
  const auto            observer = [&](const auto& rejected) {
    attempts.push_back(rejected.attempt);
    EXPECT_FALSE(rejected.comparison.ranking_gates_pass);
    EXPECT_EQ(rejected.retry_planned, rejected.attempt == 0);
  };
  EXPECT_THROW(session.update(c.rc, c.locations, 1, .5, observer), std::invalid_argument);
  EXPECT_EQ(attempts, (std::vector<unsigned>{0, 1}));
  attempts.clear();
  EXPECT_THROW(session.update(c.rc, c.locations, 1, .5, observer), std::invalid_argument);
  EXPECT_EQ(attempts, (std::vector<unsigned>{0, 1}));
}

TEST(GpTiming, ARecalibratedComparisonCannotWaiveTheHardPeriodGuardOrCommitARolledBackAnchor) {
  Circuit c;
  c.settings.correction_max_age = 1000;
  c.settings.wire.max_pin_displacement_fraction.reset();
  c.settings.wire.branch_relative_displacement = false;
  c.settings.recalibrate_on_mismatch           = true;
  c.settings.continuing_rollback               = true;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  const auto            first = session.update(c.rc, c.locations, 1, .5);
  auto                  worse = c.rc;
  for (auto& net : worse.nets) {
    for (auto& edge : net.edges) {
      edge.resistance_ohm *= 2;
    }
  }
  auto moved             = c.locations;
  moved.at("u:A").first += 1000;
  const auto rejected    = session.update(worse, moved, 2, .5);
  EXPECT_TRUE(rejected.recalibrated_after_mismatch);
  EXPECT_TRUE(rejected.rollback);
  EXPECT_GT(rejected.reference_period_ps, first.reference_period_ps);
  EXPECT_EQ(rejected.retained_period_ps, first.retained_period_ps);
  const auto restored = session.update(c.rc, c.locations, 3, .5);
  EXPECT_FALSE(restored.recalibrated_after_mismatch);
  EXPECT_GT(restored.calibration.reused, 0);
  EXPECT_EQ(restored.reference_period_ps, first.reference_period_ps);
}

TEST(GpTiming, ContinuingRegressionRetainsTimingHistoryAndSpendsIteration) {
  Circuit c;
  c.settings.correction_max_age  = 1;
  c.settings.continuing_rollback = true;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  const auto            accepted = session.update(c.rc, c.locations, 1, .5);
  auto                  worse    = c.rc;
  for (auto& net : worse.nets) {
    for (auto& edge : net.edges) {
      edge.resistance_ohm *= 2;
    }
  }
  const auto rejected = session.update(worse, c.locations, 2, .5);
  EXPECT_TRUE(rejected.rollback);
  EXPECT_EQ(rejected.decision, tc::Period_decision::regression);
  EXPECT_GT(rejected.reference_period_ps, accepted.reference_period_ps);
  EXPECT_EQ(rejected.retained_period_ps, accepted.reference_period_ps.value());
  EXPECT_LT(rejected.temperature_fraction, accepted.temperature_fraction);
  EXPECT_TRUE(rejected.weights.pins.empty());
  EXPECT_THROW(session.update(c.rc, c.locations, 2, .5), std::invalid_argument);
  const auto resumed = session.update(c.rc, c.locations, 3, .5);
  EXPECT_FALSE(resumed.rollback);
  EXPECT_EQ(resumed.reference_period_ps, accepted.reference_period_ps);
  EXPECT_EQ(resumed.temperature_fraction, rejected.temperature_fraction);
  EXPECT_EQ(resumed.decision, tc::Period_decision::within_noise);
  EXPECT_FALSE(resumed.weights.epoch_reset);
  EXPECT_EQ(resumed.weights.pins.size(), accepted.weights.pins.size());
}

TEST(GpTiming, DensityDefersBeforeTimingAndCannotRetainAnIncompatibleImprovement) {
  Circuit c;
  c.settings.continuing_rollback      = true;
  c.settings.maximum_density_overflow = .1;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  const auto            deferred = session.update(c.rc, c.locations, 1, .3);
  EXPECT_TRUE(deferred.deferred);
  EXPECT_FALSE(deferred.rollback);
  EXPECT_FALSE(deferred.reference_period_ps);
  EXPECT_TRUE(deferred.weights.pins.empty());
  EXPECT_THROW(session.update(c.rc, c.locations, 1, .05), std::invalid_argument);
  const auto accepted = session.update(c.rc, c.locations, 2, .08);
  auto       better   = c.rc;
  for (auto& net : better.nets) {
    for (auto& edge : net.edges) {
      edge.resistance_ohm *= .5;
    }
  }
  const auto rejected = session.update(better, c.locations, 3, .12);
  EXPECT_TRUE(rejected.rollback);
  EXPECT_FALSE(rejected.deferred);
  EXPECT_EQ(rejected.reason, "density_overflow");
  EXPECT_FALSE(rejected.reference_period_ps);
  EXPECT_EQ(rejected.retained_period_ps, accepted.reference_period_ps.value());
  const auto resumed = session.update(c.rc, c.locations, 4, .09);
  EXPECT_FALSE(resumed.rollback);
  EXPECT_EQ(resumed.reference_period_ps, accepted.reference_period_ps);
  EXPECT_FALSE(resumed.weights.epoch_reset);
}
TEST(GpTiming, DensityBoundAbortAndValidationAreExplicit) {
  Circuit c;
  c.settings.maximum_density_overflow = -1;
  EXPECT_THROW(tc::Gp_timing_session(*c.graph, c.settings, c.policy()), std::invalid_argument);
  c.settings.maximum_density_overflow = .1;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  session.update(c.rc, c.locations, 1, .05);
  EXPECT_THROW(session.update(c.rc, c.locations, 2, .11), std::invalid_argument);
  EXPECT_FALSE(session.update(c.rc, c.locations, 2, .05).rollback);
}
TEST(GpTiming, LegalOutputUsesFixedLegalReferenceRatherThanTheOverlappingGpGuard) {
  Circuit               c;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  const auto            baseline = session.set_legal_reference(c.rc, true);
  EXPECT_THROW(session.set_legal_reference(c.rc, true), std::invalid_argument);
  auto worse = c.rc;
  for (auto& net : worse.nets) {
    for (auto& edge : net.edges) {
      edge.resistance_ohm *= 2;
    }
  }
  // GP starts in a worse overlapping regime; its guard is independently valid.
  session.update(worse, c.locations, 10, .5);
  session.update(worse, c.locations, 20, .4);
  const auto final = session.check_legal_output(worse, true);
  EXPECT_EQ(final.decision, tc::Period_decision::regression);
  EXPECT_FALSE(final.passed());
  EXPECT_EQ(final.reference_period_ps.value(), baseline);
  const auto unchanged = session.check_legal_output(c.rc, true);
  EXPECT_TRUE(unchanged.passed());
  EXPECT_EQ(unchanged.reference_period_ps.value(), baseline);
  EXPECT_FALSE(session.check_legal_output(c.rc, false).passed());
  // Final observations never move the reference or alter the GP budget/history.
  EXPECT_EQ(session.check_legal_output(worse, true).reference_period_ps.value(), baseline);
}
TEST(GpTiming, LegalReferenceAndResponseCoverageCannotBeInventedAtDelivery) {
  Circuit               c;
  tc::Gp_timing_session missing(*c.graph, c.settings, c.policy());
  EXPECT_FALSE(missing.check_legal_output(c.rc, true).passed());
  EXPECT_THROW(missing.set_legal_reference(c.rc, false), std::invalid_argument);
  missing.update(c.rc, c.locations, 1, .1);
  EXPECT_THROW(missing.set_legal_reference(c.rc, true), std::invalid_argument);
  tc::Gp_timing_session response(*c.graph, c.settings, c.policy());
  response.set_legal_reference(c.rc, true);
  response.update(c.rc, c.locations, 10, .1);
  EXPECT_EQ(response.check_legal_output(c.rc, true).decision, tc::Period_decision::unsupported);
  response.update(c.rc, c.locations, 20, .1);
  EXPECT_TRUE(response.check_legal_output(c.rc, true).passed());
}

TEST(GpTiming, BoundedDriverPolicyIsSharedByReferenceContinuousAndFinalGuards) {
  Circuit c;
  c.settings.driver.bounded_boundary = true;
  late::Proxy proxy(*c.graph, c.settings.models, c.settings.constraints);
  const auto  observed = proxy.evaluate(&c.rc, nullptr, {.driver = c.settings.driver});
  EXPECT_TRUE(observed.bounded_boundary_policy);
  EXPECT_FALSE(observed.characterized_boundary_policy);
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  const double          reference = session.set_legal_reference(c.rc, true);
  const auto            first     = session.update(c.rc, c.locations, 10, .5);
  const auto            next      = session.update(c.rc, c.locations, 20, .4);
  EXPECT_EQ(first.reference_period_ps, next.reference_period_ps);
  EXPECT_EQ(next.decision, tc::Period_decision::within_noise);
  const auto legal = session.check_legal_output(c.rc, true);
  EXPECT_TRUE(legal.passed()) << legal.reason;
  EXPECT_EQ(legal.reference_period_ps, reference);
}

TEST(GpTiming, FreshLegalizationWeightsDoNotChangeHistoryCalibrationOrResponseBudget) {
  Circuit c;
  c.settings.legal_output_guard = true;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy()), control(*c.graph, c.settings, c.policy());
  session.set_legal_reference(c.rc, true);
  control.set_legal_reference(c.rc, true);
  const auto original = session.legalization_weights(c.rc);
  EXPECT_FALSE(original.weights.pins.empty());
  session.update(c.rc, c.locations, 10, .5);
  control.update(c.rc, c.locations, 10, .5);
  auto displaced                             = c.rc;
  displaced.nets[1].nodes[1].capacitance_ff *= 2;
  const auto fresh                           = session.legalization_weights(displaced);
  EXPECT_GT(fresh.period_ps, original.period_ps);
  EXPECT_FALSE(session.check_legal_output(c.rc, true).response_interval_complete);
  auto invalid                 = c.rc;
  invalid.nets[0].nodes[1].pin = "alien";
  EXPECT_THROW(session.legalization_weights(invalid), std::invalid_argument);
  const auto observed = session.update(c.rc, c.locations, 20, .4);
  const auto expected = control.update(c.rc, c.locations, 20, .4);
  EXPECT_EQ(observed.reference_period_ps, expected.reference_period_ps);
  EXPECT_EQ(observed.calibration.reused, expected.calibration.reused);
  ASSERT_EQ(observed.weights.pins.size(), expected.weights.pins.size());
  for (const auto& [name, weight] : expected.weights.pins) {
    EXPECT_EQ(observed.weights.pins.at(name).weight, weight.weight);
    EXPECT_EQ(observed.weights.pins.at(name).raw, weight.raw);
  }
  EXPECT_TRUE(session.check_legal_output(c.rc, true).passed());
}
TEST(GpTiming, LegalizationPrioritiesAndObservationRequireFinalGuardAndBoundedCosts) {
  Circuit c;
  c.settings.legalize_observe = true;
  EXPECT_THROW(c.settings.validate(), std::invalid_argument);
  c.settings.legalize_observe          = false;
  c.settings.legalization.displacement = 9;
  EXPECT_THROW(c.settings.validate(), std::invalid_argument);
  c.settings.legal_output_guard = true;
  EXPECT_NO_THROW(c.settings.validate());
  c.settings.legalization.net = 100;
  EXPECT_THROW(c.settings.validate(), std::invalid_argument);
  c.settings.legalization.net = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(c.settings.validate(), std::invalid_argument);
}

TEST(GpTiming, PhysicalPreparationFailureDoesNotCommitTimingPolicyOrSpentIteration) {
  Circuit               c;
  tc::Gp_timing_session actual(*c.graph, c.settings, c.policy()), control(*c.graph, c.settings, c.policy());
  actual.update(c.rc, c.locations, 1, .05);
  control.update(c.rc, c.locations, 1, .05);
  unsigned   calls  = 0;
  const auto reject = [&](const late::Result& timing, const tc::Pin_weights& weights) {
    ++calls;
    EXPECT_TRUE(timing.certified);
    EXPECT_TRUE(timing.feasible_graph);
    EXPECT_FALSE(weights.pins.empty());
    throw std::invalid_argument("physical checkpoint failed");
  };
  EXPECT_THROW(actual.update(c.rc, c.locations, 2, .05, {}, reject), std::invalid_argument);
  ASSERT_EQ(calls, 1u);
  const auto a = actual.update(c.rc, c.locations, 2, .05), b = control.update(c.rc, c.locations, 2, .05);
  EXPECT_EQ(a.reference_period_ps, b.reference_period_ps);
  EXPECT_EQ(a.calibration.reused, b.calibration.reused);
  EXPECT_EQ(a.temperature_fraction, b.temperature_fraction);
  ASSERT_EQ(a.weights.pins.size(), b.weights.pins.size());
  for (const auto& [name, w] : a.weights.pins) {
    EXPECT_DOUBLE_EQ(w.weight, b.weights.pins.at(name).weight);
  }
}
TEST(GpTiming, PreparationIsSkippedForDeferredOrRolledBackFrames) {
  Circuit c;
  c.settings.correction_max_age       = 1;
  c.settings.maximum_density_overflow = .1;
  c.settings.continuing_rollback      = true;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  unsigned              calls   = 0;
  const auto            prepare = [&](const late::Result&, const tc::Pin_weights&) { ++calls; };
  EXPECT_TRUE(session.update(c.rc, c.locations, 1, .2, {}, prepare).deferred);
  EXPECT_EQ(calls, 0u);
  EXPECT_FALSE(session.update(c.rc, c.locations, 2, .05, {}, prepare).rollback);
  EXPECT_EQ(calls, 1u);
  EXPECT_TRUE(session.update(c.rc, c.locations, 3, .2, {}, prepare).rollback);
  EXPECT_EQ(calls, 1u);
  auto worse = c.rc;
  for (auto& net : worse.nets) {
    for (auto& edge : net.edges) {
      edge.resistance_ohm *= 2;
    }
  }
  const auto hard = session.update(worse, c.locations, 4, .05, {}, prepare);
  EXPECT_TRUE(hard.rollback);
  EXPECT_EQ(hard.reason, "hard_period");
  EXPECT_EQ(calls, 1u);
}

TEST(GpTiming, StartupHintDoesNotAdvanceFeedbackHistoryOrGuard) {
  Circuit c;
  c.settings.startup_coefficient     = .025;
  c.settings.startup_average_wire_um = 0;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  const auto            initial = session.startup_weights(c.rc);
  EXPECT_TRUE(initial.supported);
  EXPECT_GT(initial.selected, 0u);
  EXPECT_FALSE(initial.weights.pins.empty());
  const auto repeated = session.startup_weights(c.rc);
  EXPECT_EQ(initial.selected, repeated.selected);
  const auto first = session.update(c.rc, c.locations, 1, .5);
  EXPECT_EQ(first.decision, tc::Period_decision::improve);
  EXPECT_THROW(session.startup_weights(c.rc), std::invalid_argument);
  c.settings.startup_coefficient = 1;
  EXPECT_THROW(c.settings.validate(), std::invalid_argument);
}

TEST(GpTiming, UnsupportedStartupAppliesNoWeightsAndCannotWaiveLaterCoverage) {
  Circuit c;
  auto    models = std::make_shared<model::Library>(*c.settings.models);
  models->cells.at("BUF").pins.at("Y").arcs[0].delay[1].reset();
  c.settings.models              = models;
  c.settings.startup_coefficient = .025;
  tc::Gp_timing_session session(*c.graph, c.settings, c.policy());
  const auto            hint = session.startup_weights(c.rc);
  EXPECT_FALSE(hint.supported);
  EXPECT_TRUE(hint.weights.pins.empty());
  EXPECT_FALSE(hint.reason.empty());
  EXPECT_THROW(session.update(c.rc, c.locations, 1, .5), std::invalid_argument);
}
