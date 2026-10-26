// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "wire_waveform.hpp"

#include <cmath>
#include <stdexcept>

#include "gtest/gtest.h"

namespace late = livehd::placement::late;
using livehd::liberty::timing::Thresholds;

TEST(WireWaveform, StepUsesExponentialCrossingsForRiseAndFall) {
  Thresholds thresholds;
  for (unsigned t = 0; t < 2; ++t) {
    const auto response = late::ramp_wire(0, 10, thresholds, thresholds, t);
    EXPECT_NEAR(response.delay_ps, 10 * std::log(2), 1e-12);
    EXPECT_NEAR(response.slew_ps, 10 * std::log(4), 1e-12);
  }
}
TEST(WireWaveform, ZeroWireConvertsThresholdsAndDeratesWithoutBroadening) {
  Thresholds source, target;
  source.output      = {.4, .6};
  source.slew_derate = .5;
  target.input       = {.7, .3};
  target.lower       = {.1, .1};
  target.upper       = {.9, .9};
  target.slew_derate = .8;
  for (unsigned t = 0; t < 2; ++t) {
    const auto response = late::ramp_wire(12, 0, source, target, t);
    EXPECT_NEAR(response.delay_ps, 3, 1e-12);
    EXPECT_NEAR(response.slew_ps, 10, 1e-12);
  }
}
TEST(WireWaveform, SlowRampApproachesElmoreDelayWithUnchangedSlew) {
  Thresholds thresholds;
  const auto response = late::ramp_wire(600, .1, thresholds, thresholds, 0);
  EXPECT_NEAR(response.delay_ps, .1, 1e-10);
  EXPECT_NEAR(response.slew_ps, 600, 1e-10);
}
TEST(WireWaveform, FiniteRampSolvesConvolutionAndScalesWithTimeUnits) {
  Thresholds   thresholds;
  const double tau = 10, duration = 20;
  const double tail   = tau / duration * (1 - std::exp(-duration / tau));
  // Choose a receiver threshold with a closed-form crossing past ramp end:
  // 1 - tail*exp(-(t-duration)/tau) = .9.
  thresholds.input    = {.9, .1};
  const auto response = late::ramp_wire(12, tau, thresholds, thresholds, 0);
  EXPECT_NEAR(response.delay_ps, duration - tau * std::log(.1 / tail) - .5 * duration, 1e-12);
  const auto scaled = late::ramp_wire(.012, .01, thresholds, thresholds, 0);
  EXPECT_NEAR(scaled.delay_ps * 1000, response.delay_ps, 1e-10);
  EXPECT_NEAR(scaled.slew_ps * 1000, response.slew_ps, 1e-10);
}
TEST(WireWaveform, TinyRampsRemainFiniteAndNegativeRCIsRejected) {
  Thresholds thresholds;
  const auto response = late::ramp_wire(1e-15, 1000, thresholds, thresholds, 0);
  EXPECT_NEAR(response.delay_ps, 1000 * std::log(2), 1e-9);
  EXPECT_NEAR(response.slew_ps, 1000 * std::log(4), 1e-9);
  EXPECT_THROW(late::ramp_wire(1, -1, thresholds, thresholds, 0), std::invalid_argument);
}
