// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/timing_feedback.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
// Read once per placement invocation; never infer a signal role from a cell or
// net name. Internal pin seeds come from synthesis signal-role metadata. A
// nonempty explicit control_ports list replaces automatic clock/reset roots.
struct Prects_library {
  std::map<std::string, std::set<std::string>>                            controls, clocks;
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> links;
  std::set<std::string>                                                   ports, pins, clock_ports, clock_pins;
  bool                                                                    override_controls = false;
};
Prects_library prects_library(const std::string& liberty, const std::string& control_ports = {});
struct Prects_networks {
  std::vector<bool>     ignored;
  std::set<std::string> pins;
  // Physical pin identities on excluded non-clock nets. STA cuts their net
  // arcs for optimization while retaining clock launch/capture and all RC.
  std::set<std::string> timing_cuts;
};
Prects_networks  prects_networks(const physical::Technology&, const physical::Floorplan&, const Prects_library&,
                                 const timing::Feedback& = {});
// A private optimization view. The original STA result and exported connectivity
// remain intact, including recovery/removal checks in downstream tools.
timing::Feedback prects_data_timing(const timing::Feedback&, const Prects_networks&);
}  // namespace livehd::placement
