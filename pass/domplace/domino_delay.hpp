// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "domino_reference.hpp"
#include "function.hpp"
#include "pass/liberty/timing_model.hpp"

// The built-in delay estimate of a provisional domino stage (2d-domplace U1):
// Sutherland-Sproull-Harris logical effort for a footed domino gate, in units
// of the technology's tau, with tau taken from the reference inverter's FO4
// delay measured on its Liberty NLDM tables. This file is the ONE place the
// formula lives; the bundle records its result, its parameters and
// kDelaySource for every coefficient it supplies. Explicit FO4 options
// override it coefficient by coefficient.
namespace livehd::domplace {

inline constexpr std::string_view kDelaySource = "logical-effort-estimate";
inline constexpr std::string_view kDelayModel  = "Sutherland-Sproull-Harris logical effort, footed domino gate + output inverter";
// The formula, written into the sidecar beside the parameters (s = stack,
// b = branches, n = PDN transistors, k = internal_load, w = precharge_width,
// l = internal_node_weight, p = p_inv, C_D = the variant register's D input,
// C_in = the reference inverter's input).
inline constexpr std::string_view kDelayFormula
    = "tau = FO4 / (4 + p); "
      "eval = tau * [ (s+1)/3 * k + p * (s+1) * (b + l*(n-b) + w/3) / 3 + C_D / (k*C_in) + p ]; "
      "precharge = tau * 2 * (3k + p * (3b + w)) / (3w); "
      "setup_eval = setup_eval_fo4 * FO4; precharge_margin = precharge_margin_fo4 * FO4";

// The FO4 delay of the reference inverter on its NLDM tables: the inverter
// driving four of its own inputs, each edge's input slew equal to the opposite
// output edge's transition at that load (a fixed point, as in a chain of FO4
// inverters). FO4 is the mean of the rise and fall delays. Slew axes and
// transition values share the Liberty's own measurement thresholds, so no
// derating is applied.
struct Fo4_derivation {
  std::string inverter, input_pin, output_pin;
  double      c_in_ff = 0, load_ff = 0;            // load = 4 * c_in
  double      slew_rise_ps = 0, slew_fall_ps = 0;  // self-consistent output transitions
  double      delay_rise_ps = 0, delay_fall_ps = 0;
  double      fo4_ps     = 0;
  uint32_t    iterations = 0;
};
// nullopt with the reason in `why` when the cell has no single input, no
// combinational arc with all four rise/fall delay and transition tables, a
// non-positive c_in_ff, or the slew iteration does not converge.
std::optional<Fo4_derivation> derive_fo4(const liberty::timing::Cell& inverter, double c_in_ff, std::string* why);

// Fixed built-in parameters, recorded in the sidecar. Units are logical-effort
// widths: the unit inverter is NMOS 1 + PMOS 2 (input 3), and every rail is
// one reference-inverter input, so each pull-down device is width 3.
struct Delay_parameters {
  double p_inv                = 1;    // reference-inverter parasitic delay in tau (FO4 = 5 tau)
  double precharge_width      = 2;    // precharge PMOS width: a unit inverter's pull-up
  double internal_load        = 1;    // output inverter on the dynamic node, in reference-inverter inputs
  double internal_node_weight = 0.5;  // diffusion of a device below a branch top, relative to the dynamic node
  double setup_eval_fo4       = 0.5;  // rail setup before evaluate
  double precharge_margin_fo4 = 0.5;  // precharge margin
};

// The technology half of the estimate: FO4 and tau, or why they are unknown.
struct Delay_model {
  Delay_parameters              parameters;
  std::optional<Fo4_derivation> fo4;
  std::optional<double>         tau_ps, c_in_ff;
  std::string                   issue;  // why fo4 is absent; every estimate is then null
};
// Derive FO4 from reference.inverter_timing at the reference inverter's input
// capacitance. A missing NLDM model or capacitance leaves fo4 absent and names
// it in `issue`; nothing defaults to zero.
Delay_model domino_delay_model(const Domino_reference& reference, const Delay_parameters& parameters = {});

// One master variant's estimate with its logical-effort terms (in tau).
struct Stage_delay {
  double g_dynamic = 0, h_dynamic = 0, p_dynamic = 0;  // footed pull-down into the output inverter
  double h_output = 0, p_output = 0;                   // output inverter into the register D input
  double c_dynamic = 0;                                // dynamic-node capacitance, in unit-inverter input widths
  double eval_tau = 0, precharge_tau = 0;
  double eval_ps = 0, precharge_ps = 0;
};
// nullopt with the reason in `why` when the model has no tau, the register D
// capacitance is unknown, or the metrics are degenerate (no stack or branch).
std::optional<Stage_delay> estimate_stage_delay(const Delay_model& model, const usyn::Gate_formula::Metrics& metrics,
                                                std::optional<double> register_d_ff, std::string* why);

}  // namespace livehd::domplace
