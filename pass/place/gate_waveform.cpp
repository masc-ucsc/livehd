// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gate_waveform.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace livehd::placement::late {
namespace {
void nonnegative(double value) {
  if (!std::isfinite(value) || value < 0) {
    throw std::invalid_argument("invalid native driver waveform input");
  }
}
struct Value {
  double voltage = 0, derivative = 0;
};
Value ramp(double t, double d) {
  if (t <= 0) {
    return {};
  }
  if (d == 0 || t >= d) {
    return {1, 0};
  }
  return {t / d, 1 / d};
}
Value pole(double t, double d, double tau) {
  if (t <= 0) {
    return {};
  }
  if (tau == 0) {
    return ramp(t, d);
  }
  const double x = t / tau;
  if (d == 0) {
    const double e = std::exp(-x);
    return {-std::expm1(-x), e / tau};
  }
  if (t <= d) {
    const double integral = x < 1e-4 ? x * x * (.5 + x * (-1. / 6 + x / 24)) : x + std::expm1(-x);
    return {tau * integral / d, -std::expm1(-x) / d};
  }
  const double tail = -std::expm1(-d / tau) * std::exp(-(t - d) / tau);
  return {1 - tau * tail / d, tail / d};
}
Value double_pole(double t, double d, double tau) {
  if (t <= 0) {
    return {};
  }
  const auto step = [](double x) { return x < 1e-4 ? x * x * (.5 + x * (-1. / 3 + x / 8)) : -std::expm1(-x) - x * std::exp(-x); };
  if (d == 0 || d < tau * 1e-7) {
    const double x = std::max(0., t - d * .5) / tau;
    return {step(x), x * std::exp(-x) / tau};
  }
  const double x        = t / tau;
  const auto   integral = [](double z) {
    return z < 1e-3 ? z * z * z * (1. / 6 + z * (-1. / 12 + z * (1. / 40 - z / 180))) : z - 2 + (z + 2) * std::exp(-z);
  };
  if (t <= d) {
    return {tau * integral(x) / d, step(x) / d};
  }
  const double y = (t - d) / tau;
  return {1 - tau * ((y + 2) * std::exp(-y) - (x + 2) * std::exp(-x)) / d, (step(x) - step(y)) / d};
}
Value cascade(double t, double d, double a, double b) {
  if (a == 0) {
    return pole(t, d, b);
  }
  if (b == 0) {
    return pole(t, d, a);
  }
  if (std::abs(a - b) < 1e-6 * std::max(a, b)) {
    return double_pole(t, d, (a + b) * .5);
  }
  const auto x = pole(t, d, a), y = pole(t, d, b);
  return {(a * x.voltage - b * y.voltage) / (a - b), (a * x.derivative - b * y.derivative) / (a - b)};
}
Value value(const Pi_waveform& shape, double t, double load) {
  const double direct = 1 - shape.weight[0] - shape.weight[1];
  const auto   base   = pole(t, shape.duration_ps, load);
  Value        result{direct * base.voltage, direct * base.derivative};
  for (unsigned i = 0; i < 2; ++i) {
    if (shape.weight[i] != 0) {
      const auto term    = cascade(t, shape.duration_ps, shape.tau_ps[i], load);
      result.voltage    += shape.weight[i] * term.voltage;
      result.derivative += shape.weight[i] * term.derivative;
    }
  }
  return {std::clamp(result.voltage, 0., 1.), std::max(0., result.derivative)};
}
struct Levels {
  double low, middle, high;
};
Levels levels(const liberty::timing::Thresholds& thresholds, unsigned t) {
  return t == 0 ? Levels{thresholds.lower[t], thresholds.output[t], thresholds.upper[t]}
                : Levels{1 - thresholds.upper[t], 1 - thresholds.output[t], 1 - thresholds.lower[t]};
}
}  // namespace

Pi_waveform pi_waveform(const Pi_model& pi, double driver_resistance_ohm, double duration_ps) {
  nonnegative(pi.near_cap_ff);
  nonnegative(pi.far_cap_ff);
  nonnegative(pi.resistance_ohm);
  nonnegative(driver_resistance_ohm);
  nonnegative(duration_ps);
  const double rd = driver_resistance_ohm * .001, rw = pi.resistance_ohm * .001;
  const double zero = rw * pi.far_cap_ff;
  const double a    = zero + rd * (pi.near_cap_ff + pi.far_cap_ff);
  const double b    = rd * pi.near_cap_ff * zero;
  Pi_waveform  result;
  result.duration_ps = duration_ps;
  if (a == 0) {
    return result;
  }
  if (b == 0) {
    result.tau_ps[0] = a;
    result.weight[0] = 1 - zero / a;
    return result;
  }
  // Hroot(s)=(1+s*Rw*Cfar)/(1+s*a+s^2*b). Stable quadratic
  // roots avoid subtracting two almost equal positive numbers.
  const double discriminant = std::max(0., a * a - 4 * b);
  const double slow = (a + std::sqrt(discriminant)) * .5, fast = b / slow;
  result.tau_ps = {slow, fast};
  result.weight = {(slow - zero) / (slow - fast), (zero - fast) / (slow - fast)};
  for (unsigned i = 0; i < 2; ++i) {
    nonnegative(result.tau_ps[i]);
    if (!std::isfinite(result.weight[i]) || result.weight[i] < -1e-10) {
      throw std::invalid_argument("nonpassive native pi waveform");
    }
    result.weight[i] = std::max(0., result.weight[i]);
  }
  return result;
}

double Pi_waveform::voltage(double time_ps, double load_elmore_ps) const { return value(*this, time_ps, load_elmore_ps).voltage; }
double Pi_waveform::crossing(double voltage, double load_elmore_ps) const {
  if (!(voltage > 0 && voltage < 1)) {
    throw std::invalid_argument("waveform crossing threshold must lie between zero and one");
  }
  nonnegative(load_elmore_ps);
  const double tau = std::max(tau_ps[0], tau_ps[1]) + load_elmore_ps;
  if (tau == 0) {
    return duration_ps * voltage;
  }
  double low = 0, high = duration_ps + 40 * tau;
  double t = std::clamp(voltage * duration_ps + weight[0] * tau_ps[0] + weight[1] * tau_ps[1] + load_elmore_ps, low, high);
  for (unsigned i = 0; i < 60; ++i) {
    const auto   response = value(*this, t, load_elmore_ps);
    const double residual = response.voltage - voltage;
    if (std::abs(residual) < 1e-11 || high - low < 1e-12 * (duration_ps + tau)) {
      return t;
    }
    if (residual < 0) {
      low = t;
    } else {
      high = t;
    }
    const double proposed = response.derivative > 0 ? t - residual / response.derivative : high;
    t                     = proposed > low && proposed < high ? proposed : (low + high) * .5;
  }
  throw std::runtime_error("native pi waveform crossing did not converge");
}

Wire_waveform Driver_waveform::wire(double elmore_ps, const liberty::timing::Thresholds& driver,
                                    const liberty::timing::Thresholds& receiver, unsigned transition) const {
  nonnegative(elmore_ps);
  if (transition > 1) {
    throw std::invalid_argument("invalid driver transition");
  }
  if (mode == Driver_mode::ideal) {
    return ramp_wire(slew_ps, elmore_ps, driver, receiver, transition);
  }
  const auto v          = levels(driver, transition);
  double     wire_delay = elmore_ps, measured_slew = slew_ps;
  if (mode == Driver_mode::distributed && elmore_ps > 0) {
    wire_delay    = std::max(0., shape.crossing(v.middle, elmore_ps) - reference_ps);
    measured_slew = std::max(slew_ps, (shape.crossing(v.high, elmore_ps) - shape.crossing(v.low, elmore_ps)) / driver.slew_derate);
  }
  const double full_slew     = measured_slew * driver.slew_derate / (driver.upper[transition] - driver.lower[transition]);
  const double adjusted_slew = full_slew * (receiver.upper[transition] - receiver.lower[transition]) / receiver.slew_derate;
  return {wire_delay + (transition == 0 ? 1 : -1) * (receiver.input[transition] - driver.output[transition]) * full_slew,
          adjusted_slew};
}

Driver_waveform fit_driver(const liberty::timing::Table& delay, const liberty::timing::Table& slew, double input_slew_ps,
                           double total_cap_ff, const liberty::timing::Thresholds& thresholds, unsigned transition,
                           const Pi_model* pi, Driver_options options) {
  nonnegative(input_slew_ps);
  nonnegative(total_cap_ff);
  if (transition > 1) {
    throw std::invalid_argument("invalid driver transition");
  }
  const auto                            v = levels(thresholds, transition);
  std::optional<liberty::timing::Table> delay_slice, slew_slice;
  const auto                            table = [&](double cap) {
    const liberty::timing::Query query{.input_slew_ps = input_slew_ps, .output_load_ff = cap};
    const double                 d = (delay_slice ? *delay_slice : delay).lookup(query);
    const double                 s = std::max(0., (slew_slice ? *slew_slice : slew).lookup(query));
    if (!std::isfinite(d) || !std::isfinite(s)) {
      throw std::invalid_argument("nonfinite driver table lookup");
    }
    return std::pair{d, s};
  };
  const auto [total_delay, total_slew] = table(total_cap_ff);
  Driver_waveform result;
  result.delay_ps         = total_delay;
  result.slew_ps          = total_slew;
  result.effective_cap_ff = total_cap_ff;
  if (!pi) {
    return result;
  }
  result.mode = Driver_mode::lumped;
  if (!(v.low > 0 && v.low < v.middle && v.middle < v.high && v.high < 1)) {
    result.mode    = Driver_mode::failed;
    result.failure = Driver_failure::thresholds;
    return result;
  }
  const double rd              = -std::log(v.middle) * std::abs(table(total_cap_ff + 1).first - total_delay);  // ps/fF
  result.driver_resistance_ohm = rd * 1000;
  // A purely capacitive load, flat delay table or negligible far branch supplies
  // no resolvable distributed driver waveform. Keep its characterized boundary.
  if (rd < 1e-5 || pi->resistance_ohm < result.driver_resistance_ohm * 1e-3 || pi->far_cap_ff == 0
      || pi->far_cap_ff < pi->near_cap_ff * 1e-3) {
    return result;
  }
  constexpr double bounded_resistance_ratio = .01;
  const bool       strongly_coupled         = pi->resistance_ohm <= result.driver_resistance_ohm * bounded_resistance_ratio;
  if (options.bounded_boundary && strongly_coupled && total_slew > 0) {
    // The zero-wire full-load step is the minimum-width pure-C response.
    // Avoid selecting an early charge-window shielding root when its full-load
    // characterized ramp is impossible in this low-wire-resistance regime.
    const double minimum = rd * total_cap_ff * std::log((1 - v.low) / (1 - v.middle));
    const double desired = total_slew * thresholds.slew_derate * (v.middle - v.low) / (v.high - v.low);
    const double margin  = 64 * std::numeric_limits<double>::epsilon() * std::max({1., minimum, desired});
    if (desired < minimum - margin) {
      result.mode    = Driver_mode::full_load_boundary;
      result.failure = Driver_failure::full_load_ramp;
      return result;
    }
  }
  result.mode          = Driver_mode::failed;
  result.failure       = Driver_failure::no_positive_ramp;
  delay_slice          = delay.output_load_slice({.input_slew_ps = input_slew_ps});
  slew_slice           = slew.output_load_slice({.input_slew_ps = input_slew_ps});
  const auto base      = pi_waveform(*pi, result.driver_resistance_ohm, 0);
  const bool zero_near = pi->near_cap_ff < pi->far_cap_ff * 1e-3;
  // The far-cap voltage uses Hfar(s)=1/((1+s*tau0)*(1+s*tau1)).
  const auto charge_pi = [&](double horizon) {
    const auto   slow = pole(horizon, horizon, base.tau_ps[0]), fast = pole(horizon, horizon, base.tau_ps[1]);
    const double gap        = base.tau_ps[0] - base.tau_ps[1];
    const double root       = base.weight[0] * slow.voltage + base.weight[1] * fast.voltage;
    const double root_dt    = base.weight[0] * slow.derivative + base.weight[1] * fast.derivative;
    const double far        = (base.tau_ps[0] * slow.voltage - base.tau_ps[1] * fast.voltage) / gap;
    const double far_dt     = (base.tau_ps[0] * slow.derivative - base.tau_ps[1] * fast.derivative) / gap;
    const double q          = pi->near_cap_ff * root + pi->far_cap_ff * far;
    // Differentiate the ramp-end charge with respect to its horizon. The
    // excitation's 1/horizon factor contributes -Q/horizon.
    const double derivative = pi->near_cap_ff * root_dt + pi->far_cap_ff * far_dt - q / horizon;
    return std::pair{q, derivative};
  };
  const auto capacitance = [&](double duration) {
    if (zero_near) {
      return total_cap_ff;
    }
    double low = pi->near_cap_ff, high = total_cap_ff, cap = (low + high) * .5;
    for (unsigned i = 0; i < 32; ++i) {
      // Compare charge under the same *untruncated* ramp. Its slope cancels;
      // evaluate both responses at the charge-matching horizon instead. The
      // horizon is bounded by the characterized full-swing time and ramp age.
      const double full_slew
          = std::max(0., slew_slice->lookup({.output_load_ff = cap})) * thresholds.slew_derate / (v.high - v.low);
      const double horizon = std::min(full_slew, 1.4 * duration);
      if (horizon == 0) {
        return low;
      }
      double horizon_derivative = 0;
      if (full_slew < 1.4 * duration && slew_slice->x.size() > 1) {
        const auto&  axis  = slew_slice->x;
        const size_t upper = std::clamp<size_t>(std::upper_bound(axis.begin(), axis.end(), cap) - axis.begin(), 1, axis.size() - 1);
        horizon_derivative = (slew_slice->values[upper] - slew_slice->values[upper - 1]) / (axis[upper] - axis[upper - 1])
                             * thresholds.slew_derate / (v.high - v.low);
      }
      const auto [q, q_derivative] = charge_pi(horizon);
      const auto   response        = pole(horizon, horizon, rd * cap);
      const double delivered = cap * response.voltage, residual = delivered - q;
      if (high - low < total_cap_ff * 1e-8 || std::abs(residual) < std::max(q, delivered) * 1e-13) {
        return cap;
      }
      if (residual < 0) {
        low = cap;
      } else {
        high = cap;
      }
      const double x = horizon / (rd * cap);
      const double partial_cap
          = x < 1e-3 ? x * x * (1. / 6 + x * (-1. / 12 + x * (1. / 40 - x / 180))) : 1 - 2 / x + (1 + 2 / x) * std::exp(-x);
      const double partial_horizon = cap * (response.derivative - response.voltage / horizon);
      const double derivative      = partial_cap + (partial_horizon - q_derivative) * horizon_derivative;
      const double proposed        = derivative > 0 ? cap - residual / derivative : high;
      cap                          = proposed > low && proposed < high ? proposed : (low + high) * .5;
    }
    return (low + high) * .5;
  };
  struct Trial {
    double      residual, cap;
    Pi_waveform shape;
    double      middle;
  };
  const auto trial = [&](double duration) {
    const double cap    = capacitance(duration);
    auto         shape  = pi_waveform({cap, 0, 0}, result.driver_resistance_ohm, duration);
    const double middle = shape.crossing(v.middle), lower = shape.crossing(v.low);
    const double desired = table(cap).second * thresholds.slew_derate * (v.middle - v.low) / (v.high - v.low);
    return Trial{middle - lower - desired, cap, shape, middle};
  };
  const double scale = std::max(total_slew * thresholds.slew_derate / (v.high - v.low), rd * total_cap_ff);
  if (!(scale > 0)) {
    return result;
  }
  double low = scale * 1e-5, high = scale * 2;
  auto   left = trial(low), right = trial(high);
  if (left.residual > 0) {
    if (options.characterized_boundary) {
      // An instantaneous step is the minimum-width member of the positive
      // linear-ramp family for a first-order Rd*C load. Its lower-to-middle
      // width is Rd*C*log((1-vlow)/(1-vmid)). The sliced NLDM slew is affine
      // between load knots, including extrapolation. Clamping negative slew
      // to zero makes (desired - minimum) convex on each interval, so checking
      // both endpoints and every interior knot proves strict infeasibility.
      // Leave numerical ambiguity unsupported rather than relaxing the fit.
      const double width_per_cap = rd * std::log((1 - v.low) / (1 - v.middle));
      const auto   impossible    = [&](double cap) {
        const double minimum = width_per_cap * cap;
        const double desired = table(cap).second * thresholds.slew_derate * (v.middle - v.low) / (v.high - v.low);
        const double margin  = 64 * std::numeric_limits<double>::epsilon() * std::max({1., minimum, desired});
        return std::isfinite(minimum) && std::isfinite(desired) && desired < minimum - margin;
      };
      bool proven = std::isfinite(pi->near_cap_ff) && pi->near_cap_ff >= 0 && pi->near_cap_ff <= total_cap_ff
                    && impossible(pi->near_cap_ff) && impossible(total_cap_ff);
      for (double cap : slew_slice->x) {
        if (cap > pi->near_cap_ff && cap < total_cap_ff) {
          proven = proven && impossible(cap);
        }
      }
      if (proven) {
        result.mode = Driver_mode::table_boundary;
      }
    }
    return result;
  }
  for (unsigned i = 0; right.residual < 0 && i < 12; ++i) {
    high  *= 2;
    right  = trial(high);
  }
  if (right.residual < 0) {
    result.failure = Driver_failure::no_bracket;
    return result;
  }
  Trial current = right;
  for (unsigned i = 0; i < 32; ++i) {
    const double secant   = (low * right.residual - high * left.residual) / (right.residual - left.residual);
    const double duration = std::clamp(secant, low + .1 * (high - low), high - .1 * (high - low));
    current               = trial(duration);
    if (std::abs(current.residual) < scale * 1e-8) {
      break;
    }
    if (current.residual < 0) {
      low  = duration;
      left = current;
    } else {
      high  = duration;
      right = current;
    }
  }
  if (std::abs(current.residual) > scale * 1e-6) {
    result.failure = Driver_failure::residual;
    return result;
  }
  result.mode              = Driver_mode::distributed;
  result.failure           = Driver_failure::none;
  result.shape             = base;
  result.shape.duration_ps = current.shape.duration_ps;
  result.reference_ps      = result.shape.crossing(v.middle);
  result.effective_cap_ff  = current.cap;
  result.delay_ps          = table(current.cap).first;
  if (zero_near) {
    result.delay_ps += result.reference_ps - current.middle;
  }
  if (options.bounded_boundary && strongly_coupled) {
    // Explicit bounded-window model, matching the comparison engine's time
    // convention without calling it. A two-time-constant interval need not
    // contain a 90% crossing. Retain characterized slew at the fitted load;
    // a successfully resolved Pi outside this regime keeps its full waveform.
    const double limit = result.shape.duration_ps + 2 * total_cap_ff * (result.driver_resistance_ohm + pi->resistance_ohm) * .001;
    if (result.shape.voltage(limit) < v.high - 64 * std::numeric_limits<double>::epsilon()) {
      result.mode    = Driver_mode::crossing_boundary;
      result.failure = Driver_failure::crossing_window;
      result.slew_ps = table(current.cap).second;
      return result;
    }
  }
  result.slew_ps = (result.shape.crossing(v.high) - result.shape.crossing(v.low)) / thresholds.slew_derate;
  return result;
}
}  // namespace livehd::placement::late
