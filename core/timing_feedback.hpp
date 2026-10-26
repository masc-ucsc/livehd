// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace livehd::timing {
// In-process physical timing feedback. Reports are optional consumers of this
// data, never the transport between timing and placement/repair.
struct Feedback {
  struct Pin {
    std::optional<double> arrival, required, slew, load_ff, slack, capacitance_ff;
    unsigned              violations        = 0;
    double                electrical_excess = 0;
    bool                  clock             = false;
    std::optional<double> hold_slack{};  // MIN-corner slack, in time_unit_ps units
  };
  double                     time_unit_ps = 1000;
  std::map<std::string, Pin> pins;
  double                     delay = 0, area = 0, electrical_excess = 0;
  unsigned                   violations              = 0;
  bool                       electrical_pin_feedback = false;
  std::string                source_report;  // optional external-report provenance only
  std::optional<double>      wns, tns, hold_wns;
  std::vector<std::string>   path;
  // Physical connection lookup: a top port (instance "PIN") is keyed by its
  // name; an instance pin by "inst:pin", else the "inst/pin" spelling.
  const Pin*                 find_pin(const std::string& instance, const std::string& pin) const {
    auto it = pins.find(instance == "PIN" ? pin : instance + ":" + pin);
    if (it == pins.end() && instance != "PIN") {
      it = pins.find(instance + "/" + pin);
    }
    return it == pins.end() ? nullptr : &it->second;
  }
};
}  // namespace livehd::timing
