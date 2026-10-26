// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "preroute_timing.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace livehd::placement {
double preroute_hold_margin(timing::Whatif& timer, const std::string& master, const std::string& input, const std::string& output,
                            double nominal_slew_ps, double nominal_load_ff) {
  if (master.empty() || input.empty() || output.empty() || input == output || !std::isfinite(nominal_slew_ps)
      || !std::isfinite(nominal_load_ff) || nominal_slew_ps < 0 || nominal_load_ff < 0) {
    throw std::invalid_argument("invalid HOLD_BUF nominal operating point");
  }
  const auto arc = timer.arc(master, input, output, nominal_slew_ps, nominal_load_ff);
  if (!arc || !std::isfinite(arc->first) || arc->first <= 0 || !std::isfinite(2 * arc->first)) {
    throw std::invalid_argument("HOLD_BUF requires a positive characterized Liberty delay");
  }
  return 2 * arc->first;
}
namespace {
std::set<std::string> endpoints(const physical::Rc_net& net) {
  physical::validate_rc_tree(net);
  std::set<std::string> pins;
  for (const auto& n : net.nodes) {
    if (!std::isfinite(n.capacitance_ff) || n.capacitance_ff < 0) {
      throw std::invalid_argument("invalid pre-route capacitance");
    }
    if (!n.pin.empty() && !pins.insert(n.pin).second) {
      throw std::invalid_argument("duplicate pre-route RC endpoint");
    }
  }
  for (const auto& e : net.edges) {
    if (!std::isfinite(e.resistance_ohm) || e.resistance_ohm < 0) {
      throw std::invalid_argument("invalid pre-route resistance");
    }
  }
  if (!pins.contains(net.driver) || pins.size() < 2) {
    throw std::invalid_argument("invalid pre-route RC driver/endpoints");
  }
  return pins;
}
}  // namespace
Preroute_timing_trial check_preroute_timing(timing::Whatif& timer, const physical::Rc_net& original, const physical::Rc_net& routed,
                                            const std::vector<std::string>& locked, const Preroute_timing_limits& limits) {
  Preroute_timing_trial out;
  if (!std::isfinite(limits.setup_margin_ps) || !std::isfinite(limits.hold_margin_ps) || limits.setup_margin_ps < 0
      || limits.hold_margin_ps < 0) {
    throw std::invalid_argument("pre-route setup and characterized hold margins are required");
  }
  auto pins = endpoints(original);
  if (original.driver != routed.driver || pins != endpoints(routed)) {
    throw std::invalid_argument("pre-route timing cannot change connectivity");
  }
  const auto net_pins = pins;
  for (const auto& pin : locked) {
    if (pin.empty()) {
      throw std::invalid_argument("empty protected timing pin");
    }
    pins.insert(pin);
  }
  const std::vector<std::string> labels(pins.begin(), pins.end());
  const auto                     expired = [&] {
    if (std::chrono::steady_clock::now() < limits.deadline) {
      return false;
    }
    out.timed_out = true;
    out.reason    = "pre-route timing deadline";
    return true;
  };
  if (expired()) {
    return out;
  }
  const auto check = [&](bool after) {
    const std::string stage = after ? "routed " : "original ";
    out.setup_slack_ps.reset();
    out.hold_slack_ps.reset();
    out.net_setup_slack_ps.reset();
    out.net_hold_slack_ps.reset();
    for (const auto& pin : pins) {
      if (expired()) {
        return false;
      }
      const auto value = timer.pin(pin);
      if (!value.slack || !value.hold_slack || !std::isfinite(*value.slack) || !std::isfinite(*value.hold_slack)) {
        out.reason = stage + "pin lacks constrained setup/hold slack";
        out.pin    = pin;
        return false;
      }
      if (net_pins.contains(pin)) {
        out.net_setup_slack_ps = out.net_setup_slack_ps ? std::min(*out.net_setup_slack_ps, *value.slack) : *value.slack;
        out.net_hold_slack_ps  = out.net_hold_slack_ps ? std::min(*out.net_hold_slack_ps, *value.hold_slack) : *value.hold_slack;
      }
      out.setup_slack_ps = out.setup_slack_ps ? std::min(*out.setup_slack_ps, *value.slack) : *value.slack;
      out.hold_slack_ps  = out.hold_slack_ps ? std::min(*out.hold_slack_ps, *value.hold_slack) : *value.hold_slack;
      if (*value.slack < limits.setup_margin_ps) {
        out.reason = stage + "setup margin";
        out.pin    = pin;
        return false;
      }
      if (*value.hold_slack < limits.hold_margin_ps) {
        out.reason = stage + "hold margin";
        out.pin    = pin;
        return false;
      }
    }
    const auto [violations, excess] = timer.electrical(labels);
    if (violations || !std::isfinite(excess) || excess != 0) {
      out.reason = stage + "electrical limits";
      return false;
    }
    return !expired();
  };
  const auto mark = timer.mark();
  try {
    if (check(false)) {
      if (&original == &routed) {
        // Eligibility and installed-tree checks pass the current RC twice.
        // Reinstalling it invalidates timing and journals a redundant edit.
        out.legal = true;
      } else {
        timer.set_net(routed);
        out.legal = check(true);
      }
    }
  } catch (...) {
    timer.rollback(mark);
    throw;
  }
  timer.rollback(mark);
  if (expired()) {
    out.legal = false;
  }
  return out;
}
Preroute_timing_trial check_preroute_repeater_timing(timing::Whatif& timer, const physical::Rc_net& original,
                                                     const std::vector<physical::Rc_net>&  branches,
                                                     const std::vector<Preroute_repeater>& repeaters,
                                                     const std::vector<std::string>& locked, const Preroute_timing_limits& limits) {
  const auto original_pins = endpoints(original);
  if (repeaters.empty() || branches.size() != repeaters.size() + 1) {
    throw std::invalid_argument("pre-route repeater tree requires one branch per driver");
  }
  std::set<std::string>              names, expected_pins = original_pins;
  std::map<std::string, std::string> input_to_output;
  std::set<std::string>              drivers{original.driver};
  for (const auto& cell : repeaters) {
    if (cell.instance.empty() || cell.master.empty() || cell.input.empty() || cell.output.empty() || cell.input == cell.output
        || cell.instance.find(':') != std::string::npos || cell.input.find(':') != std::string::npos
        || cell.output.find(':') != std::string::npos || !names.insert(cell.instance).second) {
      throw std::invalid_argument("invalid pre-route repeater identity");
    }
    const auto in = cell.instance + ":" + cell.input, out = cell.instance + ":" + cell.output;
    for (const auto& pin : original_pins) {
      if (pin.starts_with(cell.instance + ":")) {
        throw std::invalid_argument("pre-route repeater reuses an original instance");
      }
    }
    expected_pins.insert(in);
    expected_pins.insert(out);
    input_to_output.emplace(in, out);
    drivers.insert(out);
  }
  std::set<std::string>                           seen;
  std::map<std::string, std::vector<std::string>> children;
  for (const auto& branch : branches) {
    if (!drivers.contains(branch.driver) || children.contains(branch.driver)) {
      throw std::invalid_argument("duplicate or foreign pre-route branch driver");
    }
    auto& next = children[branch.driver];
    for (const auto& pin : endpoints(branch)) {
      if (!expected_pins.contains(pin) || !seen.insert(pin).second || (pin != branch.driver && drivers.contains(pin))) {
        throw std::invalid_argument("pre-route repeater tree changes or duplicates endpoints");
      }
      if (const auto child = input_to_output.find(pin); child != input_to_output.end()) {
        next.push_back(child->second);
      }
    }
  }
  if (seen != expected_pins) {
    throw std::invalid_argument("pre-route repeater tree loses endpoints");
  }
  std::set<std::string>    reached;
  std::vector<std::string> pending{original.driver};
  while (!pending.empty()) {
    auto driver = std::move(pending.back());
    pending.pop_back();
    if (!reached.insert(driver).second) {
      throw std::invalid_argument("cycle in pre-route repeater tree");
    }
    const auto& next = children.at(driver);
    pending.insert(pending.end(), next.begin(), next.end());
  }
  if (reached != drivers) {
    throw std::invalid_argument("disconnected pre-route repeater tree");
  }
  auto trial = check_preroute_timing(timer, original, original, locked, limits);
  if (!trial.legal) {
    return trial;
  }
  const auto mark = timer.mark();
  try {
    for (const auto& cell : repeaters) {
      timer.add_cell(cell.instance, cell.master);
    }
    for (const auto& branch : branches) {
      timer.set_net(branch);
    }
    std::vector<std::string> all_pins(locked);
    all_pins.insert(all_pins.end(), seen.begin(), seen.end());
    trial = check_preroute_timing(timer, branches.front(), branches.front(), all_pins, limits);
    // This check starts with the candidate tree already installed.
    if (trial.reason.starts_with("original ")) {
      trial.reason.replace(0, 9, "repeated ");
    }
  } catch (...) {
    timer.rollback(mark);
    throw;
  }
  timer.rollback(mark);
  if (std::chrono::steady_clock::now() >= limits.deadline) {
    trial.legal     = false;
    trial.timed_out = true;
    trial.reason    = "pre-route timing deadline";
  }
  return trial;
}
}  // namespace livehd::placement
