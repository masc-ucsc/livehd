// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gate_waveform.hpp"

#include <cmath>
#include <stdexcept>

#include "gtest/gtest.h"

namespace late  = livehd::placement::late;
namespace model = livehd::liberty::timing;
namespace {
model::Table linear(double intercept, double slope) {
  return {
      model::Variable::output_load,
      model::Variable::none,
      {0, 10},
      {},
      {intercept, intercept + 10 * slope}
  };
}
}  // namespace

TEST(GateWaveform, PureCapStepMatchesAnalyticExponentialAndEqualPolesRemainFinite) {
  const auto shape = late::pi_waveform({2, 0, 0}, 5000, 0);
  EXPECT_NEAR(shape.voltage(10), 1 - std::exp(-1), 1e-12);
  EXPECT_NEAR(shape.crossing(.5), 10 * std::log(2), 1e-9);
  // Convolution of two identical first-order poles: 1-(1+t/tau)*exp(-t/tau).
  EXPECT_NEAR(shape.voltage(10, 10), 1 - 2 / std::exp(1), 1e-12);
  const double crossing = shape.crossing(.5, 10);
  EXPECT_NEAR(shape.voltage(crossing, 10), .5, 1e-10);
  EXPECT_NEAR(shape.voltage(10, 10 * (1 + 1e-8)), shape.voltage(10, 10), 1e-8);
  const auto ramp_shape = late::pi_waveform({2, 0, 0}, 5000, 20);
  EXPECT_NEAR(ramp_shape.voltage(10, 10), (10 - 20 + 30 / std::exp(1)) / 20, 1e-12);
}
TEST(GateWaveform, ZeroNearCapHasAnInstantaneousResistiveDividerComponent) {
  const auto shape = late::pi_waveform({0, 2, 1000}, 500, 0);
  // At a step, the root starts at Rwire/(Rwire+Rdriver) and approaches one
  // with tau=(Rwire+Rdriver)*Cfar. Test strictly positive t.
  EXPECT_NEAR(shape.voltage(3), 1 - std::exp(-1) / 3, 1e-12);
  EXPECT_NEAR(shape.voltage(1e-10), 2. / 3, 1e-10);
  EXPECT_NEAR(shape.crossing(.9), -3 * std::log(.3), 1e-9);
}
TEST(GateWaveform, RampAndStepLimitsPreserveChargeAndTimeUnits) {
  const late::Pi_model pi{2, 3, 5000};
  auto                 shape  = late::pi_waveform(pi, 1000, 20);
  auto                 scaled = late::pi_waveform({2, 3, 5}, 1, .020);
  for (double voltage : {.1, .2, .5, .8, .9}) {
    const double crossing = shape.crossing(voltage);
    EXPECT_NEAR(shape.voltage(crossing), voltage, 1e-10);
    EXPECT_NEAR(scaled.crossing(voltage) * 1000, crossing, 1e-8);
  }
  const auto ideal = late::pi_waveform(pi, 0, 20);
  EXPECT_NEAR(ideal.voltage(10), .5, 1e-12);
  EXPECT_NEAR(ideal.crossing(.8), 16, 1e-12);
}
TEST(GateWaveform, ZeroWirePreservesNldmAndFlatTablesUseAnExplicitLumpedBoundary) {
  model::Thresholds thresholds;
  auto              delay = linear(10, 2), slew = linear(4, 1);
  const auto        ideal = late::fit_driver(delay, slew, 0, 5, thresholds, 0);
  EXPECT_EQ(ideal.mode, late::Driver_mode::ideal);
  EXPECT_EQ(ideal.delay_ps, 20);
  EXPECT_EQ(ideal.slew_ps, 9);
  const late::Pi_model pi{2, 3, 10000};
  delay             = linear(10, 0);
  const auto lumped = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &pi);
  EXPECT_EQ(lumped.mode, late::Driver_mode::lumped);
  const auto wire = lumped.wire(20, thresholds, thresholds, 0);
  EXPECT_EQ(wire.delay_ps, 20);
  EXPECT_EQ(wire.slew_ps, 9);
}
TEST(GateWaveform, ChargeFitSatisfiesNldmCrossingsAndKeepsCapacitanceBounded) {
  model::Thresholds    thresholds;
  const auto           delay = linear(10, 2), slew = linear(20, 2);
  const late::Pi_model pi{2, 3, 10000};
  const auto           fit = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &pi);
  ASSERT_EQ(fit.mode, late::Driver_mode::distributed);
  EXPECT_GE(fit.effective_cap_ff, pi.near_cap_ff);
  EXPECT_LT(fit.effective_cap_ff, 5);
  EXPECT_NEAR(fit.delay_ps, delay.lookup({.output_load_ff = fit.effective_cap_ff}), 1e-12);
  const double table_slew = slew.lookup({.output_load_ff = fit.effective_cap_ff});
  const auto   cap_shape  = late::pi_waveform({fit.effective_cap_ff, 0, 0}, fit.driver_resistance_ohm, fit.shape.duration_ps);
  EXPECT_NEAR(cap_shape.crossing(.5) - cap_shape.crossing(.2), table_slew * .5, 1e-6);
  const double d = std::min(table_slew / .6, 1.4 * fit.shape.duration_ps), rd = fit.driver_resistance_ohm * .001;
  const auto   far_tau       = fit.shape.tau_ps;
  // Far-node transfer has the two poles in cascade; evaluate its ramp-end
  // voltage from the two independently computed single-pole ramp responses.
  const auto   response      = [&](double tau) { return 1 - tau / d * (1 - std::exp(-d / tau)); };
  const double far           = (far_tau[0] * response(far_tau[0]) - far_tau[1] * response(far_tau[1])) / (far_tau[0] - far_tau[1]);
  const auto   horizon_shape = late::pi_waveform(pi, fit.driver_resistance_ohm, d);
  const double pi_charge     = pi.near_cap_ff * horizon_shape.voltage(d) + pi.far_cap_ff * far;
  const double c_charge      = fit.effective_cap_ff * response(rd * fit.effective_cap_ff);
  EXPECT_NEAR(pi_charge, c_charge, 1e-7);
  const auto wire = fit.wire(15, thresholds, thresholds, 0);
  EXPECT_GT(wire.delay_ps, 0);
  EXPECT_GE(wire.slew_ps, fit.slew_ps);
}
TEST(GateWaveform, FailedFitIsExplicitAndScalingDoesNotChangeEffectiveLoad) {
  model::Thresholds    thresholds;
  const late::Pi_model pi{2, 3, 10000};
  const auto           failed = late::fit_driver(linear(10, 2), linear(0, 0), 0, 5, thresholds, 0, &pi);
  EXPECT_EQ(failed.mode, late::Driver_mode::failed);
  const auto           fit = late::fit_driver(linear(10, 2), linear(20, 2), 0, 5, thresholds, 0, &pi);
  const late::Pi_model scaled_pi{2, 3, 10};
  const auto           scaled = late::fit_driver(linear(.010, .002), linear(.020, .002), 0, 5, thresholds, 0, &scaled_pi);
  ASSERT_EQ(fit.mode, late::Driver_mode::distributed);
  ASSERT_EQ(scaled.mode, late::Driver_mode::distributed);
  EXPECT_NEAR(fit.effective_cap_ff, scaled.effective_cap_ff, 1e-6);
  EXPECT_NEAR(fit.delay_ps, scaled.delay_ps * 1000, 1e-5);
  EXPECT_NEAR(fit.slew_ps, scaled.slew_ps * 1000, 1e-5);
}

TEST(GateWaveform, ZeroNearCapUsesTheOnePoleCharacterizedLoadBoundary) {
  model::Thresholds    thresholds;
  const late::Pi_model pi{0, 5, 10000};
  const auto           fit = late::fit_driver(linear(100, 2), linear(20, 2), 0, 5, thresholds, 0, &pi);
  ASSERT_EQ(fit.mode, late::Driver_mode::distributed);
  EXPECT_EQ(fit.effective_cap_ff, 5);
  EXPECT_EQ(fit.failure, late::Driver_failure::none);
  EXPECT_TRUE(std::isfinite(fit.delay_ps));
  EXPECT_GT(fit.shape.duration_ps, 0);
  const auto wire = fit.wire(30, thresholds, thresholds, 0);
  EXPECT_GT(wire.delay_ps, 0);
  EXPECT_GE(wire.slew_ps, fit.slew_ps);
}

TEST(GateWaveform, ProvenInfeasibilityUsesOnlyTheExplicitCharacterizedBoundary) {
  model::Thresholds    thresholds;
  const late::Pi_model pi{2, 3, 10000};
  const auto           delay = linear(10, 2), slew = linear(0, 0);
  const auto           strict = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &pi);
  ASSERT_EQ(strict.mode, late::Driver_mode::failed);
  const auto fit = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &pi, {.characterized_boundary = true});
  ASSERT_EQ(fit.mode, late::Driver_mode::table_boundary);
  EXPECT_EQ(fit.failure, late::Driver_failure::no_positive_ramp);
  EXPECT_EQ(fit.effective_cap_ff, 5);
  EXPECT_EQ(fit.delay_ps, 20);
  EXPECT_EQ(fit.slew_ps, 0);
  EXPECT_EQ(fit.shape.duration_ps, 0);
  EXPECT_EQ(fit.driver_resistance_ohm, strict.driver_resistance_ohm);
  const late::Pi_model scaled_pi{2, 3, 10};
  const auto scaled = late::fit_driver(linear(.010, .002), slew, 0, 5, thresholds, 0, &scaled_pi, {.characterized_boundary = true});
  EXPECT_EQ(scaled.mode, late::Driver_mode::table_boundary);
  EXPECT_NEAR(scaled.delay_ps * 1000, fit.delay_ps, 1e-12);
  const auto wire = fit.wire(15, thresholds, thresholds, 0);
  EXPECT_EQ(wire.delay_ps, 15);
  EXPECT_EQ(wire.slew_ps, 0);
  // A positive but still inadmissibly narrow slew retains the characterized
  // threshold conversion, rather than inventing an exponential Pi waveform.
  const auto falling = late::fit_driver(delay, linear(1, 0), 0, 5, thresholds, 1, &pi, {.characterized_boundary = true});
  ASSERT_EQ(falling.mode, late::Driver_mode::table_boundary);
  auto receiver        = thresholds;
  receiver.input[1]    = .4;
  receiver.lower[1]    = .1;
  receiver.upper[1]    = .9;
  receiver.slew_derate = .8;
  const auto converted = falling.wire(15, thresholds, receiver, 1);
  EXPECT_NEAR(converted.delay_ps, 15 + .1 / .6, 1e-12);
  EXPECT_NEAR(converted.slew_ps, 1 / .6, 1e-12);
}

TEST(GateWaveform, BoundaryProofChecksInteriorKnotsAndRejectsAmbiguousWidths) {
  const model::Thresholds thresholds;
  const late::Pi_model    pi{2, 3, 10000};
  const auto              delay = linear(10, 2);
  const model::Table      peaked{
      model::Variable::output_load,
      model::Variable::none,
           {2, 3, 5},
           {},
           {0, 100, 0}
  };
  // Both endpoint widths are impossible, but the interior table knot admits
  // a positive width. Endpoint-only or total-load-only fallback is forbidden.
  EXPECT_NE(late::fit_driver(delay, peaked, 0, 5, thresholds, 0, &pi, {.characterized_boundary = true}).mode,
            late::Driver_mode::table_boundary);
  const double slope = -std::log(.5) * 2 * std::log(.8 / .5) * 2;
  EXPECT_NE(late::fit_driver(delay, linear(0, slope), 0, 5, thresholds, 0, &pi, {.characterized_boundary = true}).mode,
            late::Driver_mode::table_boundary);
  auto invalid       = thresholds;
  invalid.output[0]  = .1;
  const auto failure = late::fit_driver(delay, linear(0, 0), 0, 5, invalid, 0, &pi, {.characterized_boundary = true});
  EXPECT_EQ(failure.mode, late::Driver_mode::failed);
  EXPECT_EQ(failure.failure, late::Driver_failure::thresholds);
}

TEST(GateWaveform, BoundaryPolicyPreservesSuccessfulFitsAndDefaultFailureExactly) {
  const model::Thresholds thresholds;
  const late::Pi_model    pi{2, 3, 10000};
  for (const auto& slew : {linear(20, 2), linear(0, 0)}) {
    const auto original = late::fit_driver(linear(10, 2), slew, 0, 5, thresholds, 0, &pi);
    const auto disabled = late::fit_driver(linear(10, 2), slew, 0, 5, thresholds, 0, &pi, {});
    EXPECT_EQ(disabled.mode, original.mode);
    EXPECT_EQ(disabled.failure, original.failure);
    EXPECT_EQ(disabled.delay_ps, original.delay_ps);
    EXPECT_EQ(disabled.slew_ps, original.slew_ps);
    EXPECT_EQ(disabled.effective_cap_ff, original.effective_cap_ff);
  }
  const auto strict  = late::fit_driver(linear(10, 2), linear(20, 2), 0, 5, thresholds, 0, &pi);
  const auto enabled = late::fit_driver(linear(10, 2), linear(20, 2), 0, 5, thresholds, 0, &pi, {.characterized_boundary = true});
  EXPECT_EQ(enabled.mode, strict.mode);
  EXPECT_EQ(enabled.delay_ps, strict.delay_ps);
  EXPECT_EQ(enabled.slew_ps, strict.slew_ps);
  EXPECT_EQ(enabled.effective_cap_ff, strict.effective_cap_ff);
  EXPECT_EQ(enabled.shape.duration_ps, strict.shape.duration_ps);
}

TEST(GateWaveform, LowResistanceFullLoadBoundaryHasItsOwnCauseAndScope) {
  model::Thresholds thresholds;
  thresholds.lower           = {.1, .1};
  thresholds.upper           = {.9, .9};
  const auto           delay = linear(10, 20), slew = linear(30, 3);
  const late::Pi_model pi{2, 3, 100};
  const auto           strict   = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &pi);
  const auto           disabled = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &pi, {});
  EXPECT_EQ(disabled.mode, strict.mode);
  EXPECT_EQ(disabled.delay_ps, strict.delay_ps);
  EXPECT_EQ(disabled.slew_ps, strict.slew_ps);
  const auto boundary = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &pi, {.bounded_boundary = true});
  ASSERT_EQ(boundary.mode, late::Driver_mode::full_load_boundary);
  EXPECT_EQ(boundary.failure, late::Driver_failure::full_load_ramp);
  EXPECT_EQ(boundary.effective_cap_ff, 5);
  EXPECT_EQ(boundary.delay_ps, 110);
  EXPECT_EQ(boundary.slew_ps, 45);
  const late::Pi_model long_wire{2, 3, 1000};
  const auto           original = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &long_wire);
  const auto           outside  = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &long_wire, {.bounded_boundary = true});
  EXPECT_EQ(outside.mode, original.mode);
  EXPECT_EQ(outside.delay_ps, original.delay_ps);
  EXPECT_EQ(outside.slew_ps, original.slew_ps);
  auto invalid      = thresholds;
  invalid.output[0] = .05;
  const auto failed = late::fit_driver(delay, slew, 0, 5, invalid, 0, &pi, {.bounded_boundary = true});
  EXPECT_EQ(failed.mode, late::Driver_mode::failed);
  EXPECT_EQ(failed.failure, late::Driver_failure::thresholds);
  const late::Pi_model scaled_pi{2, 3, .1};
  const auto           scaled
      = late::fit_driver(linear(.010, .020), linear(.030, .003), 0, 5, thresholds, 1, &scaled_pi, {.bounded_boundary = true});
  EXPECT_EQ(scaled.mode, boundary.mode);
  EXPECT_NEAR(scaled.delay_ps * 1000, boundary.delay_ps, 1e-10);
  EXPECT_NEAR(scaled.slew_ps * 1000, boundary.slew_ps, 1e-10);
}

TEST(GateWaveform, LowResistanceCrossingWindowRetainsFittedLoadAndCharacterizedSlew) {
  model::Thresholds thresholds;
  thresholds.lower           = {.1, .1};
  thresholds.upper           = {.9, .9};
  const auto           delay = linear(10, 5), slew = linear(20.6, 0);
  const late::Pi_model pi{.8, 4.2, 20};
  const auto           strict = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &pi);
  ASSERT_EQ(strict.mode, late::Driver_mode::distributed);
  const auto boundary = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &pi, {.bounded_boundary = true});
  ASSERT_EQ(boundary.mode, late::Driver_mode::crossing_boundary);
  EXPECT_EQ(boundary.failure, late::Driver_failure::crossing_window);
  EXPECT_EQ(boundary.effective_cap_ff, strict.effective_cap_ff);
  EXPECT_EQ(boundary.delay_ps, strict.delay_ps);
  EXPECT_EQ(boundary.shape.duration_ps, strict.shape.duration_ps);
  EXPECT_EQ(boundary.slew_ps, 20.6);
  const double limit = strict.shape.duration_ps + 2 * 5 * (strict.driver_resistance_ohm + 20) * .001;
  EXPECT_LT(strict.shape.voltage(limit), .9);
  EXPECT_GT(strict.shape.crossing(.9), limit);
  const auto wire = boundary.wire(10, thresholds, thresholds, 0);
  EXPECT_EQ(wire.delay_ps, 10);
  EXPECT_EQ(wire.slew_ps, 20.6);
  const late::Pi_model scaled_pi{.8, 4.2, .02};
  const auto           scaled
      = late::fit_driver(linear(.010, .005), linear(.0206, 0), 0, 5, thresholds, 1, &scaled_pi, {.bounded_boundary = true});
  EXPECT_EQ(scaled.mode, boundary.mode);
  EXPECT_NEAR(scaled.effective_cap_ff, boundary.effective_cap_ff, 1e-8);
  EXPECT_NEAR(scaled.delay_ps * 1000, boundary.delay_ps, 1e-8);
  EXPECT_NEAR(scaled.slew_ps * 1000, boundary.slew_ps, 1e-10);
  const late::Pi_model long_wire{.8, 4.2, 1000};
  const auto           original = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &long_wire);
  const auto           outside  = late::fit_driver(delay, slew, 0, 5, thresholds, 0, &long_wire, {.bounded_boundary = true});
  EXPECT_EQ(outside.mode, original.mode);
  EXPECT_EQ(outside.delay_ps, original.delay_ps);
  EXPECT_EQ(outside.slew_ps, original.slew_ps);
}
