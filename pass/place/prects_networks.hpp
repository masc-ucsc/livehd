// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/timing_feedback.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::liberty {
struct Cell_library;
}

namespace livehd::placement {
// Read once per placement invocation; never infer a signal role from a cell or
// net name. Internal pin seeds come from synthesis signal-role metadata. A
// nonempty explicit control_ports list replaces optional automatic roots;
// recognized reset roots always remain false paths for placement timing.
struct Prects_library {
  std::map<std::string, std::set<std::string>>                            controls, clocks, resets;
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> links;
  std::set<std::string>                                                   ports, pins, clock_ports, clock_pins;
  std::set<std::string>                                                   reset_ports, reset_pins;
  bool                                                                    override_controls = false;
};
Prects_library prects_library(const std::string& liberty, const std::string& control_ports = {});
Prects_library prects_library(const liberty::Cell_library& library, const std::string& control_ports = {});
struct Prects_networks {
  std::vector<bool>     ignored;
  std::set<std::string> pins;
  // Physical pin identities on excluded non-clock nets. STA cuts their net
  // arcs for optimization while retaining clock launch/capture and all RC.
  std::set<std::string> timing_cuts;
};
Prects_networks  prects_networks(const physical::Technology&, const physical::Floorplan&, const Prects_library&,
                                 const timing::Feedback& = {});
// A private optimization view. Lvalue inputs retain the original STA result;
// temporary feedback transfers ownership instead of copying every pin record.
// Exported connectivity and downstream recovery/removal checks stay intact.
timing::Feedback prects_data_timing(timing::Feedback, const Prects_networks&);
}  // namespace livehd::placement
