// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <chrono>

#include "core/timing_whatif.hpp"

namespace livehd::placement {
// Two Liberty HOLD_BUF delays at the platform's explicit nominal slew/load.
// Query only: does not instantiate a cell or change the timing session. The
// host resolves the buffer and nominal operating point; missing models fail.
double preroute_hold_margin(timing::Whatif&, const std::string& master, const std::string& input, const std::string& output,
                            double nominal_slew_ps, double nominal_load_ff);
struct Preroute_timing_limits {
  double                                setup_margin_ps = 20;
  // Must be supplied from platform HOLD_BUF characterization or an explicit
  // option; absent characterization must never become a zero hold margin.
  double                                hold_margin_ps  = -1;
  std::chrono::steady_clock::time_point deadline        = std::chrono::steady_clock::time_point::max();
};
struct Preroute_timing_trial {
  bool                  legal = false, timed_out = false;
  std::string           reason, pin;
  std::optional<double> setup_slack_ps, hold_slack_ps;
  // Candidate net only, excluding previously locked pins, for layer ranking.
  std::optional<double> net_setup_slack_ps, net_hold_slack_ps;
};
// Evaluate a wiring-only change on the current physical timing state. Both the
// original and routed RC must have exactly the same driver and physical pins.
// Every candidate pin and all previously locked pins need finite setup/hold
// slack meeting the margins before and after the edit, plus legal electrical
// checks. Times are ps. Restores the caller's what-if mark on every exit,
// including exceptions; accepted trials do not commit timing or physical edits.
// Passing the same RC object for both arguments checks the current installed
// timing state without reinstalling RC. The caller must have installed that RC.
// Callers must ensure clock/reset/tie/dont_touch eligibility separately.
Preroute_timing_trial check_preroute_timing(timing::Whatif&, const physical::Rc_net& original, const physical::Rc_net& routed,
                                            const std::vector<std::string>& locked_pins, const Preroute_timing_limits&);
// The host supplies characterized noninverting buffers, selected by the existing
// Steiner repair. Inputs/outputs are Liberty pin names, not physical labels.
struct Preroute_repeater {
  std::string instance, master, input, output;
};
// Validate an exact rooted subdivision of original: preserve every original
// endpoint once, connect every new buffer once, reject cycles/disconnected trees.
// Checks original margins before adding cells, then margins/electrical checks on
// all new and original pins with actual routed branch RC. Never publishes edits.
Preroute_timing_trial check_preroute_repeater_timing(timing::Whatif&, const physical::Rc_net& original,
                                                     const std::vector<physical::Rc_net>& branches,
                                                     const std::vector<Preroute_repeater>&,
                                                     const std::vector<std::string>& locked_pins, const Preroute_timing_limits&);
}  // namespace livehd::placement
