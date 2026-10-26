// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>

#include "pass/liberty/timing_model.hpp"
#include "rc_tree.hpp"
#include "wire_waveform.hpp"

namespace livehd::placement::late {
// Unit-step response is 1-sum(weight[i]*exp(-t/tau[i])). A nonzero direct
// component is valid for a pi model with zero near capacitance. Excitation is
// a unit ramp of duration_ps, with its start at time zero.
struct Pi_waveform {
  std::array<double, 2> tau_ps{}, weight{};
  double                duration_ps = 0;
  double                voltage(double time_ps, double load_elmore_ps = 0) const;
  double                crossing(double voltage, double load_elmore_ps = 0) const;
};
Pi_waveform pi_waveform(const Pi_model& pi, double driver_resistance_ohm, double duration_ps);

enum class Driver_mode { ideal, lumped, distributed, failed, table_boundary, full_load_boundary, crossing_boundary };
enum class Driver_failure { none, thresholds, no_positive_ramp, no_bracket, residual, full_load_ramp, crossing_window };
struct Driver_options {
  // Admit the characterized total-load boundary only after proving that no
  // nonnegative ramp can meet the NLDM lower-to-middle crossing width over
  // the entire bounded effective-load interval. Other fit failures stay errors.
  bool characterized_boundary = false;
  // Separate experimental model policy, disabled by default. Both branches
  // require Rwire/Rdriver <= .01. An impossible full-load step width retains
  // the total-load tables. A fitted Pi that cannot reach its upper threshold
  // within duration+2*Ctotal*(Rdriver+Rwire) retains table slew at fitted Ceff.
  // These characterized boundaries do not certify electrical or timer fidelity.
  bool bounded_boundary       = false;
};
struct Driver_waveform {
  Driver_failure failure  = Driver_failure::none;
  Driver_mode    mode     = Driver_mode::ideal;
  double         delay_ps = 0, slew_ps = 0, effective_cap_ff = 0;
  double         driver_resistance_ohm = 0, reference_ps = 0;
  Pi_waveform    shape;
  // Measure each sink relative to the driver's own output threshold. Voltage
  // threshold/derate conversion follows the characterized library boundary.
  Wire_waveform  wire(double elmore_ps, const liberty::timing::Thresholds& driver, const liberty::timing::Thresholds& receiver,
                      unsigned transition) const;
};
// Native charge-matching effective-capacitance model. Solve for ramp duration,
// offset and Ceff using two NLDM crossing constraints and equality of charge
// delivered to the pi and equivalent-C loads over the bounded charge window.
// Crossing constraints apply to the characterized equivalent-C waveform; the
// pi response supplies the actual driver slew and each sink's waveform. No reference
// timer is called. A numerical failure is explicit; its total-load observation
// is retained for diagnostics and must not certify optimization feedback.
// The opt-in table_boundary mode retains no_positive_ramp as its cause and
// supplies raw Elmore delay with characterized threshold/slew conversion;
// it never claims a fitted distributed waveform or electrical feasibility.
// full_load_boundary and crossing_boundary are separately counted low-wire-
// resistance approximations. They also use raw Elmore/threshold conversion;
// strict failures and successfully resolved fits outside their scope remain.
Driver_waveform fit_driver(const liberty::timing::Table& delay, const liberty::timing::Table& slew, double input_slew_ps,
                           double total_cap_ff, const liberty::timing::Thresholds& thresholds, unsigned transition,
                           const Pi_model* pi = nullptr, Driver_options options = {});
}  // namespace livehd::placement::late
