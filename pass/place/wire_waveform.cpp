// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "wire_waveform.hpp"

#include <cmath>
#include <stdexcept>

namespace livehd::placement::late {
namespace {
// Voltage progress through an ideal ramp of duration d followed by a one-pole
// low-pass with time constant tau. All crossing times are relative to ramp start.
double crossing(double voltage, double d, double tau) {
  if (tau == 0) {
    return voltage * d;
  }
  if (d == 0) {
    return -tau * std::log1p(-voltage);
  }
  const double ratio = d / tau;
  // Away from the ramp's endpoints the slow-ramp limit is t = v*d + tau.
  // This also avoids cancellation when a negligible pole is added to a long ramp.
  if (ratio * voltage > 40 && ratio * (1 - voltage) > 40) {
    return voltage * d + tau;
  }
  const double tail = -std::expm1(-ratio) / ratio;
  if (voltage >= 1 - tail) {
    return d - tau * std::log((1 - voltage) / tail);
  }
  double low = 0, high = d;
  for (unsigned i = 0; i < 48; ++i) {
    const double t = (low + high) * .5, x = t / tau;
    // x + expm1(-x) loses precision for a nearly step-like excitation.
    const double progress = x < 1e-4 ? x * x * (.5 + x * (-1. / 6 + x / 24)) : x + std::expm1(-x);
    if (progress / ratio < voltage) {
      low = t;
    } else {
      high = t;
    }
  }
  return (low + high) * .5;
}
}  // namespace

Wire_waveform ramp_wire(double driver_slew_ps, double elmore_ps, const liberty::timing::Thresholds& driver,
                        const liberty::timing::Thresholds& receiver, unsigned transition) {
  if (transition > 1 || !std::isfinite(driver_slew_ps) || driver_slew_ps < 0 || !std::isfinite(elmore_ps) || elmore_ps < 0) {
    throw std::invalid_argument("invalid wire waveform input");
  }
  const double  duration = driver_slew_ps * driver.slew_derate / (driver.upper[transition] - driver.lower[transition]);
  const double  source   = transition == 0 ? driver.output[transition] : 1 - driver.output[transition];
  const double  target   = transition == 0 ? receiver.input[transition] : 1 - receiver.input[transition];
  const double  lower    = transition == 0 ? receiver.lower[transition] : 1 - receiver.upper[transition];
  const double  upper    = transition == 0 ? receiver.upper[transition] : 1 - receiver.lower[transition];
  Wire_waveform result{crossing(target, duration, elmore_ps) - source * duration,
                       (crossing(upper, duration, elmore_ps) - crossing(lower, duration, elmore_ps)) / receiver.slew_derate};
  if (!std::isfinite(result.delay_ps) || !std::isfinite(result.slew_ps)) {
    throw std::invalid_argument("wire waveform result overflow");
  }
  return result;
}
}  // namespace livehd::placement::late
